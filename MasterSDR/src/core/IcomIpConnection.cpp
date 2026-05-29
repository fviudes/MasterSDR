#include "core/IcomIpConnection.h"
#include "core/LogManager.h"

#include <QDebug>
#include <QDateTime>
#include <QtEndian>
#include <QThread>

namespace MasterSDR {

IcomIpConnection::IcomIpConnection(QObject* parent)
    : ISourceBackend(parent)
    , m_civProto(IcomCivProtocol::DEFAULT_CI_V_ADDR)
{
    m_socket = new QUdpSocket(this);
    connect(m_socket, &QUdpSocket::readyRead, this, &IcomIpConnection::onReadyRead);

    m_audioSocket = new QUdpSocket(this);
    connect(m_audioSocket, &QUdpSocket::readyRead, this, &IcomIpConnection::onAudioReady);

    m_keepAliveTimer = new QTimer(this);
    m_keepAliveTimer->setInterval(KEEPALIVE_MS);
    connect(m_keepAliveTimer, &QTimer::timeout, this, &IcomIpConnection::onKeepAlive);
}

IcomIpConnection::~IcomIpConnection()
{
    disconnectFromRadio();
}

QByteArray IcomIpConnection::buildPacket(uint16_t typeCode, uint16_t seq,
                                          const QByteArray& payload)
{
    uint32_t totalLen = 16 + static_cast<uint32_t>(payload.size());
    QByteArray pkt;
    // length (uint32 LE)
    pkt.append(reinterpret_cast<const char*>(&totalLen), 4);
    // type_code (uint16 LE)
    pkt.append(reinterpret_cast<const char*>(&typeCode), 2);
    // sequence (uint16 LE)
    pkt.append(reinterpret_cast<const char*>(&seq), 2);
    // source_port (uint16 LE)
    pkt.append(reinterpret_cast<const char*>(&m_sourcePort), 2);
    // source_id (uint16 LE)
    pkt.append(reinterpret_cast<const char*>(&m_sourceId), 2);
    // destination_port (uint16 LE) — uses m_destPort set from SYN-ACK
    pkt.append(reinterpret_cast<const char*>(&m_destPort), 2);
    // destination_id (uint16 LE) — uses m_destId set from SYN-ACK
    pkt.append(reinterpret_cast<const char*>(&m_destId), 2);
    // payload
    if (!payload.isEmpty()) pkt.append(payload);
    return pkt;
}

QByteArray IcomIpConnection::buildPacketFor(uint16_t typeCode, uint16_t seq,
                                             uint16_t dstPort, uint16_t dstId,
                                             const QByteArray& payload)
{
    uint32_t totalLen = 16 + static_cast<uint32_t>(payload.size());
    QByteArray pkt;
    pkt.append(reinterpret_cast<const char*>(&totalLen), 4);
    pkt.append(reinterpret_cast<const char*>(&typeCode), 2);
    pkt.append(reinterpret_cast<const char*>(&seq), 2);
    pkt.append(reinterpret_cast<const char*>(&m_sourcePort), 2);
    pkt.append(reinterpret_cast<const char*>(&m_sourceId), 2);
    pkt.append(reinterpret_cast<const char*>(&dstPort), 2);
    pkt.append(reinterpret_cast<const char*>(&dstId), 2);
    if (!payload.isEmpty()) pkt.append(payload);
    return pkt;
}

void IcomIpConnection::sendCtrlPacket(uint16_t typeCode, uint16_t seq,
                                       const QByteArray& payload)
{
    // Use m_destPort from SYN-ACK header (radio's actual ctrl channel port)
    // NOT m_ctrlPort parameter — radio may use different port in header vs UDP
    QByteArray pkt = buildPacketFor(typeCode, seq, m_destPort, m_destId, payload);
    m_socket->writeDatagram(pkt, m_host, m_ctrlPort);
}

void IcomIpConnection::connectToRadio(const QString& host, uint16_t ctrlPort,
                                       uint16_t serialPort, uint16_t audioPort,
                                       const QString& username, const QString& password)
{
    m_host = QHostAddress(host);
    m_ctrlPort = ctrlPort;
    m_serialPort = serialPort;
    m_audioPort = audioPort;
    m_username = username;
    m_password = password;
    m_connected = false;
    m_seq = 0;

    m_state.store(State::Connecting);
    emit stateChanged(State::Connecting);

    m_socket->close();
    m_socket->bind();

    m_audioSocket->close();
    m_audioSocket->bind();

    m_sourcePort = static_cast<uint16_t>(m_socket->localPort());
    m_sourceId = 0;  // Client ID — node-red/wfview use 0 initially

    qCDebug(lcConnection) << "IcomIpConnection: connecting to" << host
             << "ctrl:" << ctrlPort
             << "sourcePort:" << m_sourcePort << "sourceId:" << Qt::hex << m_sourceId;

    // Send SYN (type 0x03, seq 0)
    sendCtrlPacket(TYPE_SYN, 0);
    qCDebug(lcConnection) << "IcomIpConnection: SYN sent";
}

void IcomIpConnection::disconnectFromRadio()
{
    m_keepAliveTimer->stop();
    m_connected = false;
    // Send DISCON before closing sockets
    sendCtrlPacket(TYPE_DISCON, m_seq++);
    m_socket->close();
    m_audioSocket->close();
    m_state.store(State::Disconnected);
    emit stateChanged(State::Disconnected);
    emit disconnected();
}

void IcomIpConnection::onReadyRead()
{
    while (m_socket->hasPendingDatagrams()) {
        QByteArray data;
        data.resize(static_cast<int>(m_socket->pendingDatagramSize()));
        QHostAddress sender;
        quint16 senderPort = 0;
        m_socket->readDatagram(data.data(), data.size(), &sender, &senderPort);

        // Raw CI-V frames from port 50002 (no 16-byte header)
        if (data.size() >= 6 && data.size() < 60
            && static_cast<uint8_t>(data[0]) == IcomCivProtocol::PREAMBLE1
            && static_cast<uint8_t>(data[1]) == IcomCivProtocol::PREAMBLE2) {
            qCDebug(lcConnection) << "IcomIpConnection: raw CI-V from port" << senderPort << data.toHex().constData();
            CivResponse resp = m_civProto.parseResponse(data);
            if (resp.valid) {
                handleCivResponse(resp);
            }
            continue;
        }

        if (data.size() < 16) continue;

        uint32_t len    = qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(data.constData()));
        uint16_t type   = qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(data.constData()) + 4);
        uint16_t seq    = qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(data.constData()) + 6);
        uint16_t srcPort = qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(data.constData()) + 8);
        uint16_t srcId  = qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(data.constData()) + 10);

        qCDebug(lcConnection) << "IcomIpConnection: recv type:" << Qt::hex << type
                 << "seq:" << seq << "from:" << sender.toString() << ":" << senderPort;

        processPacket(data, senderPort);
    }
}

void IcomIpConnection::handleCivResponse(const CivResponse& resp)
{
    switch (resp.cmd) {
    case IcomCivProtocol::CMD_FREQ:
    case IcomCivProtocol::CMD_READ_VFO: {
        uint64_t freq = IcomCivProtocol::decodeBcdFreq(resp.data);
        m_rxFreq = freq;
        qCDebug(lcConnection) << "IcomIpConnection: freq" << freq << "Hz";
        emit frequencyUpdated(freq);
        break;
    }
    case IcomCivProtocol::CMD_MODE: {
        if (!resp.data.isEmpty()) {
            auto modeStr = IcomCivProtocol::modeToString(static_cast<IcomCivProtocol::CivMode>(resp.data[0]));
            if (!modeStr.isEmpty()) {
                m_rxMode = modeStr;
                emit modeUpdated(modeStr);
            }
        }
        break;
    }
    case IcomCivProtocol::CMD_S_METER: {
        if (!resp.data.isEmpty()) {
            int level = static_cast<int>(static_cast<uint8_t>(resp.data[0]));
            m_sMeter = level;
            emit sMeterUpdated(level);
            if (resp.subCmd == IcomCivProtocol::SUB_SQUELCH) {
                emit squelchStatusUpdated(resp.data[0] != 0x00);
            }
        }
        break;
    }
    case IcomCivProtocol::CMD_RIG_ID: {
        if (!resp.data.isEmpty()) {
            uint8_t rigId = static_cast<uint8_t>(resp.data[0]);
            QString model = IcomCivProtocol::rigIdToModel(rigId);
            m_radioModel = model;
            qCDebug(lcConnection) << "IcomIpConnection: detected model" << model << "from rig ID" << Qt::hex << rigId;
            emit radioInfoUpdated();
        }
        break;
    }
    case IcomCivProtocol::CMD_SPLIT: {
        if (!resp.data.isEmpty()) {
            m_split = (resp.data[0] != 0x00);
            emit splitUpdated(m_split);
        }
        break;
    }
    case IcomCivProtocol::CMD_ATTENUATOR: {
        if (!resp.data.isEmpty()) {
            m_attenuator = (resp.data[0] != 0x00);
            emit attenuatorUpdated(m_attenuator);
        }
        break;
    }
    case IcomCivProtocol::CMD_MIC_GAIN: {
        if (!resp.data.isEmpty() && resp.subCmd == IcomCivProtocol::SUB_TX_POWER) {
            m_txPower = static_cast<int>(resp.data[0]) * 100 / 255;
            emit txPowerUpdated(m_txPower);
        } else if (!resp.data.isEmpty() && resp.subCmd == IcomCivProtocol::SUB_RF_GAIN) {
            m_rfGain = static_cast<int>(resp.data[0]) * 100 / 255;
            emit rfGainUpdated(m_rfGain);
        }
        break;
    }
    case IcomCivProtocol::CMD_PREAMP: {
        if (!resp.data.isEmpty()) {
            if (resp.subCmd == IcomCivProtocol::SUB_PREAMP) {
                m_preamp = static_cast<int>(resp.data[0]);
                emit preampUpdated(m_preamp);
            } else if (resp.subCmd == IcomCivProtocol::SUB_BKIN) {
                m_bkInMode = static_cast<int>(resp.data[0]);
                emit bkInUpdated(m_bkInMode);
            } else if (resp.subCmd == IcomCivProtocol::SUB_APF) {
                m_apfMode = static_cast<int>(resp.data[0]);
                emit apfUpdated(m_apfMode);
            }
        }
        break;
    }
    case IcomCivProtocol::CMD_SPECTRUM: {
        qCDebug(lcConnection) << "IcomIpConnection: CI-V scope response" << resp.data.size() << "bytes";
        break;
    }
    default:
        break;
    }
}

void IcomIpConnection::processPacket(const QByteArray& data, quint16 senderPort)
{
    if (data.size() < 16) return;

    uint16_t type = qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(data.constData()) + 4);
    uint16_t seq  = qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(data.constData()) + 6);
    uint16_t srcPort = qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(data.constData()) + 8);
    uint16_t srcId  = qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(data.constData()) + 10);

    if (senderPort == m_ctrlPort) {
        // Control channel messages
        switch (type) {
        case TYPE_SYN_ACK: // 0x04
            m_destPort = srcPort;
            m_destId = srcId;
            qCDebug(lcConnection) << "IcomIpConnection: SYN-ACK, sending READY";
            // Send READY
            sendCtrlPacket(TYPE_READY, m_seq++);
            break;

        case TYPE_READY: // 0x06
            qCDebug(lcConnection) << "IcomIpConnection: READY received, connection established";
            if (!m_connected) {
                m_connected = true;
                m_state.store(State::Connected);
                emit stateChanged(State::Connected);
                emit connected();
                m_keepAliveTimer->start();

                // Start CI-V polling on serial port with proper sub-commands
                sendCivCommand(IcomCivProtocol::CMD_READ_VFO, 0);
                sendCivCommand(IcomCivProtocol::CMD_MODE, IcomCivProtocol::SUB_MODE_READ);
                sendCivCommand(IcomCivProtocol::CMD_S_METER, IcomCivProtocol::SUB_SMETER);
                // Poll squelch status
                sendCivCommand(IcomCivProtocol::CMD_S_METER, IcomCivProtocol::SUB_SQUELCH);
                // Query rig identity for auto-detection
                QByteArray rigIdCmd = m_civProto.buildReadRigId();
                sendSerialPacket(m_seq++, rigIdCmd);
                // Register audio/scope stream on port 50003
                // Register serial stream on port 50002 — send init packet
                {
                    uint16_t serialSeq = m_seq++;
                    QByteArray serialPkt = buildPacketFor(TYPE_DATA, serialSeq, m_serialPort, m_destId);
                    m_socket->writeDatagram(serialPkt, m_host, m_serialPort);
                    qCDebug(lcConnection) << "IcomIpConnection: serial registration sent to" << m_host.toString() << ":" << m_serialPort;
                }
                // Register audio/scope stream on port 50003 — send init + are-you-ready
                if (m_audioSocket) {
                    uint16_t audioSeq = m_seq++;
                    QByteArray audioPkt = buildPacketFor(TYPE_DATA, audioSeq, m_audioPort, m_destId);
                    m_audioSocket->writeDatagram(audioPkt, m_host, m_audioPort);
                    qCDebug(lcConnection) << "IcomIpConnection: audio are-you-ready sent to" << m_host.toString() << ":" << m_audioPort;
                }
            }
            break;

        case TYPE_PING: // 0x07
            sendCtrlPacket(TYPE_PING, m_pingSeq++);
            break;

        default:
            break;
        }
    }

    // CI-V payload detection — works from any senderPort (ctrl or serial)
    if (type == TYPE_DATA) {
        QByteArray civPayload;
        bool hasCivPayload = data.size() > 16;
        if (hasCivPayload) civPayload = data.mid(16);

        qCDebug(lcConnection) << "IcomIpConnection: TYPE_DATA" << data.size() << "bytes from port" << senderPort
                 << "payloadSize:" << (hasCivPayload ? civPayload.size() : 0)
                 << "payload:" << (hasCivPayload ? civPayload.left(30).toHex().constData() : "(empty)");

        if (!hasCivPayload) return;

        bool hasCivPreamble = (civPayload.size() >= 5
            && static_cast<uint8_t>(civPayload[0]) == IcomCivProtocol::PREAMBLE1
            && static_cast<uint8_t>(civPayload[1]) == IcomCivProtocol::PREAMBLE2);

        // Some Icom models strip FE FE preamble in IP responses
        bool hasRawCiv = (civPayload.size() >= 4
            && (static_cast<uint8_t>(civPayload[0]) == IcomCivProtocol::HOST_ADDR
                || static_cast<uint8_t>(civPayload[0]) == m_civProto.civAddress()));

        if (hasCivPreamble || hasRawCiv) {
            // If no preamble, rebuild it for parseResponse
            QByteArray fullPayload = civPayload;
            if (!hasCivPreamble && hasRawCiv) {
                fullPayload.prepend(static_cast<char>(IcomCivProtocol::PREAMBLE2));
                fullPayload.prepend(static_cast<char>(IcomCivProtocol::PREAMBLE1));
            }
            qCDebug(lcConnection) << "IcomIpConnection: CI-V data from port" << senderPort << ":" << civPayload.toHex().left(30);

            CivResponse resp = m_civProto.parseResponse(fullPayload);
            if (!resp.valid) return;

        switch (resp.cmd) {
        case IcomCivProtocol::CMD_FREQ:
        case IcomCivProtocol::CMD_READ_VFO: {
            uint64_t freq = IcomCivProtocol::decodeBcdFreq(resp.data);
            m_rxFreq = freq;
            qCDebug(lcConnection) << "IcomIpConnection: freq" << freq << "Hz";
            emit frequencyUpdated(freq);
            break;
        }
        case IcomCivProtocol::CMD_MODE: {
            if (!resp.data.isEmpty()) {
                auto modeStr = IcomCivProtocol::modeToString(static_cast<IcomCivProtocol::CivMode>(resp.data[0]));
                if (!modeStr.isEmpty()) {
                    m_rxMode = modeStr;
                    emit modeUpdated(modeStr);
                }
            }
            break;
        }
        case IcomCivProtocol::CMD_S_METER: {
            if (!resp.data.isEmpty()) {
                int level = static_cast<int>(static_cast<uint8_t>(resp.data[0]));
                m_sMeter = level;
                emit sMeterUpdated(level);
                // Handle squelch status (sub=0x01)
                if (resp.subCmd == IcomCivProtocol::SUB_SQUELCH) {
                    bool squelchOpen = (resp.data[0] != 0x00);
                    emit squelchStatusUpdated(squelchOpen);
                }
            }
            break;
        }
        case IcomCivProtocol::CMD_RIG_ID: {
            if (!resp.data.isEmpty()) {
                uint8_t rigId = static_cast<uint8_t>(resp.data[0]);
                QString model = IcomCivProtocol::rigIdToModel(rigId);
                m_radioModel = model;
                qCDebug(lcConnection) << "IcomIpConnection: detected model" << model << "from rig ID" << Qt::hex << rigId;
                emit radioInfoUpdated();
            }
            break;
        }
        case IcomCivProtocol::CMD_SPLIT: {
            if (!resp.data.isEmpty()) {
                m_split = (resp.data[0] != 0x00);
                emit splitUpdated(m_split);
            }
            break;
        }
        case IcomCivProtocol::CMD_ATTENUATOR: {
            if (!resp.data.isEmpty()) {
                m_attenuator = (resp.data[0] != 0x00);
                emit attenuatorUpdated(m_attenuator);
            }
            break;
        }
        case IcomCivProtocol::CMD_MIC_GAIN: {
            if (!resp.data.isEmpty() && resp.subCmd == IcomCivProtocol::SUB_TX_POWER) {
                m_txPower = static_cast<int>(resp.data[0]) * 100 / 255;
                emit txPowerUpdated(m_txPower);
            } else if (!resp.data.isEmpty() && resp.subCmd == IcomCivProtocol::SUB_RF_GAIN) {
                m_rfGain = static_cast<int>(resp.data[0]) * 100 / 255;
                emit rfGainUpdated(m_rfGain);
            }
            break;
        }
        case IcomCivProtocol::CMD_PREAMP: {
            if (!resp.data.isEmpty()) {
                if (resp.subCmd == IcomCivProtocol::SUB_PREAMP) {
                    m_preamp = static_cast<int>(resp.data[0]);
                    emit preampUpdated(m_preamp);
                } else if (resp.subCmd == IcomCivProtocol::SUB_BKIN) {
                    m_bkInMode = static_cast<int>(resp.data[0]);
                    emit bkInUpdated(m_bkInMode);
                } else if (resp.subCmd == IcomCivProtocol::SUB_APF) {
                    m_apfMode = static_cast<int>(resp.data[0]);
                    emit apfUpdated(m_apfMode);
                }
            }
            break;
        }
        case IcomCivProtocol::CMD_SPECTRUM: {
            // Scope data streams on port 50003, not CI-V. Log for debug.
            qCDebug(lcConnection) << "IcomIpConnection: CI-V scope response" << resp.data.size() << "bytes";
            break;
        }
        default:
            break;
        }
    }
    }
}

void IcomIpConnection::onKeepAlive()
{
    if (!m_connected) return;
    // PING on ctrl port — required for connection liveness
    sendCtrlPacket(TYPE_PING, m_pingSeq++);
    // Idle packet on serial port — keeps serial channel alive
    {
        QByteArray idlePkt = buildPacketFor(TYPE_DATA, m_pingSeq++, m_serialPort, m_destId);
        m_socket->writeDatagram(idlePkt, m_host, m_serialPort);
    }
    // Idle packet on audio port — keeps audio channel alive
    if (m_audioSocket) {
        QByteArray idlePkt = buildPacketFor(TYPE_DATA, m_pingSeq++, m_audioPort, m_destId);
        m_audioSocket->writeDatagram(idlePkt, m_host, m_audioPort);
    }

    // Poll radio state using CI-V commands with proper sub-commands
    sendCivCommand(IcomCivProtocol::CMD_READ_VFO, 0);      // VFO frequency (0x03)
    sendCivCommand(IcomCivProtocol::CMD_MODE, IcomCivProtocol::SUB_MODE_READ); // Mode (0x06 0x04)
    sendCivCommand(IcomCivProtocol::CMD_S_METER, IcomCivProtocol::SUB_SMETER); // S-meter (0x15 0x02)
    sendCivCommand(IcomCivProtocol::CMD_S_METER, IcomCivProtocol::SUB_SQUELCH); // Squelch (0x15 0x01)
    // Extended commands (IC-7610/IC-9700 compatible)
    sendCivCommand(IcomCivProtocol::CMD_MIC_GAIN, IcomCivProtocol::SUB_TX_POWER); // TX Power (0x14 0x0A)
    sendCivCommand(IcomCivProtocol::CMD_MIC_GAIN, IcomCivProtocol::SUB_RF_GAIN);  // RF Gain (0x14 0x02)
    sendCivCommand(IcomCivProtocol::CMD_SPLIT, 0);           // Split (0x0F)
    sendCivCommand(IcomCivProtocol::CMD_PREAMP, IcomCivProtocol::SUB_PREAMP);      // Preamp (0x16 0x02)
    sendCivCommand(IcomCivProtocol::CMD_ATTENUATOR, 0);      // Attenuator (0x11)
    // NOTE: Spectrum scope data streams on port 50003 (audio), not via CI-V 0x27
}

void IcomIpConnection::sendSerialPacket(uint16_t seq, const QByteArray& civFrame)
{
    // Radio only responds from port 50001 (Ctrl). Send CI-V there too.
    QByteArray pkt = buildPacketFor(TYPE_DATA, seq, m_ctrlPort, m_destId, civFrame);
    m_socket->writeDatagram(pkt, m_host, m_ctrlPort);
}

void IcomIpConnection::sendCivCommand(uint8_t cmd, uint8_t subCmd, const QByteArray& data)
{
    QByteArray civFrame = m_civProto.buildCommand(cmd, subCmd, data);
    sendSerialPacket(m_seq++, civFrame);
    qCDebug(lcConnection) << "IcomIpConnection: CI-V TX cmd:" << Qt::hex << cmd
             << "sub:" << subCmd << "frame:" << civFrame.toHex().constData();
}

void IcomIpConnection::setFrequency(uint64_t freqHz)
{
    m_rxFreq = freqHz;
    sendCivCommand(IcomCivProtocol::CMD_FREQ, 0,
                    IcomCivProtocol::encodeBcdFreq(freqHz));
    emit radioInfoUpdated();
}

void IcomIpConnection::setMode(const QString& mode)
{
    m_rxMode = mode;
    auto civMode = IcomCivProtocol::modeFromString(mode);
    QByteArray data;
    data.append(static_cast<char>(civMode));
    data.append('\x00');
    sendCivCommand(IcomCivProtocol::CMD_MODE, 0, data);
    emit radioInfoUpdated();
}

void IcomIpConnection::setPtt(bool tx)
{
    m_ptt = tx;
    QByteArray data;
    data.append(tx ? '\x01' : '\x00');
    sendCivCommand(IcomCivProtocol::CMD_PTT, 0, data);
    emit radioInfoUpdated();
}

void IcomIpConnection::onAudioReady()
{
    while (m_audioSocket->hasPendingDatagrams()) {
        QByteArray data;
        data.resize(static_cast<int>(m_audioSocket->pendingDatagramSize()));
        m_audioSocket->readDatagram(data.data(), data.size());
        if (data.size() > 16) {
            QByteArray pcm = data.mid(16);
            // IC-705 sends both PCM audio (~160 bytes) and scope data (~475 bytes)
            // on port 50003. Scope data is used for panadapter.
            if (pcm.size() >= 400) {
                qCDebug(lcConnection) << "IcomIpConnection: audio scope data" << pcm.size() << "bytes";
                emit spectrumDataReady(pcm);
            } else {
                qCDebug(lcConnection) << "IcomIpConnection: audio PCM" << pcm.size() << "bytes";
                emit audioDataReady(pcm);
            }
        }
    }
}

} // namespace MasterSDR
