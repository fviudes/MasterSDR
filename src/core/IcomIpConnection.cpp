#include "core/IcomIpConnection.h"

#include <QDebug>
#include <QDateTime>
#include <QtEndian>

namespace MasterSDR {

IcomIpConnection::IcomIpConnection(QObject* parent)
    : QObject(parent)
    , m_civProto(IcomCivProtocol::DEFAULT_CI_V_ADDR)
{
}

IcomIpConnection::~IcomIpConnection()
{
    disconnectFromRadio();
}

void IcomIpConnection::init()
{
    m_socket = new QUdpSocket(this);
    connect(m_socket, &QUdpSocket::readyRead, this, &IcomIpConnection::onReadyRead);

    m_watchdogTimer = new QTimer(this);
    m_watchdogTimer->setInterval(WATCHDOG_INTERVAL_MS);
    connect(m_watchdogTimer, &QTimer::timeout, this, &IcomIpConnection::onWatchdogTimeout);
}

void IcomIpConnection::connectToRadio(const QString& host, uint16_t ctrlPort,
                                       uint16_t serialPort, uint16_t audioPort,
                                       const QString& username, const QString& password)
{
    if (!m_socket) init();

    m_host = QHostAddress(host);
    m_ctrlPort = ctrlPort;
    m_serialPort = serialPort;
    m_audioPort = audioPort;
    m_username = username;
    m_password = password;
    m_readBuffer.clear();
    m_packetSeq = 0;

    m_state.store(State::Connecting);
    emit stateChanged(State::Connecting);

    // Bind to any available port
    m_socket->bind(QHostAddress::AnyIPv4, 0);

    // Send auth packet to control port
    // Icom IP auth packet format: STX STX len seq username \0 password \0
    QByteArray authPkt;
    authPkt.append(static_cast<char>(STX1));
    authPkt.append(static_cast<char>(STX2));

    QByteArray payload;
    payload.append('\x01');
    payload.append(username.toUtf8());
    payload.append('\x00');
    payload.append(password.toUtf8());
    payload.append('\x00');

    uint16_t len = static_cast<uint16_t>(payload.size());
    authPkt.append(static_cast<char>((len >> 8) & 0xFF));
    authPkt.append(static_cast<char>(len & 0xFF));

    uint16_t seq = static_cast<uint16_t>(m_packetSeq++);
    authPkt.append(static_cast<char>((seq >> 8) & 0xFF));
    authPkt.append(static_cast<char>(seq & 0xFF));

    authPkt.append(payload);

    m_socket->writeDatagram(authPkt, m_host, m_ctrlPort);

    m_state.store(State::Connected);
    emit stateChanged(State::Connected);
    emit connected();
    m_watchdogTimer->start();

    qDebug() << "IcomIpConnection UDP: connected to" << host
             << "ctrl:" << ctrlPort << "serial:" << serialPort << "audio:" << audioPort;

    // Query radio state
    sendCivCommand(IcomCivProtocol::CMD_FREQ, 0);
}

void IcomIpConnection::disconnectFromRadio()
{
    m_watchdogTimer->stop();
    if (m_socket) m_socket->close();
    m_state.store(State::Disconnected);
    emit stateChanged(State::Disconnected);
    emit disconnected();
}

void IcomIpConnection::onReadyRead()
{
    while (m_socket && m_socket->hasPendingDatagrams()) {
        QByteArray data;
        data.resize(static_cast<int>(m_socket->pendingDatagramSize()));
        QHostAddress sender;
        quint16 senderPort = 0;
        m_socket->readDatagram(data.data(), data.size(), &sender, &senderPort);

        // Filter: only accept packets from the radio IP on control port
        if (sender != m_host || senderPort != m_ctrlPort) continue;

        // Process CI-V response
        if (data.size() >= 6 &&
            static_cast<uint8_t>(data[0]) == STX1 &&
            static_cast<uint8_t>(data[1]) == STX2) {

            uint16_t pktLen = qFromBigEndian<quint16>(
                reinterpret_cast<const uchar*>(data.constData()) + 2);

            if (static_cast<int>(pktLen + 6) <= data.size()) {
                QByteArray civData = data.mid(6, pktLen);
                processCivResponse(civData);
            }
        }
    }
}

void IcomIpConnection::processCivResponse(const QByteArray& data)
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

void IcomIpConnection::onWatchdogTimeout()
{
    if (m_state.load() == State::Connected) {
        sendCivCommand(IcomCivProtocol::CMD_FREQ, 0);
    }
}

void IcomIpConnection::sendCivCommand(uint8_t cmd, uint8_t subCmd, const QByteArray& data)
{
    QByteArray civFrame = m_civProto.buildCommand(cmd, subCmd, data);

    // Wrap in Icom IP packet
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

    sendPacket(pkt);
}

void IcomIpConnection::sendPacket(const QByteArray& data)
{
    if (m_state.load() != State::Connected || !m_socket) return;
    m_socket->writeDatagram(data, m_host, m_ctrlPort);
}

void IcomIpConnection::setFrequency(uint64_t freqHz)
{
    sendCivCommand(IcomCivProtocol::CMD_FREQ, 0,
                   IcomCivProtocol::encodeBcdFreq(freqHz));
}

} // namespace MasterSDR
