#include "core/IcomIpConnection.h"

#include <QDebug>
#include <QDateTime>
#include <QtEndian>
#include <QNetworkInterface>

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
    m_authRetryTimer->setInterval(2000);
    connect(m_authRetryTimer, &QTimer::timeout, this, &IcomIpConnection::retryAuth);
}

IcomIpConnection::~IcomIpConnection()
{
    disconnectFromRadio();
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

    m_state.store(State::Connecting);
    emit stateChanged(State::Connecting);

    m_socket->close();
    m_socket->bind();

    qDebug() << "IcomIpConnection: connecting to" << host
             << "ctrl:" << ctrlPort << "serial:" << serialPort << "audio:" << audioPort;

    sendAuthPacket();
}

void IcomIpConnection::sendAuthPacket()
{
    // Icom IP auth: Sends username + password to control port
    // The radio should respond with a confirmation on the same port
    QByteArray payload;
    payload.append(static_cast<char>(PKT_TYPE_AUTH));
    payload.append(m_username.toUtf8());
    payload.append('\x00');
    payload.append(m_password.toUtf8());
    payload.append('\x00');

    sendCtrlPacket(payload);

    qDebug() << "IcomIpConnection: auth sent, username:" << m_username
             << "attempt:" << (m_authRetries + 1);

    m_authRetryTimer->start();
}

void IcomIpConnection::retryAuth()
{
    if (m_authenticated || m_state.load() == State::Disconnected) return;

    m_authRetries++;
    if (m_authRetries < MAX_AUTH_RETRIES) {
        qDebug() << "IcomIpConnection: auth retry" << m_authRetries;
        sendAuthPacket();
    } else {
        m_errorString = "Authentication failed after " + QString::number(MAX_AUTH_RETRIES) + " attempts";
        qDebug() << "IcomIpConnection:" << m_errorString;
        m_state.store(State::Error);
        emit stateChanged(State::Error);
        emit errorOccurred(m_errorString);
    }
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

void IcomIpConnection::sendCtrlPacket(const QByteArray& payload)
{
    QByteArray pkt;
    pkt.append('\xFE');
    pkt.append('\xFE');

    uint16_t len = static_cast<uint16_t>(payload.size());
    pkt.append(static_cast<char>((len >> 8) & 0xFF));
    pkt.append(static_cast<char>(len & 0xFF));
    pkt.append(payload);

    if (m_socket->state() == QAbstractSocket::BoundState) {
        qint64 sent = m_socket->writeDatagram(pkt, m_host, m_ctrlPort);
        qDebug() << "IcomIpConnection: sent" << sent << "bytes to" << m_host.toString() << ":" << m_ctrlPort;
    }
}

void IcomIpConnection::sendSerialPacket(const QByteArray& civFrame)
{
    QByteArray pkt;
    pkt.append('\xFE');
    pkt.append('\xFE');

    uint16_t len = static_cast<uint16_t>(civFrame.size());
    pkt.append(static_cast<char>((len >> 8) & 0xFF));
    pkt.append(static_cast<char>(len & 0xFF));
    pkt.append(civFrame);

    if (m_socket->state() == QAbstractSocket::BoundState) {
        m_socket->writeDatagram(pkt, m_host, m_serialPort);
    }
}

void IcomIpConnection::sendCivCommand(uint8_t cmd, uint8_t subCmd, const QByteArray& data)
{
    QByteArray civFrame = m_civProto.buildCommand(cmd, subCmd, data);
    sendSerialPacket(civFrame);
}

void IcomIpConnection::onReadyRead()
{
    while (m_socket->hasPendingDatagrams()) {
        QByteArray data;
        data.resize(static_cast<int>(m_socket->pendingDatagramSize()));
        QHostAddress sender;
        quint16 senderPort = 0;
        m_socket->readDatagram(data.data(), data.size(), &sender, &senderPort);

        qDebug() << "IcomIpConnection: received" << data.size() << "bytes from"
                 << sender.toString() << ":" << senderPort
                 << "hex:" << data.toHex().left(40);

        if (sender != m_host) {
            qDebug() << "IcomIpConnection: ignoring packet from" << sender.toString() << "(expected" << m_host.toString() << ")";
            continue;
        }

        if (senderPort == m_ctrlPort) {
            processCtrlData(data);
        } else if (senderPort == m_serialPort) {
            processSerialData(data);
        }
    }
}

void IcomIpConnection::processCtrlData(const QByteArray& data)
{
    if (data.size() < 4) {
        qDebug() << "IcomIpConnection: ctrl data too short:" << data.size();
        return;
    }

    uint8_t b0 = static_cast<uint8_t>(data[0]);
    uint8_t b1 = static_cast<uint8_t>(data[1]);

    qDebug() << "IcomIpConnection: ctrl data header:" << Qt::hex << b0 << b1;

    if (b0 != 0xFE || b1 != 0xFE) {
        qDebug() << "IcomIpConnection: invalid ctrl header, raw:" << data.toHex().left(20);
        return;
    }

    uint16_t pktLen = qFromBigEndian<quint16>(
        reinterpret_cast<const uchar*>(data.constData()) + 2);

    if (static_cast<int>(pktLen + 4) > data.size()) {
        qDebug() << "IcomIpConnection: partial packet, need" << pktLen + 4 << "have" << data.size();
        return;
    }

    QByteArray payload = data.mid(4, pktLen);
    if (payload.isEmpty()) return;

    uint8_t type = static_cast<uint8_t>(payload[0]);
    qDebug() << "IcomIpConnection: ctrl payload type:" << Qt::hex << type;

    if (!m_authenticated) {
        // Any valid response from the radio on the control port means auth succeeded
        m_authRetryTimer->stop();
        m_authenticated = true;
        m_state.store(State::Connected);
        emit stateChanged(State::Connected);
        emit connected();
        m_keepAliveTimer->start();

        qDebug() << "IcomIpConnection: authenticated and connected";

        // Start polling
        sendCivCommand(IcomCivProtocol::CMD_FREQ, 0);
        sendCivCommand(IcomCivProtocol::CMD_MODE, 0);
    }
}

void IcomIpConnection::processSerialData(const QByteArray& data)
{
    if (data.size() < 4) return;
    if (static_cast<uint8_t>(data[0]) != 0xFE || static_cast<uint8_t>(data[1]) != 0xFE) return;

    uint16_t len = qFromBigEndian<quint16>(
        reinterpret_cast<const uchar*>(data.constData()) + 2);
    if (static_cast<int>(len + 4) > data.size()) return;

    QByteArray civPayload = data.mid(4, len);

    qDebug() << "IcomIpConnection: serial data" << civPayload.toHex().left(30);

    CivResponse resp = m_civProto.parseResponse(civPayload);
    if (!resp.valid) {
        qDebug() << "IcomIpConnection: invalid CI-V response";
        return;
    }

    qDebug() << "IcomIpConnection: CI-V cmd:" << Qt::hex << static_cast<int>(resp.cmd);

    switch (resp.cmd) {
    case IcomCivProtocol::CMD_FREQ: {
        uint64_t freq = IcomCivProtocol::decodeBcdFreq(resp.data);
        qDebug() << "IcomIpConnection: frequency" << freq << "Hz";
        emit frequencyUpdated(freq);
        break;
    }
    case IcomCivProtocol::CMD_MODE: {
        if (!resp.data.isEmpty()) {
            auto mode = static_cast<IcomCivProtocol::CivMode>(static_cast<uint8_t>(resp.data[0]));
            qDebug() << "IcomIpConnection: mode" << IcomCivProtocol::modeToString(mode);
            emit modeUpdated(IcomCivProtocol::modeToString(mode));
        }
        break;
    }
    default:
        qDebug() << "IcomIpConnection: unhandled CI-V cmd:" << Qt::hex << static_cast<int>(resp.cmd);
        break;
    }
}

void IcomIpConnection::onKeepAlive()
{
    if (m_state.load() != State::Connected) return;

    QByteArray idle;
    idle.append(static_cast<char>(PKT_TYPE_IDLE));
    sendCtrlPacket(idle);

    sendCivCommand(IcomCivProtocol::CMD_FREQ, 0);
}

void IcomIpConnection::setFrequency(uint64_t freqHz)
{
    sendCivCommand(IcomCivProtocol::CMD_FREQ, 0,
                   IcomCivProtocol::encodeBcdFreq(freqHz));
}

} // namespace MasterSDR
