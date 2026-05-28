#include "core/IcomIpConnection.h"

#include <QDebug>
#include <QDateTime>
#include <QtEndian>
#include <QNetworkInterface>
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

    m_authRetryTimer = new QTimer(this);
    m_authRetryTimer->setSingleShot(true);
    m_authRetryTimer->setInterval(3000);
    connect(m_authRetryTimer, &QTimer::timeout, this, &IcomIpConnection::retryAuth);
}

IcomIpConnection::~IcomIpConnection()
{
    disconnectFromRadio();
}

static uint32_t makeId(const QHostAddress& addr, quint16 port)
{
    quint32 ip = addr.toIPv4Address();
    return ((ip >> 16) & 0xFFFF) << 16 | (port & 0xFFFF);
}

static QByteArray buildPacket(uint16_t type, uint16_t seq,
                               uint32_t senderId, uint32_t recvId,
                               const QByteArray& payload = QByteArray())
{
    QByteArray pkt;
    // Total length (16 header + payload)
    uint32_t totalLen = 16 + static_cast<uint32_t>(payload.size());
    pkt.append(reinterpret_cast<const char*>(&totalLen), 4);
    // Type (2 bytes) + seq (2 bytes)
    pkt.append(reinterpret_cast<const char*>(&type), 2);
    pkt.append(reinterpret_cast<const char*>(&seq), 2);
    // Sender ID
    pkt.append(reinterpret_cast<const char*>(&senderId), 4);
    // Receiver ID
    pkt.append(reinterpret_cast<const char*>(&recvId), 4);
    // Payload
    if (!payload.isEmpty()) pkt.append(payload);
    return pkt;
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
    m_authenticated = false;
    m_authRetries = 0;
    m_seq = 0;
    m_radioId = 0;
    m_radioSerialPort = 0;
    m_radioAudioPort = 0;

    m_state.store(State::Connecting);
    emit stateChanged(State::Connecting);

    m_socket->close();
    m_socket->bind();

    m_localId = makeId(QHostAddress(QHostAddress::LocalHost), m_socket->localPort());

    qCDebug(lcConnection) << "IcomIpConnection: connecting to" << host
             << "ctrl:" << ctrlPort << "localId:" << Qt::hex << m_localId;

    // Send SYN (type 0x03, seq 0) to control port
    QByteArray syn = buildPacket(0x03, 0, m_localId, 0);
    m_socket->writeDatagram(syn, m_host, m_ctrlPort);
    qCDebug(lcConnection) << "IcomIpConnection: sent SYN to" << host << ":" << ctrlPort;
}

void IcomIpConnection::disconnectFromRadio()
{
    m_authRetryTimer->stop();
    m_keepAliveTimer->stop();
    m_socket->close();
    m_state.store(State::Disconnected);
    m_authenticated = false;
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

        if (data.size() < 12) continue;

        uint32_t totalLen = qFromLittleEndian<quint32>(
            reinterpret_cast<const uchar*>(data.constData()));
        uint16_t type = qFromLittleEndian<quint16>(
            reinterpret_cast<const uchar*>(data.constData()) + 4);
        uint16_t seq  = qFromLittleEndian<quint16>(
            reinterpret_cast<const uchar*>(data.constData()) + 6);
        uint32_t sndId = qFromLittleEndian<quint32>(
            reinterpret_cast<const uchar*>(data.constData()) + 8);
        uint32_t rcvId = qFromLittleEndian<quint32>(
            reinterpret_cast<const uchar*>(data.constData()) + 12);

        qCDebug(lcConnection) << "IcomIpConnection: recv" << data.size() << "bytes"
                 << "type:" << Qt::hex << type << "seq:" << seq
                 << "from" << sender.toString() << ":" << senderPort;

        if (senderPort == m_ctrlPort) {
            processCtrlPacket(type, seq, sndId, rcvId, data.mid(16));
        } else if (senderPort == m_serialPort) {
            processSerialData(data.mid(16));
        }
    }
}

void IcomIpConnection::processCtrlPacket(uint16_t type, uint16_t seq,
                                          uint32_t sndId, uint32_t rcvId,
                                          const QByteArray& payload)
{
    Q_UNUSED(seq);
    Q_UNUSED(payload);

    if (type == 0x04) { // ACK - radio responded to our SYN
        m_radioId = sndId;
        qCDebug(lcConnection) << "IcomIpConnection: ACK received, radioId:" << Qt::hex << m_radioId;

        // Send "Are you ready" (type 0x06, seq 1)
        QByteArray ready = buildPacket(0x06, 1, m_localId, m_radioId);
        m_socket->writeDatagram(ready, m_host, m_ctrlPort);
        qCDebug(lcConnection) << "IcomIpConnection: sent READY";
    }
    else if (type == 0x06) { // Ready response
        qCDebug(lcConnection) << "IcomIpConnection: ready confirmed";

        // Send auth credentials
        QByteArray authPayload;
        authPayload.append(m_username.toUtf8());
        authPayload.append('\x00');
        authPayload.append(m_password.toUtf8());
        authPayload.append('\x00');
        QByteArray auth = buildPacket(0x00, ++m_seq, m_localId, m_radioId, authPayload);
        m_socket->writeDatagram(auth, m_host, m_ctrlPort);
        qCDebug(lcConnection) << "IcomIpConnection: sent AUTH";
    }
    else if (type == 0x07) { // Ping - radio is alive
        // Respond with ping back
        QByteArray pong = buildPacket(0x07, seq, m_localId, m_radioId);
        m_socket->writeDatagram(pong, m_host, m_ctrlPort);
    }
    else if (type == 0x01) { // NACK - retransmit
        qCDebug(lcConnection) << "IcomIpConnection: NACK received";
    }

    // Any response means auth succeeded (for now)
    if (!m_authenticated) {
        m_authenticated = true;
        m_authRetryTimer->stop();
        m_state.store(State::Connected);
        emit stateChanged(State::Connected);
        emit connected();
        m_keepAliveTimer->start();
        qCDebug(lcConnection) << "IcomIpConnection: connected";

        // Start polling
        sendCivCommand(IcomCivProtocol::CMD_FREQ, 0);
        sendCivCommand(IcomCivProtocol::CMD_MODE, 0);
    }
}

void IcomIpConnection::processSerialData(const QByteArray& civPayload)
{
    if (civPayload.isEmpty()) return;

    qCDebug(lcConnection) << "IcomIpConnection: serial data" << civPayload.toHex().left(30);

    CivResponse resp = m_civProto.parseResponse(civPayload);
    if (!resp.valid) return;

    switch (resp.cmd) {
    case IcomCivProtocol::CMD_FREQ: {
        uint64_t freq = IcomCivProtocol::decodeBcdFreq(resp.data);
        qCDebug(lcConnection) << "IcomIpConnection: frequency" << freq << "Hz";
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

void IcomIpConnection::onKeepAlive()
{
    if (m_state.load() != State::Connected) return;

    // Send ping
    QByteArray ping = buildPacket(0x07, m_seq, m_localId, m_radioId);
    m_socket->writeDatagram(ping, m_host, m_ctrlPort);

    // Poll frequency on serial port
    sendCivCommand(IcomCivProtocol::CMD_FREQ, 0);
}

void IcomIpConnection::sendCivCommand(uint8_t cmd, uint8_t subCmd, const QByteArray& data)
{
    QByteArray civFrame = m_civProto.buildCommand(cmd, subCmd, data);

    QByteArray pkt = buildPacket(0x00, ++m_seq, m_localId, m_radioId, civFrame);
    m_socket->writeDatagram(pkt, m_host, m_serialPort);
    qCDebug(lcConnection) << "IcomIpConnection: sent CI-V cmd:" << Qt::hex << cmd << "to serial port";
}

void IcomIpConnection::setFrequency(uint64_t freqHz)
{
    sendCivCommand(IcomCivProtocol::CMD_FREQ, 0,
                   IcomCivProtocol::encodeBcdFreq(freqHz));
}

} // namespace MasterSDR
