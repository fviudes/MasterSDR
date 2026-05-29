#include "core/CivToVita49Bridge.h"
#include "core/IcomCivProtocol.h"
#include "core/LogManager.h"

#include <QDebug>
#include <QDateTime>
#include <QtEndian>
#include <QCryptographicHash>

namespace MasterSDR {

// ────────────────────────────────────────────────────────────────
//  Construction / Destruction
// ────────────────────────────────────────────────────────────────

CivToVita49Bridge::CivToVita49Bridge(QObject* parent)
    : QObject(parent)
    , m_vita49Addr(QHostAddress::LocalHost)
    , m_elapsed()
{
    m_ctrlSocket = new QUdpSocket(this);
    connect(m_ctrlSocket, &QUdpSocket::readyRead, this, &CivToVita49Bridge::onCtrlReadyRead);

    m_audioSocket = new QUdpSocket(this);
    connect(m_audioSocket, &QUdpSocket::readyRead, this, &CivToVita49Bridge::onAudioReadyRead);

    m_keepAliveTimer = new QTimer(this);
    m_keepAliveTimer->setInterval(KEEPALIVE_MS);
    connect(m_keepAliveTimer, &QTimer::timeout, this, &CivToVita49Bridge::onKeepAlive);

    m_elapsed.start();
}

CivToVita49Bridge::~CivToVita49Bridge()
{
    disconnectFromRadio();
}

// ────────────────────────────────────────────────────────────────
//  Icom IP Packet construction (16-byte header, Little Endian)
// ────────────────────────────────────────────────────────────────

QByteArray CivToVita49Bridge::buildIcomPacket(uint16_t type, uint16_t seq,
                                               uint16_t dstPort, uint16_t dstId,
                                               const QByteArray& payload)
{
    uint32_t totalLen = HEADER_SIZE + static_cast<uint32_t>(payload.size());
    QByteArray pkt;
    pkt.append(reinterpret_cast<const char*>(&totalLen), 4);
    pkt.append(reinterpret_cast<const char*>(&type), 2);
    pkt.append(reinterpret_cast<const char*>(&seq), 2);
    pkt.append(reinterpret_cast<const char*>(&m_sourcePort), 2);
    pkt.append(reinterpret_cast<const char*>(&m_sourceId), 2);
    pkt.append(reinterpret_cast<const char*>(&dstPort), 2);
    pkt.append(reinterpret_cast<const char*>(&dstId), 2);
    if (!payload.isEmpty()) {
        pkt.append(payload);
    }
    return pkt;
}

void CivToVita49Bridge::sendIcomCtrlPacket(uint16_t type, uint16_t seq,
                                            uint16_t dstPort, uint16_t dstId,
                                            const QByteArray& payload)
{
    QByteArray pkt = buildIcomPacket(type, seq, dstPort, dstId, payload);
    m_ctrlSocket->writeDatagram(pkt, m_host, m_ctrlPort);
}

void CivToVita49Bridge::sendIcomCivCommand(uint8_t cmd, uint8_t subCmd,
                                            const QByteArray& data)
{
    IcomCivProtocol proto(m_civAddr);
    QByteArray civFrame = proto.buildCommand(cmd, subCmd, data);

    QByteArray pkt = buildIcomPacket(TYPE_DATA, m_seq++, m_ctrlPort, m_destId, civFrame);
    m_ctrlSocket->writeDatagram(pkt, m_host, m_ctrlPort);
}

// ────────────────────────────────────────────────────────────────
//  Connection lifecycle
// ────────────────────────────────────────────────────────────────

void CivToVita49Bridge::connectToRadio(const QString& host,
                                        uint16_t ctrlPort,
                                        uint16_t serialPort,
                                        uint16_t audioPort)
{
    m_host = QHostAddress(host);
    m_ctrlPort = ctrlPort;
    m_serialPort = serialPort;
    m_audioPort = audioPort;
    m_connected = false;
    m_seq = 0;
    m_pingSeq = 0;
    m_audioSeq = 0;
    m_fftSeq = 0;
    m_meterSeq = 0;

    m_state = ConnectionState::Connecting;
    emit stateChanged(m_state);

    m_ctrlSocket->close();
    m_ctrlSocket->bind();

    m_audioSocket->close();
    m_audioSocket->bind();

    m_sourcePort = static_cast<uint16_t>(m_ctrlSocket->localPort());
    m_sourceId = 0;

    qCDebug(lcConnection) << "CivToVita49Bridge: connecting to" << host
             << "ctrl:" << ctrlPort << "src:" << m_sourcePort;

    sendIcomCtrlPacket(TYPE_SYN, 0, m_destPort, m_destId);
    m_state = ConnectionState::Handshaking;
    emit stateChanged(m_state);
}

void CivToVita49Bridge::disconnectFromRadio()
{
    m_keepAliveTimer->stop();
    if (m_connected) {
        sendIcomCtrlPacket(TYPE_DISCON, m_seq++, m_destPort, m_destId);
    }
    m_connected = false;
    m_ctrlSocket->close();
    m_audioSocket->close();
    m_state = ConnectionState::Disconnected;
    emit stateChanged(m_state);
    emit disconnected();
}

void CivToVita49Bridge::setCivAddress(uint8_t addr)
{
    m_civAddr = addr;
}

void CivToVita49Bridge::setVita49Target(uint16_t udpPort)
{
    m_vita49Port = udpPort;
}

// ────────────────────────────────────────────────────────────────
//  Icom IP packet processing
// ────────────────────────────────────────────────────────────────

void CivToVita49Bridge::onCtrlReadyRead()
{
    while (m_ctrlSocket->hasPendingDatagrams()) {
        QByteArray data;
        data.resize(static_cast<int>(m_ctrlSocket->pendingDatagramSize()));
        QHostAddress sender;
        quint16 senderPort = 0;
        m_ctrlSocket->readDatagram(data.data(), data.size(), &sender, &senderPort);

        // Raw CI-V frames (no 16-byte header) from serial port
        if (data.size() >= 6 && data.size() < 60
            && static_cast<uint8_t>(data[0]) == IcomCivProtocol::PREAMBLE1
            && static_cast<uint8_t>(data[1]) == IcomCivProtocol::PREAMBLE2) {
            parseCivResponse(data);
            continue;
        }

        if (data.size() < HEADER_SIZE) continue;

        processIcomPacket(data, senderPort);
    }
}

void CivToVita49Bridge::processIcomPacket(const QByteArray& data, quint16 senderPort)
{
    if (data.size() < HEADER_SIZE) return;

    uint16_t type   = qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(data.constData()) + 4);
    Q_UNUSED(seq)
    uint16_t srcPort = qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(data.constData()) + 8);
    uint16_t srcId  = qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(data.constData()) + 10);

    // Control channel messages
    if (senderPort == m_ctrlPort) {
        switch (type) {
        case TYPE_SYN_ACK:
            m_destPort = srcPort;
            m_destId = srcId;
            qCDebug(lcConnection) << "CivToVita49Bridge: SYN-ACK, sending READY";
            sendIcomCtrlPacket(TYPE_READY, m_seq++, m_destPort, m_destId);
            break;

        case TYPE_READY:
            if (!m_connected) {
                m_connected = true;
                m_state = ConnectionState::Connected;
                emit stateChanged(m_state);
                emit connected();

                m_keepAliveTimer->start();

                // Register serial + audio channels
                {
                    QByteArray pkt = buildIcomPacket(TYPE_DATA, m_seq++, m_serialPort, m_destId);
                    m_ctrlSocket->writeDatagram(pkt, m_host, m_serialPort);
                }
                {
                    QByteArray pkt = buildIcomPacket(TYPE_DATA, m_seq++, m_audioPort, m_destId);
                    m_audioSocket->writeDatagram(pkt, m_host, m_audioPort);
                }

                // Initial state poll
                sendIcomCivCommand(IcomCivProtocol::CMD_READ_VFO);
                sendIcomCivCommand(IcomCivProtocol::CMD_MODE, IcomCivProtocol::SUB_MODE_READ);
                sendIcomCivCommand(IcomCivProtocol::CMD_S_METER, IcomCivProtocol::SUB_SMETER);
                sendIcomCivCommand(IcomCivProtocol::CMD_S_METER, IcomCivProtocol::SUB_SQUELCH);
            }
            break;

        case TYPE_PING:
            sendIcomCtrlPacket(TYPE_PING, m_pingSeq++, m_destPort, m_destId);
            break;

        case TYPE_DISCON:
            disconnectFromRadio();
            break;
        }
    }

    // DATA with CI-V payload
    if (type == TYPE_DATA && data.size() > HEADER_SIZE) {
        QByteArray payload = data.mid(HEADER_SIZE);

        bool hasCivPreamble = (payload.size() >= 5
            && static_cast<uint8_t>(payload[0]) == IcomCivProtocol::PREAMBLE1
            && static_cast<uint8_t>(payload[1]) == IcomCivProtocol::PREAMBLE2);

        bool hasRawCiv = (payload.size() >= 4
            && (static_cast<uint8_t>(payload[0]) == IcomCivProtocol::HOST_ADDR
                || static_cast<uint8_t>(payload[0]) == m_civAddr));

        if (hasCivPreamble) {
            parseCivResponse(payload);
        } else if (hasRawCiv) {
            QByteArray full = payload;
            full.prepend(static_cast<char>(IcomCivProtocol::PREAMBLE2));
            full.prepend(static_cast<char>(IcomCivProtocol::PREAMBLE1));
            parseCivResponse(full);
        }
    }
}

void CivToVita49Bridge::parseCivResponse(const QByteArray& data)
{
    IcomCivProtocol proto(m_civAddr);
    CivResponse resp = proto.parseResponse(data);
    if (!resp.valid) return;

    handleCivResponse(resp.cmd, resp.subCmd, resp.data);
}

// ────────────────────────────────────────────────────────────────
//  CI-V response → Radio state + VITA-49 meter synthesis
// ────────────────────────────────────────────────────────────────

void CivToVita49Bridge::handleCivResponse(uint8_t cmd, uint8_t subCmd, const QByteArray& data)
{
    switch (cmd) {

    // ── Frequency (mandatory for every poll cycle) ──
    case IcomCivProtocol::CMD_FREQ:
    case IcomCivProtocol::CMD_READ_VFO: {
        if (data.size() >= 5) {
            uint64_t freq = IcomCivProtocol::decodeBcdFreq(data);
            if (freq >= 30000 && freq <= 3000000000ULL) {
                m_rxFreq = freq;
                emit frequencyUpdated(freq);
                emit radioInfoUpdated();
            }
        }
        break;
    }

    // ── Mode ──
    case IcomCivProtocol::CMD_MODE: {
        if (!data.isEmpty()) {
            QString modeStr = IcomCivProtocol::modeToString(
                static_cast<IcomCivProtocol::CivMode>(data[0]));
            if (!modeStr.isEmpty()) {
                m_rxMode = modeStr;
                emit modeUpdated(modeStr);
                emit radioInfoUpdated();
            }
        }
        break;
    }

    // ── S-Meter + Squelch → also synthesized as VITA-49 meter ──
    case IcomCivProtocol::CMD_S_METER: {
        if (!data.isEmpty()) {
            int level = static_cast<int>(static_cast<uint8_t>(data[0]));
            if (subCmd == IcomCivProtocol::SUB_SMETER) {
                m_sMeter = level;
                emit sMeterUpdated(level);
            } else if (subCmd == IcomCivProtocol::SUB_SQUELCH) {
                m_squelchOpen = (data[0] != 0x00);
            }

            // Synthesize VITA-49 meter packet
            if (m_vita49Enabled && m_vita49Port > 0) {
                synthesizeVita49Meters();
            }
        }
        break;
    }

    // ── Rig identity ──
    case IcomCivProtocol::CMD_RIG_ID: {
        if (!data.isEmpty()) {
            QString model = IcomCivProtocol::rigIdToModel(static_cast<uint8_t>(data[0]));
            m_radioModel = model;
            qCDebug(lcConnection) << "CivToVita49Bridge: detected" << model;
            emit radioInfoUpdated();
        }
        break;
    }

    // ── Split ──
    case IcomCivProtocol::CMD_SPLIT:
        if (!data.isEmpty()) {
            m_split = (data[0] != 0x00);
            emit splitUpdated(m_split);
            emit radioInfoUpdated();
        }
        break;

    // ── Attenuator ──
    case IcomCivProtocol::CMD_ATTENUATOR:
        if (!data.isEmpty()) {
            m_attenuator = (data[0] != 0x00);
            emit attenuatorUpdated(m_attenuator);
            emit radioInfoUpdated();
        }
        break;

    // ── TX Power / RF Gain via mic gain command ──
    case IcomCivProtocol::CMD_MIC_GAIN:
        if (!data.isEmpty()) {
            int pct = static_cast<int>(static_cast<uint8_t>(data[0])) * 100 / 255;
            if (subCmd == IcomCivProtocol::SUB_TX_POWER) {
                m_txPower = pct;
                emit txPowerUpdated(m_txPower);
                // Meter synthesis
                if (m_vita49Enabled && m_vita49Port > 0) synthesizeVita49Meters();
            } else if (subCmd == IcomCivProtocol::SUB_RF_GAIN) {
                m_rfGain = pct;
                emit rfGainUpdated(m_rfGain);
                if (m_vita49Enabled && m_vita49Port > 0) synthesizeVita49Meters();
            }
            emit radioInfoUpdated();
        }
        break;

    // ── Preamp / BKIN / APF ──
    case IcomCivProtocol::CMD_PREAMP:
        if (!data.isEmpty()) {
            if (subCmd == IcomCivProtocol::SUB_PREAMP) {
                m_preamp = static_cast<int>(data[0]);
                emit preampUpdated(m_preamp);
            } else if (subCmd == IcomCivProtocol::SUB_BKIN) {
                m_bkInMode = static_cast<int>(data[0]);
                emit bkInUpdated(m_bkInMode);
            } else if (subCmd == IcomCivProtocol::SUB_APF) {
                m_apfMode = static_cast<int>(data[0]);
                emit apfUpdated(m_apfMode);
            }
            emit radioInfoUpdated();
        }
        break;

    // ── PTT feedback ──
    case IcomCivProtocol::CMD_PTT:
        if (!data.isEmpty()) {
            m_ptt = (data[0] != 0x00);
            emit pttStateChanged(m_ptt);
            emit radioInfoUpdated();
        }
        break;

    // ── Spectrum scope data → handled on audio port, not here ──
    case IcomCivProtocol::CMD_SPECTRUM:
        break;

    default:
        break;
    }
}

// ────────────────────────────────────────────────────────────────
//  Keepalive (3-second polling + PING)
// ────────────────────────────────────────────────────────────────

void CivToVita49Bridge::onKeepAlive()
{
    if (!m_connected) return;

    // PING on ctrl
    sendIcomCtrlPacket(TYPE_PING, m_pingSeq++, m_destPort, m_destId);

    // Idle on serial
    {
        QByteArray idle = buildIcomPacket(TYPE_DATA, m_pingSeq++, m_serialPort, m_destId);
        m_ctrlSocket->writeDatagram(idle, m_host, m_serialPort);
    }

    // Idle on audio
    {
        QByteArray idle = buildIcomPacket(TYPE_DATA, m_pingSeq++, m_audioPort, m_destId);
        m_audioSocket->writeDatagram(idle, m_host, m_audioPort);
    }

    // Poll all CI-V parameters
    sendIcomCivCommand(IcomCivProtocol::CMD_READ_VFO);
    sendIcomCivCommand(IcomCivProtocol::CMD_MODE, IcomCivProtocol::SUB_MODE_READ);
    sendIcomCivCommand(IcomCivProtocol::CMD_S_METER, IcomCivProtocol::SUB_SMETER);
    sendIcomCivCommand(IcomCivProtocol::CMD_S_METER, IcomCivProtocol::SUB_SQUELCH);
    sendIcomCivCommand(IcomCivProtocol::CMD_MIC_GAIN, IcomCivProtocol::SUB_TX_POWER);
    sendIcomCivCommand(IcomCivProtocol::CMD_MIC_GAIN, IcomCivProtocol::SUB_RF_GAIN);
    sendIcomCivCommand(IcomCivProtocol::CMD_SPLIT);
    sendIcomCivCommand(IcomCivProtocol::CMD_PREAMP, IcomCivProtocol::SUB_PREAMP);
    sendIcomCivCommand(IcomCivProtocol::CMD_ATTENUATOR);
}

// ────────────────────────────────────────────────────────────────
//  Application commands → CI-V
// ────────────────────────────────────────────────────────────────

void CivToVita49Bridge::setFrequency(uint64_t freqHz)
{
    m_rxFreq = freqHz;
    QByteArray bcd = IcomCivProtocol::encodeBcdFreq(freqHz);
    sendIcomCivCommand(IcomCivProtocol::CMD_FREQ, 0, bcd);
    emit frequencyUpdated(freqHz);
}

void CivToVita49Bridge::setMode(const QString& mode)
{
    m_rxMode = mode;
    auto civMode = IcomCivProtocol::modeFromString(mode);
    QByteArray data;
    data.append(static_cast<char>(civMode));
    data.append(static_cast<char>(0x00));
    sendIcomCivCommand(IcomCivProtocol::CMD_MODE, IcomCivProtocol::SUB_MODE_SET, data);
    emit modeUpdated(mode);
}

void CivToVita49Bridge::setPtt(bool tx)
{
    m_ptt = tx;
    QByteArray data;
    data.append(tx ? '\x01' : '\x00');
    sendIcomCivCommand(IcomCivProtocol::CMD_PTT, tx ? IcomCivProtocol::SUB_PTT_TX : IcomCivProtocol::SUB_PTT_RX, data);
    emit pttStateChanged(tx);
}

void CivToVita49Bridge::setSplit(bool on)
{
    m_split = on;
    QByteArray data;
    data.append(on ? '\x01' : '\x00');
    sendIcomCivCommand(IcomCivProtocol::CMD_SPLIT, 0, data);
    emit splitUpdated(on);
}

void CivToVita49Bridge::setTxPower(int pct)
{
    m_txPower = pct;
    uint8_t rawVal = static_cast<uint8_t>(pct * 255 / 100);
    QByteArray data;
    data.append(static_cast<char>(rawVal));
    sendIcomCivCommand(IcomCivProtocol::CMD_MIC_GAIN, IcomCivProtocol::SUB_TX_POWER, data);
    emit txPowerUpdated(pct);
}

void CivToVita49Bridge::setRfGain(int pct)
{
    m_rfGain = pct;
    uint8_t rawVal = static_cast<uint8_t>(pct * 255 / 100);
    QByteArray data;
    data.append(static_cast<char>(rawVal));
    sendIcomCivCommand(IcomCivProtocol::CMD_MIC_GAIN, IcomCivProtocol::SUB_RF_GAIN, data);
    emit rfGainUpdated(pct);
}

void CivToVita49Bridge::setAttenuator(bool on)
{
    m_attenuator = on;
    QByteArray data;
    data.append(on ? '\x01' : '\x00');
    sendIcomCivCommand(IcomCivProtocol::CMD_ATTENUATOR, 0, data);
    emit attenuatorUpdated(on);
}

void CivToVita49Bridge::setPreamp(int level)
{
    m_preamp = level;
    QByteArray data;
    data.append(static_cast<char>(level));
    sendIcomCivCommand(IcomCivProtocol::CMD_PREAMP, IcomCivProtocol::SUB_PREAMP, data);
    emit preampUpdated(level);
}

// ────────────────────────────────────────────────────────────────
//  Audio port: PCM audio + spectrum scope data
// ────────────────────────────────────────────────────────────────

void CivToVita49Bridge::onAudioReadyRead()
{
    while (m_audioSocket->hasPendingDatagrams()) {
        QByteArray data;
        data.resize(static_cast<int>(m_audioSocket->pendingDatagramSize()));
        m_audioSocket->readDatagram(data.data(), data.size());

        if (data.size() <= HEADER_SIZE) continue;

        QByteArray payload = data.mid(HEADER_SIZE);

        if (m_vita49Enabled && m_vita49Port > 0) {
            // IC-705 sends scope data (>=400 bytes) + PCM audio intermixed
            if (payload.size() >= 400) {
                synthesizeVita49Fft(payload);
            } else {
                synthesizeVita49Audio(payload);
            }
        }
    }
}

// ────────────────────────────────────────────────────────────────
//  VITA-49 Header construction
// ────────────────────────────────────────────────────────────────

QByteArray CivToVita49Bridge::buildVita49Header(quint16 pcc, quint32 streamId, quint32 payloadBytes)
{
    // VITA-49 ExtDataWithStream: 28 bytes total header
    // Word0: packet type=3 (IFDataExtWithStream) in bits 31:28
    //        bit 27: C=1 (ClassIdentifier present)
    //        bit 26: T=1 (Trailer present — set to 0 here, no trailer needed for meters/audio)
    //        bits 23:20: TSI
    //        bits 19:16: TSF (sequence num in stream — 4 bits)
    //        bits 15:0:  packetSize in 32-bit words (total/4)
    //
    // Word1-2: streamId (uint32 BE in lower bits)
    // Word3-4: class identifier (OUI 24bit + ICC 8bit)
    // Word5-6: OUI=0x001C2D, PCC in lower 16 bits
    // Word7: timestamp (we fill later or leave 0)

    uint32_t totalWords = (VITA49_HEADER_BYTES + payloadBytes + 3) / 4;

    QByteArray hdr(VITA49_HEADER_BYTES, '\0');

    // Word 0: packet type 0x3 (IFDataExtWithStream), C=1, T=0
    uint32_t w0 = (0x3u << 28)      // packet_type = 3 (ExtDataWithStream)
                | (0x1u << 27)       // C=1 (class ID present)
                | (0x0u << 26)       // T=0 (no trailer)
                | (static_cast<uint32_t>(m_fftSeq & 0xF) << 16)
                | (totalWords & 0xFFFFu);
    qToBigEndian(w0, reinterpret_cast<uchar*>(hdr.data()));

    // Words 1-2: streamId
    uint32_t sid = streamId;
    qToBigEndian(sid, reinterpret_cast<uchar*>(hdr.data()) + 4);

    // Word 3-4: class identifier (OUI)
    uint32_t oui = FLEX_OUI;
    qToBigEndian(oui, reinterpret_cast<uchar*>(hdr.data()) + 8);

    // Word 5-6: information class code (ICC) + packet class code (PCC)
    uint32_t iccPcc = (FLEX_ICC << 16) | pcc;
    qToBigEndian(iccPcc, reinterpret_cast<uchar*>(hdr.data()) + 12);

    // Words 7-8: timestamp (compatible nanoseconds or absolute)
    // Word 9: 0 (no trailer present)
    return hdr;
}

// ────────────────────────────────────────────────────────────────
//  VITA-49 Audio (PCC 0x03E3: float32 stereo big-endian)
// ────────────────────────────────────────────────────────────────

QByteArray CivToVita49Bridge::buildVita49AudioPacket(const QByteArray& rawPcm)
{
    // Convert Icom PCM (int16, mono, native endian) to float32 stereo BE
    int sampleCount = rawPcm.size() / 2;  // int16 = 2 bytes
    QByteArray floatPayload;
    floatPayload.reserve(sampleCount * 8); // float32 stereo = 8 bytes per sample pair

    const auto* src = reinterpret_cast<const qint16*>(rawPcm.constData());
    for (int i = 0; i < sampleCount; ++i) {
        float sample = static_cast<float>(src[i]) / 32768.0f;
        // Left channel
        uint32_t floatBits;
        memcpy(&floatBits, &sample, 4);
        uint32_t beFloat = qToBigEndian(floatBits);
        floatPayload.append(reinterpret_cast<const char*>(&beFloat), 4);
        // Right channel (same for mono source)
        floatPayload.append(reinterpret_cast<const char*>(&beFloat), 4);
    }

    QByteArray hdr = buildVita49Header(PCC_IF_DATA, 0x04000001, floatPayload.size());
    hdr.append(floatPayload);

    return hdr;
}

void CivToVita49Bridge::synthesizeVita49Audio(const QByteArray& rawPcm)
{
    m_audioSeq = (m_audioSeq + 1) & 0xF;
    QByteArray pkt = buildVita49AudioPacket(rawPcm);
    sendVita49ToLoopback(pkt);
    emit vita49AudioPacket(pkt);
}

// ────────────────────────────────────────────────────────────────
//  VITA-49 FFT (PCC 0x8003: uint16 bins big-endian)
// ────────────────────────────────────────────────────────────────

QByteArray CivToVita49Bridge::buildVita49FftPacket(const QVector<float>& bins,
                                                     quint16 startBin, quint16 totalBins)
{
    // Sub-header for FFT frame: 12 bytes
    // [0-1] startBinIndex (uint16 BE)
    // [2-3] numBins (uint16 BE)
    // [4-7] totalBinsInFrame (uint32 BE)
    // [8-11] frameIndex (uint32 BE)

    QByteArray subHdr(12, '\0');
    quint16 start = qToBigEndian(startBin);
    memcpy(subHdr.data(), &start, 2);
    quint16 count = qToBigEndian(static_cast<quint16>(bins.size()));
    memcpy(subHdr.data() + 2, &count, 2);
    quint32 total = qToBigEndian(totalBins);
    memcpy(subHdr.data() + 4, &total, 4);
    quint32 frmIdx = qToBigEndian(m_fftFrameIndex);
    memcpy(subHdr.data() + 8, &frmIdx, 4);

    // Scale Icom scope bins (0-255) to uint16 BE range (0-65535)
    // Then map to pixel Y position based on panYPixels
    QByteArray binPayload;
    for (float v : bins) {
        // Icom scope byte 0-255 → Y position 0..panYPixels-1
        float normalized = v / 255.0f;
        quint16 pixelY = static_cast<quint16>(normalized * (m_panYPixels - 1));
        quint16 beVal = qToBigEndian(pixelY);
        binPayload.append(reinterpret_cast<const char*>(&beVal), 2);
    }

    uint32_t totalPayload = subHdr.size() + binPayload.size();
    QByteArray hdr = buildVita49Header(PCC_FFT, m_panStreamId, totalPayload);
    hdr.append(subHdr);
    hdr.append(binPayload);

    return hdr;
}

void CivToVita49Bridge::synthesizeVita49Fft(const QByteArray& scopeData)
{
    m_fftSeq = (m_fftSeq + 1) & 0xF;
    m_fftFrameIndex++;

    // Icom scope data: raw byte values (0-255 intensity per bin)
    int numBins = scopeData.size();
    QVector<float> bins;
    bins.reserve(numBins);
    for (int i = 0; i < numBins; ++i) {
        bins.append(static_cast<float>(static_cast<uint8_t>(scopeData.at(i))));
    }

    QByteArray pkt = buildVita49FftPacket(bins, 0, static_cast<quint16>(numBins));
    sendVita49ToLoopback(pkt);
    emit vita49FftPacket(pkt);
}

// ────────────────────────────────────────────────────────────────
//  VITA-49 Meter (PCC 0x8002: id/value pairs)
// ────────────────────────────────────────────────────────────────

QByteArray CivToVita49Bridge::buildVita49MeterPacket(const QVector<quint16>& ids,
                                                       const QVector<qint16>& vals)
{
    QByteArray payload;
    for (int i = 0; i < ids.size(); ++i) {
        quint16 idBe = qToBigEndian(ids[i]);
        payload.append(reinterpret_cast<const char*>(&idBe), 2);
        qint16 valBe = qToBigEndian(vals[i]);
        payload.append(reinterpret_cast<const char*>(&valBe), 2);
    }

    QByteArray hdr = buildVita49Header(PCC_METER, 0x00000700, payload.size());
    hdr.append(payload);
    return hdr;
}

void CivToVita49Bridge::synthesizeVita49Meters()
{
    m_meterSeq = (m_meterSeq + 1) & 0xF;

    QVector<quint16> ids;
    QVector<qint16> vals;

    // S-meter level: convert 0-255 raw → dBm approximation
    // Icom s-meter: 0=no signal, 120=S9, 255=max pin
    // FlexRadio S meter: raw/128 = dBm, -127 to 20
    // Map: dBm ≈ -127 + (level / 12.0f) * 6.0
    float sDbm = -127.0f + (m_sMeter / 12.0f) * 6.0f;
    qint16 sRaw = static_cast<qint16>(sDbm * 128.0f);

    ids.append(IcomMeterIndex::SLICE0_LEVEL);
    vals.append(sRaw);

    ids.append(IcomMeterIndex::SLICE0_SQUELCH);
    vals.append(static_cast<qint16>(m_squelchOpen));

    // TX power: 0-100% → raw scaling
    ids.append(IcomMeterIndex::TX_POWER);
    vals.append(static_cast<qint16>(m_txPower * 128 / 100));

    // RF gain
    ids.append(IcomMeterIndex::RF_GAIN);
    vals.append(static_cast<qint16>(m_rfGain * 128 / 100));

    QByteArray pkt = buildVita49MeterPacket(ids, vals);
    sendVita49ToLoopback(pkt);
    emit vita49MeterPacket(pkt);
}

// ────────────────────────────────────────────────────────────────
//  VITA-49 loopback delivery
// ────────────────────────────────────────────────────────────────

void CivToVita49Bridge::sendVita49ToLoopback(const QByteArray& pkt)
{
    if (m_vita49Port == 0 || !m_vita49Enabled) return;

    m_ctrlSocket->writeDatagram(pkt, m_vita49Addr, m_vita49Port);
}

void CivToVita49Bridge::setVita49Enabled(bool on)
{
    m_vita49Enabled = on;
}

// ────────────────────────────────────────────────────────────────
//  Panadapter config
// ────────────────────────────────────────────────────────────────

void CivToVita49Bridge::setPanCenter(uint64_t centerHz)
{
    m_panCenterHz = centerHz;
}

void CivToVita49Bridge::setPanSpan(uint32_t spanHz)
{
    m_panSpanHz = spanHz;
}

void CivToVita49Bridge::setPanPixelCount(int xPixels, int yPixels)
{
    m_panXPixels = xPixels;
    m_panYPixels = yPixels;
}

// ────────────────────────────────────────────────────────────────
//  Status line generation (for RadioModel compatibility)
// ────────────────────────────────────────────────────────────────

QString CivToVita49Bridge::buildSliceStatus(int index) const
{
    double freqMhz = m_rxFreq / 1000000.0;
    return QStringLiteral("slice %1 RF_frequency=%2 mode=%3 pan=0x40000000 "
                          "active=1 filter_lo=-3000 filter_hi=3000")
        .arg(index)
        .arg(freqMhz, 0, 'f', 6)
        .arg(m_rxMode);
}

// ────────────────────────────────────────────────────────────────
//  Utility: BCD frequency
// ────────────────────────────────────────────────────────────────

uint64_t CivToVita49Bridge::decodeBcdFreq(const uint8_t* data, int len)
{
    uint64_t freq = 0;
    for (int i = 0; i < len && i < 5; ++i) {
        uint8_t b = data[i];
        freq = freq * 10 + ((b >> 4) & 0x0F);
        freq = freq * 10 + (b & 0x0F);
    }
    return freq;
}

QByteArray CivToVita49Bridge::encodeBcdFreq(uint64_t freqHz)
{
    return IcomCivProtocol::encodeBcdFreq(freqHz);
}

QString CivToVita49Bridge::modeByteToString(uint8_t modeByte)
{
    return IcomCivProtocol::modeToString(static_cast<IcomCivProtocol::CivMode>(modeByte));
}

uint8_t CivToVita49Bridge::modeStringToByte(const QString& mode)
{
    return static_cast<uint8_t>(IcomCivProtocol::modeFromString(mode));
}

QString CivToVita49Bridge::rigIdToModel(uint8_t rigId)
{
    return IcomCivProtocol::rigIdToModel(rigId);
}

} // namespace MasterSDR
