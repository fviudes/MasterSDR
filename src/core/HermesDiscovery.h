#pragma once

#include "core/HermesProtocol.h"

#include <QHostAddress>
#include <QObject>
#include <QList>
#include <QTimer>
#include <QUdpSocket>

namespace MasterSDR {

struct HermesRadioInfo {
    QString ipAddress;
    uint16_t udpPort{1025};
    QString mac;
    QString gatewareVersion;
    uint8_t boardId{0};
    uint8_t numReceivers{1};
    bool isRunning{false};
    float temperature{0.0f};
};

class HermesDiscovery : public QObject {
    Q_OBJECT

public:
    explicit HermesDiscovery(QObject* parent = nullptr);
    ~HermesDiscovery() override;

    void startDiscovery();
    void stopDiscovery();
    QList<HermesRadioInfo> discoveredRadios() const;

signals:
    void radioDiscovered(const HermesRadioInfo& info);
    void radioLost(const QString& ipAddress);

private slots:
    void onReadyRead();
    void onStaleCheck();

private:
    void sendDiscoveryProbe();
    void sendDiscoveryProbe(const QHostAddress& bindAddr);

    QUdpSocket* m_socket{nullptr};
    QTimer* m_staleTimer{nullptr};
    QMap<QString, HermesRadioInfo> m_radios;
    QMap<QString, qint64> m_lastSeen;
    bool m_running{false};

    static constexpr int STALE_TIMEOUT_MS = 8000;
};

} // namespace MasterSDR
