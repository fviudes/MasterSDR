#include "core/HermesDiscovery.h"

#include <QDateTime>
#include <QNetworkInterface>
#include <QDebug>
#include <QUdpSocket>

namespace MasterSDR {

HermesDiscovery::HermesDiscovery(QObject* parent)
    : QObject(parent)
{
    m_socket = new QUdpSocket(this);
    connect(m_socket, &QUdpSocket::readyRead, this, &HermesDiscovery::onReadyRead);

    m_staleTimer = new QTimer(this);
    m_staleTimer->setInterval(4000);
    connect(m_staleTimer, &QTimer::timeout, this, &HermesDiscovery::onStaleCheck);
}

HermesDiscovery::~HermesDiscovery()
{
    stopDiscovery();
}

void HermesDiscovery::startDiscovery()
{
    if (m_running) return;
    m_running = true;

    // Bind to each interface explicitly (HL2 discovery requires binding to specific interface)
    bool bound = false;
    const auto ifaces = QNetworkInterface::allInterfaces();
    for (const auto& iface : ifaces) {
        if (iface.flags() & QNetworkInterface::IsLoopBack) continue;
        for (const auto& entry : iface.addressEntries()) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol) continue;
            if (entry.ip() == QHostAddress::LocalHost) continue;

            QHostAddress bindAddr = entry.ip();
            qDebug() << "Hermes discovery: binding to" << bindAddr.toString();

            m_socket->close();
            if (m_socket->bind(bindAddr, 0, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
                bound = true;
                break; // Bind to first valid interface
            }
        }
        if (bound) break;
    }

    if (!bound) {
        // Fallback: bind to any
        qDebug() << "Hermes discovery: fallback bind to AnyIPv4";
        m_socket->bind(QHostAddress::AnyIPv4, 0, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    }

    m_staleTimer->start();
    sendDiscoveryProbe();
}

void HermesDiscovery::stopDiscovery()
{
    if (!m_running) return;
    m_running = false;
    m_staleTimer->stop();
    m_socket->close();
    m_radios.clear();
    m_lastSeen.clear();
}

QList<HermesRadioInfo> HermesDiscovery::discoveredRadios() const
{
    return m_radios.values();
}

void HermesDiscovery::sendDiscoveryProbe()
{
    QByteArray pkt = HermesProtocol::buildDiscoveryPacket();

    // Send to broadcast on both known HL2 ports
    m_socket->writeDatagram(pkt, QHostAddress::Broadcast, HermesProtocol::DISCOVERY_PORT);
    m_socket->writeDatagram(pkt, QHostAddress::Broadcast, HermesProtocol::DISCOVERY_PORT_ALT);

    qDebug() << "Hermes: sent probes to" << HermesProtocol::DISCOVERY_PORT
             << "and" << HermesProtocol::DISCOVERY_PORT_ALT;
}

void HermesDiscovery::onReadyRead()
{
    while (m_socket->hasPendingDatagrams()) {
        QByteArray data;
        data.resize(static_cast<int>(m_socket->pendingDatagramSize()));
        QHostAddress sender;
        quint16 senderPort = 0;
        m_socket->readDatagram(data.data(), data.size(), &sender, &senderPort);

        qDebug() << "Hermes: received" << data.size() << "bytes from"
                 << sender.toString() << ":" << senderPort;

        if (!HermesProtocol::isValidReply(data)) {
            qDebug() << "Hermes: invalid reply from" << sender.toString();
            continue;
        }

        auto reply = HermesProtocol::decodeDiscoveryReply(data, sender.toString(), HermesProtocol::COMMAND_PORT);

        HermesRadioInfo info;
        info.ipAddress      = reply.ipAddress;
        info.udpPort        = HermesProtocol::COMMAND_PORT;
        info.mac            = reply.mac;
        info.gatewareVersion = reply.gateware;
        info.boardId        = reply.boardId;
        info.numReceivers   = reply.numReceivers;
        info.isRunning      = (reply.type == 3);
        info.temperature    = reply.temperature;

        QString key = info.ipAddress;
        bool isNew = !m_radios.contains(key);

        m_radios[key] = info;
        m_lastSeen[key] = QDateTime::currentMSecsSinceEpoch();

        if (isNew) {
            qDebug() << "Hermes Lite 2 discovered:" << info.ipAddress
                     << "MAC:" << info.mac
                     << "GW:" << info.gatewareVersion
                     << "RX:" << info.numReceivers
                     << "Running:" << info.isRunning;
            emit radioDiscovered(info);
        }
    }
}

void HermesDiscovery::onStaleCheck()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QStringList stale;

    for (auto it = m_lastSeen.cbegin(); it != m_lastSeen.cend(); ++it) {
        if (now - it.value() > STALE_TIMEOUT_MS) {
            stale.append(it.key());
        }
    }

    for (const auto& key : stale) {
        m_radios.remove(key);
        m_lastSeen.remove(key);
        emit radioLost(key);
    }

    if (m_running) {
        sendDiscoveryProbe();
    }
}

} // namespace MasterSDR
