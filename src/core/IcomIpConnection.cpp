#include "core/IcomIpConnection.h"
#include "core/LogManager.h"

#include <QDebug>
#include <QDateTime>
#include <QtEndian>
#include <QThread>

namespace MasterSDR {

IcomIpConnection::IcomIpConnection(QObject* parent)
    : QObject(parent)
    , m_civProto(IcomCivProtocol::DEFAULT_CI_V_ADDR)
{
    m_socket = new QUdpSocket(this);
    connect(m_socket, &QUdpSocket::readyRead, this, &IcomIpConnection::onReadyRead);

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
    // destination_port (uint16 LE)
    pkt.append(reinterpret_cast<const char*>(&m_destPort), 2);
    // destination_id (uint16 LE)
    pkt.append(reinterpret_cast<const char*>(&m_destId), 2);
    // payload
    if (!payload.isEmpty()) pkt.append(payload);
    return pkt;
}

void IcomIpConnection::sendCtrlPacket(uint16_t typeCode, uint16_t seq,
                                       const QByteArray& payload)
{
    QByteArray pkt = buildPacket(typeCode, seq, payload);
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
    if (m_connected) {
        sendCtrlPacket(TYPE_DISCON, m_seq++);
    }
    m_socket->close();
    m_state.store(State::Disconnected);
    m_connected = false;
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

                // Start CI-V polling on serial port
                sendCivCommand(IcomCivProtocol::CMD_FREQ, 0);
                sendCivCommand(IcomCivProtocol::CMD_MODE, 0);
            }
            break;

        case TYPE_PING: // 0x07
            // Respond with ping
            sendCtrlPacket(TYPE_PING, m_pingSeq++);
            break;

        default:
            break;
        }
    } else if (senderPort == m_serialPort && type == TYPE_DATA) {
        // Serial channel - CI-V response
        QByteArray civPayload = data.mid(16);
        if (civPayload.isEmpty()) return;

        qCDebug(lcConnection) << "IcomIpConnection: CI-V data:" << civPayload.toHex().left(30);

        CivResponse resp = m_civProto.parseResponse(civPayload);
        if (!resp.valid) return;

        switch (resp.cmd) {
        case IcomCivProtocol::CMD_FREQ: {
            uint64_t freq = IcomCivProtocol::decodeBcdFreq(resp.data);
            qCDebug(lcConnection) << "IcomIpConnection: freq" << freq << "Hz";
            emit frequencyUpdated(freq);
            break;
        }
        case IcomCivProtocol::CMD_MODE: {
            if (!resp.data.isEmpty()) {
                auto mode = static_cast<IcomCivProtocol::CivMode>(static_cast<uint8_t>(resp.data[0]));
                emit modeUpdated(IcomCivProtocol::modeToString(mode));
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
    sendCivCommand(IcomCivProtocol::CMD_FREQ, 0);
    sendCivCommand(IcomCivProtocol::CMD_MODE, 0);
}

void IcomIpConnection::sendSerialPacket(uint16_t seq, const QByteArray& civFrame)
{
    QByteArray pkt = buildPacket(TYPE_DATA, seq, civFrame);
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
    sendCivCommand(IcomCivProtocol::CMD_FREQ, 0,
                   IcomCivProtocol::encodeBcdFreq(freqHz));
}

void IcomIpConnection::setMode(const QString& mode)
{
    auto civMode = IcomCivProtocol::modeFromString(mode);
    QByteArray data;
    data.append(static_cast<char>(civMode));
    data.append('\x00');
    sendCivCommand(IcomCivProtocol::CMD_MODE, 0, data);
}

void IcomIpConnection::setPtt(bool tx)
{
    QByteArray data;
    data.append(tx ? '\x01' : '\x00');
    sendCivCommand(IcomCivProtocol::CMD_PTT, 0, data);
}

} // namespace MasterSDR
