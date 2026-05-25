#include "core/IcomIpConnection.h"

#include <QDebug>
#include <QDateTime>
#include <QtEndian>

namespace MasterSDR {

IcomIpConnection::IcomIpConnection(QObject* parent)
    : QObject(parent)
    , m_civProto(DEFAULT_CI_V_ADDR)
{
}

IcomIpConnection::~IcomIpConnection()
{
    disconnectFromRadio();
}

void IcomIpConnection::init()
{
    m_socket = new QTcpSocket(this);
    connect(m_socket, &QTcpSocket::connected, this, &IcomIpConnection::onConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &IcomIpConnection::onDisconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &IcomIpConnection::onReadyRead);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &IcomIpConnection::onError);

    m_authTimer = new QTimer(this);
    m_authTimer->setSingleShot(true);
    m_authTimer->setInterval(AUTH_TIMEOUT_MS);
    connect(m_authTimer, &QTimer::timeout, this, &IcomIpConnection::onAuthTimerTimeout);

    m_watchdogTimer = new QTimer(this);
    m_watchdogTimer->setInterval(WATCHDOG_INTERVAL_MS);
    connect(m_watchdogTimer, &QTimer::timeout, this, &IcomIpConnection::onWatchdogTimeout);
}

void IcomIpConnection::connectToRadio(const QString& host, uint16_t ctrlPort,
                                       const QString& username, const QString& password)
{
    if (!m_socket) init();

    m_host = QHostAddress(host);
    m_port = ctrlPort;
    m_username = username;
    m_password = password;
    m_authenticated = false;
    m_readBuffer.clear();
    m_packetSeq = 0;

    m_state.store(State::Connecting);
    emit stateChanged(State::Connecting);

    qDebug() << "IcomIpConnection: connecting to" << host << ":" << ctrlPort;
    m_socket->connectToHost(host, ctrlPort);
}

void IcomIpConnection::disconnectFromRadio()
{
    m_watchdogTimer->stop();
    m_authTimer->stop();

    if (m_socket && m_socket->state() == QAbstractSocket::ConnectedState) {
        m_socket->disconnectFromHost();
    }

    m_state.store(State::Disconnected);
    m_authenticated = false;
    emit stateChanged(State::Disconnected);
    emit disconnected();

    qDebug() << "IcomIpConnection: disconnected";
}

void IcomIpConnection::onConnected()
{
    qDebug() << "IcomIpConnection: TCP connected, waiting for auth challenge...";
    m_state.store(State::Authenticating);
    emit stateChanged(State::Authenticating);
    m_authTimer->start();
}

void IcomIpConnection::onDisconnected()
{
    m_watchdogTimer->stop();
    if (m_state.load() != State::Disconnected) {
        m_state.store(State::Disconnected);
        m_authenticated = false;
        emit stateChanged(State::Disconnected);
        emit disconnected();
    }
}

void IcomIpConnection::onReadyRead()
{
    m_readBuffer.append(m_socket->readAll());

    if (!m_authenticated) {
        // Try to detect auth challenge
        if (m_readBuffer.size() >= 4) {
            processAuthChallenge(m_readBuffer);
        }
    } else {
        // Process CI-V over IP packets
        // Each packet starts with magic bytes 0xFE 0xFE + length + seq + CI-V data
        while (m_readBuffer.size() >= 6) {
            if (static_cast<uint8_t>(m_readBuffer[0]) == STX1 &&
                static_cast<uint8_t>(m_readBuffer[1]) == STX2) {
                // Extract length (2 bytes, big-endian)
                uint16_t pktLen = qFromBigEndian<quint16>(
                    reinterpret_cast<const uchar*>(m_readBuffer.constData()) + 2);
                uint16_t seq = qFromBigEndian<quint16>(
                    reinterpret_cast<const uchar*>(m_readBuffer.constData()) + 4);

                if (m_readBuffer.size() >= static_cast<int>(pktLen + 6)) {
                    QByteArray pkt = m_readBuffer.mid(6, pktLen);
                    m_readBuffer.remove(0, pktLen + 6);
                    processCivPacket(pkt);
                } else {
                    break; // Wait for more data
                }
            } else {
                // Skip unknown byte
                m_readBuffer.remove(0, 1);
            }
        }
    }
}

void IcomIpConnection::processAuthChallenge(const QByteArray& data)
{
    Q_UNUSED(data);

    // Icom IP radios send a specific challenge packet
    // Format varies by radio model and firmware version
    // Common format: 0xFE 0xFE <len> <auth_type> <challenge_data>

    qDebug() << "IcomIpConnection: received" << data.size() << "bytes for auth";

    // Send authentication response
    // Format: 0xFE 0xFE <len> <auth_response_type> <username> <password>
    QByteArray authResp;
    authResp.append(static_cast<char>(STX1));
    authResp.append(static_cast<char>(STX2));

    QByteArray payload;
    payload.append('\x01');  // Auth response type
    payload.append(m_username.toUtf8());
    payload.append('\x00');
    payload.append(m_password.toUtf8());
    payload.append('\x00');

    uint16_t len = static_cast<uint16_t>(payload.size());
    authResp.append(static_cast<char>((len >> 8) & 0xFF));
    authResp.append(static_cast<char>(len & 0xFF));
    authResp.append(payload);

    qDebug() << "IcomIpConnection: sending auth response" << authResp.size() << "bytes"
             << "user:" << m_username;

    m_socket->write(authResp);
    m_socket->flush();

    m_authenticated = true;
    m_authTimer->stop();
    m_state.store(State::Connected);
    emit stateChanged(State::Connected);
    emit connected();
    m_watchdogTimer->start();

    qDebug() << "IcomIpConnection: authenticated, connected";

    // Query radio for current state
    sendCivCommand(IcomCivProtocol::CMD_FREQ, 0);
}

void IcomIpConnection::processCivPacket(const QByteArray& data)
{
    if (data.size() < 4) return;

    CivResponse resp = m_civProto.parseResponse(data);
    if (!resp.valid) return;

    emit civResponseReceived(resp);

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

void IcomIpConnection::onError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error);
    m_errorString = m_socket->errorString();
    qDebug() << "IcomIpConnection: error -" << m_errorString;
    emit errorOccurred(m_errorString);
}

void IcomIpConnection::onAuthTimerTimeout()
{
    if (!m_authenticated) {
        m_errorString = "Authentication timeout - no response from radio";
        qDebug() << "IcomIpConnection:" << m_errorString;
        emit errorOccurred(m_errorString);
        disconnectFromRadio();
    }
}

void IcomIpConnection::onWatchdogTimeout()
{
    if (m_state.load() == State::Connected && m_socket) {
        // Send keep-alive / poll for status
        sendCivCommand(IcomCivProtocol::CMD_FREQ, 0);
    }
}

void IcomIpConnection::sendCivCommand(uint8_t cmd, uint8_t subCmd, const QByteArray& data)
{
    QByteArray civFrame = m_civProto.buildCommand(cmd, subCmd, data);
    sendPacket(wrapCivPacket(civFrame));
}

void IcomIpConnection::sendCivCommand(const QByteArray& rawCivFrame)
{
    sendPacket(wrapCivPacket(rawCivFrame));
}

QByteArray IcomIpConnection::wrapCivPacket(const QByteArray& civFrame)
{
    QByteArray pkt;
    pkt.append(static_cast<char>(STX1));
    pkt.append(static_cast<char>(STX2));

    uint16_t len = static_cast<uint16_t>(civFrame.size());
    pkt.append(static_cast<char>((len >> 8) & 0xFF));
    pkt.append(static_cast<char>(len & 0xFF));

    uint16_t seq = static_cast<uint16_t>(m_packetSeq++);
    pkt.append(static_cast<char>((seq >> 8) & 0xFF));
    pkt.append(static_cast<char>(seq & 0xFF));

    pkt.append(civFrame);
    return pkt;
}

void IcomIpConnection::sendPacket(const QByteArray& data)
{
    if (m_state.load() != State::Connected || !m_socket) return;
    m_socket->write(data);
    m_socket->flush();
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
    sendCivCommand(IcomCivProtocol::CMD_MODE, 0, data);
}

} // namespace MasterSDR
