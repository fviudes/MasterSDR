#include "core/HermesDiscovery.h"

#include <QDateTime>
#include <QNetworkInterface>
#include <QDebug>

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

    m_socket->bind(QHostAddress::AnyIPv4, 0, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);

    // Also bind to each interface explicitly for reliable broadcast
    const auto ifaces = QNetworkInterface::allInterfaces();
    for (const auto& iface : ifaces) {
        if (iface.flags() & QNetworkInterface::IsLoopBack) continue;
        for (const auto& entry : iface.addressEntries()) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol) continue;
            if (entry.ip() == QHostAddress::LocalHost) continue;
            qDebug() << "Hermes discovery: probing on" << entry.ip().toString();
        }
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
    m_socket->writeDatagram(pkt, QHostAddress::Broadcast, HermesProtocol::DISCOVERY_PORT);
    m_socket->writeDatagram(pkt, QHostAddress::Broadcast, HermesProtocol::DISCOVERY_PORT_ALT);

    // Also send to 255.255.255.255 explicitly
    m_socket->writeDatagram(pkt, QHostAddress(QStringLiteral("255.255.255.255")), HermesProtocol::DISCOVERY_PORT);

    qDebug() << "Hermes: sent discovery probes on ports 1024/1025";
}

void HermesDiscovery::onReadyRead()
{
    while (m_socket->hasPendingDatagrams()) {
        QByteArray data;
        data.resize(static_cast<int>(m_socket->pendingDatagramSize()));
        QHostAddress sender;
        quint16 senderPort = 0;
        m_socket->readDatagram(data.data(), data.size(), &sender, &senderPort);

        if (!HermesProtocol::isValidReply(data)) continue;

        HermesRadioInfo info;
        auto reply = HermesProtocol::decodeDiscoveryReply(data, sender.toString(), HermesProtocol::COMMAND_PORT);

        info.ipAddress      = reply.ipAddress;
        info.udpPort        = reply.udpPort;
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
                     << "RX:" << info.numReceivers;
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
