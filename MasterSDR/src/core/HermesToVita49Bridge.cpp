#include "core/HermesToVita49Bridge.h"
#include "core/HermesProtocol.h"
#include "core/LogManager.h"

#include <QDebug>
#include <QDateTime>
#include <QtEndian>
#include <QtMath>
#include <cmath>

namespace MasterSDR {

// ────────────────────────────────────────────────────────────────
//  Construction / Destruction
// ────────────────────────────────────────────────────────────────

HermesToVita49Bridge::HermesToVita49Bridge(QObject* parent)
    : QObject(parent)
    , m_vita49Addr(QHostAddress::LocalHost)
{
    m_socket = new QUdpSocket(this);
    connect(m_socket, &QUdpSocket::readyRead, this, &HermesToVita49Bridge::onReadyRead);

    m_watchdogTimer = new QTimer(this);
    m_watchdogTimer->setInterval(WATCHDOG_MS);
    connect(m_watchdogTimer, &QTimer::timeout, this, &HermesToVita49Bridge::onWatchdog);

    // Precompute Hanning window for FFT
    m_hanningWindow.resize(FFT_SIZE);
    for (int i = 0; i < FFT_SIZE; ++i) {
        m_hanningWindow[i] = 0.5f * (1.0f - std::cos(2.0 * M_PI * i / (FFT_SIZE - 1)));
    }

    m_fftInBuf.resize(FFT_SIZE * 2, 0.0f);  // Complex interleaved
    m_elapsed.start();
}

HermesToVita49Bridge::~HermesToVita49Bridge()
{
    disconnectFromRadio();
}

// ────────────────────────────────────────────────────────────────
//  HL2 Protocol builders
// ────────────────────────────────────────────────────────────────

QByteArray HermesToVita49Bridge::buildDiscoveryPacket()
{
    QByteArray pkt(DISCOVERY_SIZE, '\0');
    pkt[0] = '\xEF';
    pkt[1] = '\xFE';
    pkt[2] = static_cast<char>(PKT_DISCOVERY);
    return pkt;
}

QByteArray HermesToVita49Bridge::buildStartPacket(uint8_t flags)
{
    QByteArray pkt(CMD_FRAME_SIZE, '\0');
    pkt[0] = '\xEF';
    pkt[1] = '\xFE';
    pkt[2] = static_cast<char>(PKT_START);
    pkt[3] = static_cast<char>(flags);
    return pkt;
}

QByteArray HermesToVita49Bridge::buildCommandPacket(uint8_t addr, uint32_t data)
{
    QByteArray pkt(CMD_FRAME_SIZE, '\0');
    pkt[0] = '\xEF';
    pkt[1] = '\xFE';
    pkt[2] = static_cast<char>(PKT_COMMAND);
    pkt[3] = '\x7F';
    pkt[4] = static_cast<char>(addr << 1);
    qToBigEndian(data, reinterpret_cast<uchar*>(pkt.data()) + 5);
    return pkt;
}

uint32_t HermesToVita49Bridge::buildControlWord(uint8_t speed, uint8_t numRx, bool duplex, bool mox)
{
    uint32_t ctrl = 0;
    ctrl |= (static_cast<uint32_t>(speed) & 0x03) << 24;
    ctrl |= (static_cast<uint32_t>(numRx & 0x0F)) << 3;
    if (duplex) ctrl |= (1u << 2);
    if (mox)    ctrl |= 1;
    return ctrl;
}

// ────────────────────────────────────────────────────────────────
//  Connection lifecycle
// ────────────────────────────────────────────────────────────────

void HermesToVita49Bridge::connectToRadio(const QString& host, uint16_t port)
{
    m_host = QHostAddress(host);
    m_radioPort = port;
    m_connected = false;

    m_state = ConnectionState::Connecting;
    emit stateChanged(m_state);

    m_socket->close();
    if (!m_socket->bind(QHostAddress::AnyIPv4, 0)) {
        m_errorString = QStringLiteral("Failed to bind UDP socket");
        qCWarning(lcConnection) << m_errorString;
        m_state = ConnectionState::Error;
        emit stateChanged(m_state);
        emit errorOccurred(m_errorString);
        return;
    }

    qCDebug(lcConnection) << "HermesToVita49Bridge: bound port" << m_socket->localPort()
             << "connecting to" << host << ":" << port;

    // Connection sequence matching HermesConnection.cpp
    // 1. Stop twice (reset radio state)
    QByteArray stopPkt = buildStartPacket(0);
    m_socket->writeDatagram(stopPkt, m_host, m_radioPort);
    m_socket->flush();
    QThread::msleep(10);
    m_socket->writeDatagram(stopPkt, m_host, m_radioPort);
    m_socket->flush();
    QThread::msleep(10);

    // 2. Start radio + wideband (3 packets for reliability)
    QByteArray startPkt = buildStartPacket(START_RADIO | START_WIDEBAND | START_DISABLE_WATCHDOG);
    for (int i = 0; i < 3; ++i) {
        m_socket->writeDatagram(startPkt, m_host, m_radioPort);
        m_socket->flush();
        QThread::msleep(2);
    }

    // 3. Set control word: speed=48k, 1 receiver, no duplex, no MOX
    uint32_t ctrl = buildControlWord(SPEED_48K, 1, false, false);
    QByteArray ctrlPkt = buildCommandPacket(ADDR_CONTROL, ctrl);
    m_socket->writeDatagram(ctrlPkt, m_host, m_radioPort);

    // 4. Set initial frequency
    uint32_t ctrlPkt2 = buildControlWord(SPEED_48K, 1, false, false);
    QByteArray ctrlPkt2Bytes = buildCommandPacket(ADDR_CONTROL, ctrlPkt2);
    m_socket->writeDatagram(ctrlPkt2Bytes, m_host, m_radioPort);

    setFrequency(m_rxFreq);

    m_connected = true;
    m_state = ConnectionState::Connected;
    emit stateChanged(m_state);
    emit connected();
    emit radioInfoUpdated();

    m_watchdogTimer->start();

    qCDebug(lcConnection) << "HermesToVita49Bridge: connected to HL2 at" << host << ":" << port;
}

void HermesToVita49Bridge::disconnectFromRadio()
{
    m_watchdogTimer->stop();
    if (m_connected && m_socket->state() == QAbstractSocket::BoundState) {
        QByteArray stopPkt = buildStartPacket(0);
        m_socket->writeDatagram(stopPkt, m_host, m_radioPort);
        QThread::msleep(10);
        m_socket->writeDatagram(stopPkt, m_host, m_radioPort);
        m_socket->flush();
    }
    m_socket->close();
    m_connected = false;
    m_state = ConnectionState::Disconnected;
    emit stateChanged(m_state);
    emit disconnected();
}

void HermesToVita49Bridge::setVita49Target(uint16_t udpPort)
{
    m_vita49Port = udpPort;
}

// ────────────────────────────────────────────────────────────────
//  Application commands → HL2 registers
// ────────────────────────────────────────────────────────────────

void HermesToVita49Bridge::setFrequency(uint64_t freqHz)
{
    m_rxFreq = freqHz;
    if (!m_connected) return;

    uint32_t nco = static_cast<uint32_t>(freqHz);
    QByteArray pkt = buildCommandPacket(ADDR_RX1_NCO, nco);
    m_socket->writeDatagram(pkt, m_host, m_radioPort);

    emit frequencyUpdated(freqHz);
    emit radioInfoUpdated();
}

void HermesToVita49Bridge::setMode(const QString& mode)
{
    // HL2 is an SDR — mode is entirely software-defined
    // Store locally and emit for UI consistency
    m_rxMode = mode;
    emit modeUpdated(mode);
    emit radioInfoUpdated();
}

void HermesToVita49Bridge::setPtt(bool tx)
{
    m_ptt = tx;
    if (!m_connected) return;

    // Rebuild control word with new MOX bit
    uint32_t ctrl = buildControlWord(SPEED_48K, 1, false, tx);
    QByteArray pkt = buildCommandPacket(ADDR_CONTROL, ctrl);
    m_socket->writeDatagram(pkt, m_host, m_radioPort);

    emit pttStateChanged(tx);
    emit radioInfoUpdated();

    // Update meter
    if (m_vita49Enabled && m_vita49Port > 0) {
        synthesizeVita49Meters();
    }
}

void HermesToVita49Bridge::setTxDrive(uint8_t drive)
{
    if (!m_connected) return;
    QByteArray pkt = buildCommandPacket(ADDR_TX_DRIVE, static_cast<uint32_t>(drive));
    m_socket->writeDatagram(pkt, m_host, m_radioPort);
}

void HermesToVita49Bridge::setLnaGain(uint8_t gain, bool stepAttenuator)
{
    if (!m_connected) return;
    uint32_t val = gain & 0x3F;
    if (stepAttenuator) val |= (1u << 5);
    QByteArray pkt = buildCommandPacket(ADDR_LNA_GAIN, val);
    m_socket->writeDatagram(pkt, m_host, m_radioPort);
}

// ────────────────────────────────────────────────────────────────
//  Watchdog keepalive
// ────────────────────────────────────────────────────────────────

void HermesToVita49Bridge::onWatchdog()
{
    if (!m_connected) return;

    // Send discovery packet as keepalive heartbeat
    QByteArray pkt = buildDiscoveryPacket();
    m_socket->writeDatagram(pkt, m_host, m_radioPort);
    m_socket->flush();
}

// ────────────────────────────────────────────────────────────────
//  Packet reception
// ────────────────────────────────────────────────────────────────

void HermesToVita49Bridge::onReadyRead()
{
    while (m_socket->hasPendingDatagrams()) {
        QByteArray data;
        data.resize(static_cast<int>(m_socket->pendingDatagramSize()));
        QHostAddress sender;
        quint16 senderPort = 0;
        m_socket->readDatagram(data.data(), data.size(), &sender, &senderPort);

        if (data.size() < 3) continue;

        // Validate HL2 cookie
        if (static_cast<uint8_t>(data[0]) != 0xEF || static_cast<uint8_t>(data[1]) != 0xFE) {
            continue;
        }

        processDatagram(data);
    }
}

void HermesToVita49Bridge::processDatagram(const QByteArray& data)
{
    uint8_t pktType = static_cast<uint8_t>(data[2]);

    // Wideband IQ data: byte[3] == PKT_START (0x04), 1024 bytes
    if (data.size() == static_cast<int>(IQ_BUFFER_SIZE) && pktType == PKT_START) {
        processWidebandPacket(data);
        return;
    }

    // Status reply: 60 bytes, type 0x02 or 0x03
    if (data.size() == DISCOVERY_SIZE && (pktType == PKT_DISCOVERY || pktType == 0x03)) {
        processStatusReply(data);
        return;
    }
}

// ────────────────────────────────────────────────────────────────
//  Wideband IQ Processing → VITA-49 FFT + Audio
// ────────────────────────────────────────────────────────────────

void HermesToVita49Bridge::processWidebandPacket(const QByteArray& data)
{
    if (!m_vita49Enabled || m_vita49Port == 0) return;

    // HL2 wideband packet: 1024 bytes total
    // Bytes 0-2: EF FE 04 (cookie + type)
    // Byte 3: 0x04 (wideband sub-type)
    // Bytes 4-7: uint32 BE sequence counter
    // Bytes 8-1023: IQ samples (1016 bytes = 508 int16 IQ pairs)

    QByteArray iqPayload = data.mid(8, IQ_BUFFER_SIZE - 8);

    // Compute software FFT from IQ data
    computeSoftwareFft(iqPayload);

    // Extract audio magnitude for VITA-49 audio packets
    computeAudioExtract(iqPayload);
}

void HermesToVita49Bridge::computeSoftwareFft(const QByteArray& widebandPayload)
{
    if (widebandPayload.size() < 2) return;

    // Extract IQ samples: int16 I,Q pairs (signed, little-endian from ADC)
    // Fill m_fftInBuf with complex samples interleaved: real, imag, real, imag...
    int maxSamples = qMin(FFT_SIZE, widebandPayload.size() / 4);
    float maxMag = 0.0f;

    for (int i = 0; i < maxSamples; ++i) {
        int offset = i * 4;
        qint16 iVal = qFromLittleEndian<qint16>(reinterpret_cast<const uchar*>(widebandPayload.constData()) + offset);
        qint16 qVal = qFromLittleEndian<qint16>(reinterpret_cast<const uchar*>(widebandPayload.constData()) + offset + 2);

        float real = iVal / 32768.0f;
        float imag = qVal / 32768.0f;

        // Apply Hanning window
        float w = m_hanningWindow[i];
        m_fftInBuf[i * 2]     = real * w;
        m_fftInBuf[i * 2 + 1] = imag * w;

        float mag = std::sqrt(real * real + imag * imag);
        if (mag > maxMag) maxMag = mag;
    }

    // Simple DFT-based spectrum (not full FFT — uses magnitude bins)
    // Produce FFT bins as pixel Y positions for PanadapterStream
    QVector<float> bins;
    int numBins = qMin(m_panXPixels, FFT_SIZE / 2);
    bins.reserve(numBins);

    // NB: Real FFT requires a proper implementation. This is a simplified
    // magnitude-bin approach that produces usable spectrum data.
    // Each bin = magnitude sum of nearby frequency buckets.
    for (int bin = 0; bin < numBins; ++bin) {
        float accum = 0.0f;
        int startI = bin * FFT_SIZE / numBins;
        int endI = (bin + 1) * FFT_SIZE / numBins;
        for (int j = startI; j < endI; ++j) {
            accum += std::sqrt(m_fftInBuf[j * 2] * m_fftInBuf[j * 2] +
                               m_fftInBuf[j * 2 + 1] * m_fftInBuf[j * 2 + 1]);
        }
        bins.append(accum / (endI - startI));
    }

    // Update S-meter from max magnitude
    if (maxMag > 0.0f) {
        float dbFs = 20.0f * std::log10(maxMag);
        m_sMeter = static_cast<int>(qBound(-150.0f, dbFs + 127.0f, 20.0f) * 1.5f);
        emit sMeterUpdated(m_sMeter);
    }

    // Scale bins to Y pixel positions for VITA-49 FFT format
    m_fftFrameIndex++;
    m_fftSeq = (m_fftSeq + 1) & 0xF;

    QByteArray fftPkt = buildVita49FftPacket(bins, 0, static_cast<quint16>(numBins));
    sendVita49ToLoopback(fftPkt);
    emit vita49FftPacket(fftPkt);
}

void HermesToVita49Bridge::computeAudioExtract(const QByteArray& widebandPayload)
{
    // Extract audio from IQ data by computing magnitude and decimating
    // HL2 ADC rate: 76.8 MHz → after decimation chain → 48 kHz audio
    // For bridge, we compute magnitude envelope and pack as float32 stereo

    int maxSamples = qMin(504, widebandPayload.size() / 4);
    if (maxSamples < 1) return;

    QByteArray floatPayload;
    floatPayload.reserve(maxSamples * 8); // float32 stereo = 8 bytes per sample pair

    for (int i = 0; i < maxSamples; ++i) {
        int offset = i * 4;
        qint16 iVal = qFromLittleEndian<qint16>(reinterpret_cast<const uchar*>(widebandPayload.constData()) + offset);
        qint16 qVal = qFromLittleEndian<qint16>(reinterpret_cast<const uchar*>(widebandPayload.constData()) + offset + 2);

        float sample = std::sqrt(static_cast<float>(iVal * iVal + qVal * qVal)) / 32768.0f;

        uint32_t floatBits;
        memcpy(&floatBits, &sample, 4);
        uint32_t beFloat = qToBigEndian(floatBits);
        floatPayload.append(reinterpret_cast<const char*>(&beFloat), 4);
        floatPayload.append(reinterpret_cast<const char*>(&beFloat), 4); // mono → stereo
    }

    m_audioSeq = (m_audioSeq + 1) & 0xF;

    QByteArray hdr = buildVita49Header(PCC_IF_DATA, 0x04000001, floatPayload.size());
    hdr.append(floatPayload);

    sendVita49ToLoopback(hdr);
    emit vita49AudioPacket(hdr);
}

// ────────────────────────────────────────────────────────────────
//  Status reply processing → VITA-49 meters
// ────────────────────────────────────────────────────────────────

void HermesToVita49Bridge::processStatusReply(const QByteArray& data)
{
    if (data.size() < DISCOVERY_SIZE) return;

    auto reply = HermesProtocol::decodeDiscoveryReply(data, m_host.toString(), m_radioPort);

    // Update radio identity
    m_macAddress = reply.mac;
    m_gatewareVersion = reply.gateware;
    m_boardId = reply.boardId;
    m_numReceivers = reply.numReceivers;

    // Update telemetry
    m_temperature = reply.temperature;
    m_forwardPower = reply.forwardPower;
    m_reversePower = reply.reversePower;
    m_adcClipCount = reply.adcClipCount;
    m_cwKey = reply.extCwKey;
    m_ptt = reply.ptt;
    m_txOn = reply.txOn;
    m_paExtTr = reply.paExtTr;
    m_paIntTr = reply.paIntTr;

    // Emit signals
    emit temperatureUpdated(m_temperature);
    emit powerUpdated(m_forwardPower, m_reversePower);
    emit pttStateChanged(m_ptt);
    emit cwKeyChanged(m_cwKey);
    emit adcClipCountUpdated(m_adcClipCount);
    emit radioInfoUpdated();

    // Synthesize VITA-49 meter packet
    if (m_vita49Enabled && m_vita49Port > 0) {
        synthesizeVita49Meters();
    }
}

// ────────────────────────────────────────────────────────────────
//  VITA-49 Header construction (same format as CivToVita49Bridge)
// ────────────────────────────────────────────────────────────────

QByteArray HermesToVita49Bridge::buildVita49Header(quint16 pcc, quint32 streamId, quint32 payloadBytes)
{
    uint32_t totalWords = (VITA49_HEADER_BYTES + payloadBytes + 3) / 4;

    QByteArray hdr(VITA49_HEADER_BYTES, '\0');

    uint32_t w0 = (0x3u << 28)
                | (0x1u << 27)
                | (0x0u << 26)
                | (static_cast<uint32_t>(m_fftSeq & 0xF) << 16)
                | (totalWords & 0xFFFFu);
    qToBigEndian(w0, reinterpret_cast<uchar*>(hdr.data()));

    uint32_t sid = streamId;
    qToBigEndian(sid, reinterpret_cast<uchar*>(hdr.data()) + 4);

    uint32_t oui = FLEX_OUI;
    qToBigEndian(oui, reinterpret_cast<uchar*>(hdr.data()) + 8);

    uint32_t iccPcc = (FLEX_ICC << 16) | pcc;
    qToBigEndian(iccPcc, reinterpret_cast<uchar*>(hdr.data()) + 12);

    return hdr;
}

// ────────────────────────────────────────────────────────────────
//  VITA-49 FFT (PCC 0x8003)
// ────────────────────────────────────────────────────────────────

QByteArray HermesToVita49Bridge::buildVita49FftPacket(const QVector<float>& bins,
                                                        quint16 startBin, quint16 totalBins)
{
    // Sub-header: 12 bytes
    QByteArray subHdr(12, '\0');
    quint16 start = qToBigEndian(startBin);
    memcpy(subHdr.data(), &start, 2);
    quint16 count = qToBigEndian(static_cast<quint16>(bins.size()));
    memcpy(subHdr.data() + 2, &count, 2);
    quint32 total = qToBigEndian(totalBins);
    memcpy(subHdr.data() + 4, &total, 4);
    quint32 frmIdx = qToBigEndian(m_fftFrameIndex);
    memcpy(subHdr.data() + 8, &frmIdx, 4);

    // Bin payload: map magnitude to pixel Y position
    QByteArray binPayload;
    for (float v : bins) {
        // Normalize and convert to Y pixel position (0=top=max_dbm, max=bottom=min_dbm)
        float clamped = qBound(0.0f, v, 1.0f);
        quint16 pixelY = static_cast<quint16>((1.0f - clamped) * (m_panYPixels - 1));
        quint16 beVal = qToBigEndian(pixelY);
        binPayload.append(reinterpret_cast<const char*>(&beVal), 2);
    }

    uint32_t totalPayload = subHdr.size() + binPayload.size();
    QByteArray hdr = buildVita49Header(PCC_FFT, m_panStreamId, totalPayload);
    hdr.append(subHdr);
    hdr.append(binPayload);

    return hdr;
}

// ────────────────────────────────────────────────────────────────
//  VITA-49 Meter (PCC 0x8002)
// ────────────────────────────────────────────────────────────────

QByteArray HermesToVita49Bridge::buildVita49MeterPacket(const QVector<quint16>& ids,
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

void HermesToVita49Bridge::synthesizeVita49Meters()
{
    m_meterSeq = (m_meterSeq + 1) & 0xF;

    QVector<quint16> ids;
    QVector<qint16> vals;

    // Temperature: raw/64 = degF → convert to degC and scale
    // FlexRadio uses raw → (raw / 64.0) for temperature in degF
    float tempF = m_temperature * 9.0f / 5.0f + 32.0f;
    ids.append(HermesMeterIndex::TEMPERATURE);
    vals.append(static_cast<qint16>(tempF * 64));

    // Forward power: raw uint16 value (needs calibration for watts)
    ids.append(HermesMeterIndex::FWD_POWER);
    vals.append(static_cast<qint16>(m_forwardPower));

    // Reverse power
    ids.append(HermesMeterIndex::REV_POWER);
    vals.append(static_cast<qint16>(m_reversePower));

    // PTT state
    ids.append(HermesMeterIndex::PTT_STATE);
    vals.append(static_cast<qint16>(m_ptt ? 128 : 0));

    // TX on
    ids.append(HermesMeterIndex::TX_ON);
    vals.append(static_cast<qint16>(m_txOn ? 128 : 0));

    // ADC clip count
    ids.append(HermesMeterIndex::ADC_CLIP);
    vals.append(static_cast<qint16>(m_adcClipCount * 128 / 3));

    // CW key
    ids.append(HermesMeterIndex::CW_KEY);
    vals.append(static_cast<qint16>(m_cwKey ? 128 : 0));

    // S-meter synthesized from IQ magnitude
    float sDbm = -127.0f + (m_sMeter / 12.0f) * 6.0f;
    ids.append(HermesMeterIndex::SLICE0_LEVEL);
    vals.append(static_cast<qint16>(sDbm * 128.0f));

    QByteArray pkt = buildVita49MeterPacket(ids, vals);
    sendVita49ToLoopback(pkt);
    emit vita49MeterPacket(pkt);
}

// ────────────────────────────────────────────────────────────────
//  VITA-49 delivery
// ────────────────────────────────────────────────────────────────

void HermesToVita49Bridge::sendVita49ToLoopback(const QByteArray& pkt)
{
    if (m_vita49Port == 0 || !m_vita49Enabled) return;
    m_socket->writeDatagram(pkt, m_vita49Addr, m_vita49Port);
}

void HermesToVita49Bridge::setVita49Enabled(bool on)
{
    m_vita49Enabled = on;
}

void HermesToVita49Bridge::setPanPixelCount(int xPixels, int yPixels)
{
    m_panXPixels = xPixels;
    m_panYPixels = yPixels;
}

} // namespace MasterSDR
