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

    // Close and rebind
    m_socket->close();
    m_socket->bind();

    qDebug() << "IcomIpConnection: connecting to" << host
             << "ctrl:" << ctrlPort << "serial:" << serialPort << "audio:" << audioPort;

    // Send auth packet to control port
    // Format: type(1) + username + \0 + password + \0
    QByteArray payload;
    payload.append(static_cast<char>(PKT_TYPE_AUTH));
    payload.append(username.toUtf8());
    payload.append('\x00');
    payload.append(password.toUtf8());
    payload.append('\x00');

    sendCtrlPacket(payload);
}

void IcomIpConnection::disconnectFromRadio()
{
    m_keepAliveTimer->stop();
    m_socket->close();
    m_state.store(State::Disconnected);
    m_authenticated = false;
    emit stateChanged(State::Disconnected);
    emit disconnected();
}

void IcomIpConnection::sendCtrlPacket(const QByteArray& payload)
{
    // Icom UDP control packet: FE FE + length(2) + payload
    QByteArray pkt;
    pkt.append('\xFE');
    pkt.append('\xFE');

    uint16_t len = static_cast<uint16_t>(payload.size());
    pkt.append(static_cast<char>((len >> 8) & 0xFF));
    pkt.append(static_cast<char>(len & 0xFF));
    pkt.append(payload);

    if (m_socket->state() == QAbstractSocket::BoundState) {
        m_socket->writeDatagram(pkt, m_host, m_ctrlPort);
        m_socket->flush();
    }
}

void IcomIpConnection::sendSerialPacket(const QByteArray& civFrame)
{
    // Icom UDP serial packet = same wrapper, sent to serial port
    QByteArray pkt;
    pkt.append('\xFE');
    pkt.append('\xFE');

    uint16_t len = static_cast<uint16_t>(civFrame.size());
    pkt.append(static_cast<char>((len >> 8) & 0xFF));
    pkt.append(static_cast<char>(len & 0xFF));
    pkt.append(civFrame);

    if (m_socket->state() == QAbstractSocket::BoundState) {
        m_socket->writeDatagram(pkt, m_host, m_serialPort);
        m_socket->flush();
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

        if (sender != m_host) continue;

        // Route to control or serial processor based on port
        if (senderPort == m_ctrlPort) {
            processCtrlData(data);
        } else if (senderPort == m_serialPort) {
            processSerialData(data);
        } else if (senderPort == m_audioPort) {
            // Audio data - not processed here
        }
    }
}

void IcomIpConnection::processCtrlData(const QByteArray& data)
{
    if (data.size() < 4) return;
    if (static_cast<uint8_t>(data[0]) != 0xFE || static_cast<uint8_t>(data[1]) != 0xFE) return;

    uint16_t len = qFromBigEndian<quint16>(
        reinterpret_cast<const uchar*>(data.constData()) + 2);
    if (static_cast<int>(len + 4) > data.size()) return;

    QByteArray payload = data.mid(4, len);
    if (payload.isEmpty()) return;

    uint8_t type = static_cast<uint8_t>(payload[0]);

    if (type == PKT_TYPE_IDLE || type == PKT_TYPE_PING) {
        // Keep-alive - respond with same
        sendCtrlPacket(payload);
    } else if (!m_authenticated) {
        // Auth response from radio - accept any valid response as success
        m_authenticated = true;
        m_state.store(State::Connected);
        emit stateChanged(State::Connected);
        emit connected();
        m_keepAliveTimer->start();

        qDebug() << "IcomIpConnection: authenticated and connected";

        // Start polling radio state via serial port
        sendCivCommand(IcomCivProtocol::CMD_FREQ, 0);
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

    // Parse CI-V response
    CivResponse resp = m_civProto.parseResponse(civPayload);
    if (!resp.valid) return;

    switch (resp.cmd) {
    case IcomCivProtocol::CMD_FREQ: {
        uint64_t freq = IcomCivProtocol::decodeBcdFreq(resp.data);
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

    // Send idle on control port
    QByteArray idle;
    idle.append(static_cast<char>(PKT_TYPE_IDLE));
    sendCtrlPacket(idle);

    // Poll frequency on serial port
    sendCivCommand(IcomCivProtocol::CMD_FREQ, 0);
}

void IcomIpConnection::setFrequency(uint64_t freqHz)
{
    sendCivCommand(IcomCivProtocol::CMD_FREQ, 0,
                   IcomCivProtocol::encodeBcdFreq(freqHz));
}

} // namespace MasterSDR
