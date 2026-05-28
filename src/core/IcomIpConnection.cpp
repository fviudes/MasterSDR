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
    QByteArray pkt = buildPacketFor(typeCode, seq, m_ctrlPort, m_destId, payload);
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
    // source_id from IP (like wfview/node-red-icom)
    quint32 ip = QHostAddress(QHostAddress::LocalHost).toIPv4Address();
    // Actually use the real IP - but node-red uses 0 initially
    m_sourceId = static_cast<uint16_t>((ip >> 8) & 0xFFFF);

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
                // Register audio stream on port 50003 — send init packet
                if (m_audioSocket) {
                    uint16_t audioSeq = m_seq++;
                    QByteArray audioPkt = buildPacketFor(TYPE_DATA, audioSeq, m_audioPort, m_destId);
                    m_audioSocket->writeDatagram(audioPkt, m_host, m_audioPort);
                    qCDebug(lcConnection) << "IcomIpConnection: audio registration sent to" << m_host.toString() << ":" << m_audioPort;
                }
            }
            break;

        case TYPE_PING: // 0x07
            // Respond with ping
            sendCtrlPacket(TYPE_PING, m_pingSeq++);
            break;

        default:
            break;
        }
    } else if (type == TYPE_DATA) {
        // Data channel — could be serial (50002) or audio (50003)
        // CI-V frames start with FE FE preamble — try parsing regardless of senderPort
        QByteArray civPayload = data.mid(16);
        if (civPayload.size() >= 5
            && static_cast<uint8_t>(civPayload[0]) == IcomCivProtocol::PREAMBLE1
            && static_cast<uint8_t>(civPayload[1]) == IcomCivProtocol::PREAMBLE2) {
            qCDebug(lcConnection) << "IcomIpConnection: CI-V data from port" << senderPort << ":" << civPayload.toHex().left(30);

            CivResponse resp = m_civProto.parseResponse(civPayload);
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
        default:
            break;
        }
    }
}

void IcomIpConnection::onKeepAlive()
{
    if (!m_connected) return;
    sendCtrlPacket(TYPE_PING, m_pingSeq++);

    // Poll radio state using CI-V commands with proper sub-commands
    sendCivCommand(IcomCivProtocol::CMD_READ_VFO, 0);      // VFO frequency (0x03)
    sendCivCommand(IcomCivProtocol::CMD_MODE, IcomCivProtocol::SUB_MODE_READ); // Mode (0x06 0x04)
    sendCivCommand(IcomCivProtocol::CMD_S_METER, IcomCivProtocol::SUB_SMETER); // S-meter (0x15 0x02)
    sendCivCommand(IcomCivProtocol::CMD_S_METER, IcomCivProtocol::SUB_SQUELCH); // Squelch (0x15 0x01)
}

void IcomIpConnection::sendSerialPacket(uint16_t seq, const QByteArray& civFrame)
{
    QByteArray pkt = buildPacketFor(TYPE_DATA, seq, m_serialPort, m_destId, civFrame);
    m_socket->writeDatagram(pkt, m_host, m_serialPort);
}

void IcomIpConnection::sendCivCommand(uint8_t cmd, uint8_t subCmd, const QByteArray& data)
{
    QByteArray civFrame = m_civProto.buildCommand(cmd, subCmd, data);
    sendSerialPacket(m_seq++, civFrame);
    qCDebug(lcConnection) << "IcomIpConnection: sent CI-V cmd:" << Qt::hex << cmd;
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
            emit audioDataReady(pcm);
        }
    }
}

} // namespace MasterSDR
