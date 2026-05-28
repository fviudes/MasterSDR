#include "core/HermesConnection.h"
#include "core/LogManager.h"

#include <QDebug>
#include <QDateTime>
#include <QNetworkInterface>
#include <QThread>

namespace MasterSDR {

HermesConnection::HermesConnection(QObject* parent)
    : QObject(parent)
{
}

HermesConnection::~HermesConnection()
{
    disconnectFromRadio();
}

void HermesConnection::init()
{
    m_socket = new QUdpSocket(this);
    connect(m_socket, &QUdpSocket::readyRead, this, &HermesConnection::onReadyRead);

    m_watchdogTimer = new QTimer(this);
    m_watchdogTimer->setInterval(WATCHDOG_INTERVAL_MS);
    connect(m_watchdogTimer, &QTimer::timeout, this, &HermesConnection::onWatchdogTimeout);
}

void HermesConnection::connectToRadio(const HermesRadioInfo& info)
{
    if (!m_socket) init();

    m_radioAddress = QHostAddress(info.ipAddress);
    m_radioPort = info.udpPort;

    m_state.store(State::Connecting);
    emit stateChanged(State::Connecting);

    // Close any existing socket binding
    m_socket->close();

    // Bind to any available port for receiving HL2 data
    if (!m_socket->bind(QHostAddress::AnyIPv4, 0)) {
        m_errorString = "Failed to bind UDP socket: " + m_socket->errorString();
        qCWarning(lcConnection) << m_errorString;
        m_state.store(State::Error);
        emit stateChanged(State::Error);
        emit errorOccurred(m_errorString);
        return;
    }

    qCDebug(lcConnection) << "Hermes bound to port:" << m_socket->localPort();

    // Start sequence: Send Stop twice (matches working hl2setup/Python code)
    QByteArray stopPkt = HermesProtocol::buildStopPacket(0);
    m_socket->writeDatagram(stopPkt, m_radioAddress, m_radioPort);
    m_socket->flush();
    QThread::msleep(10);
    m_socket->writeDatagram(stopPkt, m_radioAddress, m_radioPort);
    m_socket->flush();
    QThread::msleep(10);

    // Send Start (radio + wideband) like the Python code
    QByteArray startPkt = HermesProtocol::buildStartPacket(
        HermesProtocol::START_RADIO | HermesProtocol::START_WIDEBAND | HermesProtocol::START_DISABLE_WATCHDOG);
    for (int i = 0; i < 3; ++i) {
        m_socket->writeDatagram(startPkt, m_radioAddress, m_radioPort);
        m_socket->flush();
        QThread::msleep(2);
    }

    // Set number of receivers to 1, duplex off, MOX off
    uint32_t ctrl = HermesProtocol::buildControlWord(HermesProtocol::SPEED_48K, 1, false, false);
    sendCommand(HermesProtocol::ADDR_CONTROL, ctrl);

    // Set default frequency
    setRX1Frequency(14074000);

    m_state.store(State::Connected);
    emit stateChanged(State::Connected);
    emit connected();

    m_watchdogTimer->start();

    qCDebug(lcConnection) << "Hermes Lite 2 connected:" << info.ipAddress << "port:" << info.udpPort;
}

void HermesConnection::disconnectFromRadio()
{
    m_watchdogTimer->stop();

    if (m_socket && m_state.load() == State::Connected) {
        QByteArray stopPkt = HermesProtocol::buildStopPacket(0);
        m_socket->writeDatagram(stopPkt, m_radioAddress, m_radioPort);
        QThread::msleep(10);
        m_socket->writeDatagram(stopPkt, m_radioAddress, m_radioPort);
        m_socket->flush();
    }

    if (m_socket) {
        m_socket->close();
    }

    m_state.store(State::Disconnected);
    emit stateChanged(State::Disconnected);
    emit disconnected();

    qCDebug(lcConnection) << "Hermes Lite 2 disconnected";
}

void HermesConnection::sendCommand(uint8_t addr, uint32_t data)
{
    if (m_state.load() != State::Connected || !m_socket) return;
    QByteArray pkt = HermesProtocol::buildCommandPacket(addr, data);
    m_socket->writeDatagram(pkt, m_radioAddress, m_radioPort);
    m_socket->flush();
}

void HermesConnection::sendCommand(uint8_t addr, const QByteArray& data)
{
    if (m_state.load() != State::Connected || !m_socket) return;
    QByteArray pkt = HermesProtocol::buildCommandPacket(addr, data);
    m_socket->writeDatagram(pkt, m_radioAddress, m_radioPort);
    m_socket->flush();
}

void HermesConnection::setFrequency(uint32_t freqHz)
{
    m_txFreq = freqHz;
    sendCommand(HermesProtocol::ADDR_TX_NCO, freqHz);
}

void HermesConnection::setRX1Frequency(uint32_t freqHz)
{
    m_rxFreq = freqHz;
    sendCommand(HermesProtocol::ADDR_RX1_NCO, freqHz);
}

void HermesConnection::startRadio()
{
    QByteArray pkt = HermesProtocol::buildStartPacket(
        HermesProtocol::START_RADIO | HermesProtocol::START_WIDEBAND | HermesProtocol::START_DISABLE_WATCHDOG);
    if (m_socket) {
        m_socket->writeDatagram(pkt, m_radioAddress, m_radioPort);
        m_socket->flush();
    }
}

void HermesConnection::stopRadio()
{
    QByteArray pkt = HermesProtocol::buildStopPacket(0);
    if (m_socket) {
        m_socket->writeDatagram(pkt, m_radioAddress, m_radioPort);
        m_socket->flush();
    }
}

void HermesConnection::setMox(bool active)
{
    uint32_t ctrl = HermesProtocol::buildControlWord(HermesProtocol::SPEED_48K, 1, false, active);
    sendCommand(HermesProtocol::ADDR_CONTROL, ctrl);
    emit pttStateChanged(active);
}

void HermesConnection::onReadyRead()
{
    while (m_socket && m_socket->hasPendingDatagrams()) {
        QByteArray data;
        data.resize(static_cast<int>(m_socket->pendingDatagramSize()));
        QHostAddress sender;
        quint16 senderPort = 0;
        m_socket->readDatagram(data.data(), data.size(), &sender, &senderPort);

        if (data.isEmpty()) continue;

        qCDebug(lcConnection) << "Hermes received:" << data.size() << "bytes from"
                               << sender.toString() << ":" << senderPort
                               << "header:" << Qt::hex
                               << static_cast<uint8_t>(data[0])
                               << static_cast<uint8_t>(data[1])
                               << static_cast<uint8_t>(data[2]);

        if (data.size() >= 60 && HermesProtocol::isValidReply(data)) {
            processReply(data);
        }
    }
}

void HermesConnection::processReply(const QByteArray& data)
{
    auto reply = HermesProtocol::decodeDiscoveryReply(data, m_radioAddress.toString(), m_radioPort);

    emit discoveryReplyReceived(reply);
    emit temperatureUpdated(reply.temperature);
    emit powerUpdated(reply.forwardPower, reply.reversePower);
    emit pttStateChanged(reply.ptt);
    emit cwKeyChanged(reply.extCwKey);
}

void HermesConnection::onWatchdogTimeout()
{
    if (m_state.load() != State::Connected || !m_socket) return;
    // Send discovery packet as keep-alive
    QByteArray pkt = HermesProtocol::buildDiscoveryPacket();
    m_socket->writeDatagram(pkt, m_radioAddress, m_radioPort);
    m_socket->flush();
}

} // namespace MasterSDR
