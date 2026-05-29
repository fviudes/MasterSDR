#include "core/HermesDiscovery.h"

#include <QDateTime>
#include <QNetworkInterface>
#include <QDebug>
#include <QUdpSocket>

namespace MasterSDR {

HermesDiscovery::HermesDiscovery(QObject* parent)
    : QObject(parent)
    , m_replySocket(nullptr)
{
    m_replySocket = new QUdpSocket(this);

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

    // Create a fresh socket for each discovery cycle
    if (m_replySocket->state() == QAbstractSocket::BoundState) {
        m_replySocket->close();
    }

    // Bind to ANY address on any port (HL2 will reply to the sender)
    m_replySocket->bind(QHostAddress::AnyIPv4, 0, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);

    // Enable broadcast on the socket
    m_replySocket->setSocketOption(QAbstractSocket::MulticastLoopbackOption, 0);

    connect(m_replySocket, &QUdpSocket::readyRead, this, &HermesDiscovery::onReadyRead);

    m_staleTimer->start();
    sendDiscoveryProbe();
}

void HermesDiscovery::stopDiscovery()
{
    if (!m_running) return;
    m_running = false;
    m_staleTimer->stop();
    disconnect(m_replySocket, &QUdpSocket::readyRead, this, nullptr);
    m_replySocket->close();
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
    // HL2 discovery packet: 0xEF 0xFE 0x02 + 57 zeros = 60 bytes

    // Send to each interface's SUBNET broadcast (matches working hl2setup)
    const auto ifaces = QNetworkInterface::allInterfaces();
    for (const auto& iface : ifaces) {
        if (iface.flags() & QNetworkInterface::IsLoopBack) continue;
        if (!(iface.flags() & QNetworkInterface::IsUp)) continue;

        for (const auto& entry : iface.addressEntries()) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol) continue;
            if (entry.ip().isLoopback()) continue;

            QHostAddress subnetBcast = entry.broadcast();
            if (subnetBcast.isNull()) continue;

            qDebug() << "Hermes discovery: probing" << entry.ip().toString()
                     << "bcast:" << subnetBcast.toString();
            m_replySocket->writeDatagram(pkt, subnetBcast, HermesProtocol::DISCOVERY_PORT);
        }
    }

    // Also try port 1024 (for gateware update mode)
    for (const auto& iface : ifaces) {
        if (iface.flags() & QNetworkInterface::IsLoopBack) continue;
        if (!(iface.flags() & QNetworkInterface::IsUp)) continue;
        for (const auto& entry : iface.addressEntries()) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol) continue;
            if (entry.ip().isLoopback()) continue;
            QHostAddress bcast = entry.broadcast();
            if (bcast.isNull()) continue;
            m_replySocket->writeDatagram(pkt, bcast, HermesProtocol::DISCOVERY_PORT_ALT);
        }
    }
}

void HermesDiscovery::onReadyRead()
{
    while (m_replySocket->hasPendingDatagrams()) {
        QByteArray data;
        data.resize(static_cast<int>(m_replySocket->pendingDatagramSize()));
        QHostAddress sender;
        quint16 senderPort = 0;
        m_replySocket->readDatagram(data.data(), data.size(), &sender, &senderPort);

        if (!HermesProtocol::isValidReply(data)) continue;

        auto reply = HermesProtocol::decodeDiscoveryReply(data, sender.toString(), HermesProtocol::COMMAND_PORT);

        HermesRadioInfo info;
        info.ipAddress      = sender.toString();
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
            qDebug() << "Hermes Lite 2 discovered from" << sender.toString()
                     << "MAC:" << info.mac << "GW:" << info.gatewareVersion;
            emit radioDiscovered(info);
        }
    }
}

void HermesDiscovery::onStaleCheck()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QStringList stale;
    for (auto it = m_lastSeen.cbegin(); it != m_lastSeen.cend(); ++it) {
        if (now - it.value() > STALE_TIMEOUT_MS) stale.append(it.key());
    }
    for (const auto& key : stale) {
        m_radios.remove(key);
        m_lastSeen.remove(key);
        emit radioLost(key);
    }
    if (m_running) sendDiscoveryProbe();
}

} // namespace MasterSDR
