#pragma once

#include "core/HermesProtocol.h"
#include "core/HermesDiscovery.h"
#include "core/ISourceBackend.h"

#include <QObject>
#include <QTimer>
#include <QUdpSocket>
#include <QHostAddress>
#include <atomic>

namespace MasterSDR {

class HermesConnection : public ISourceBackend {
    Q_OBJECT

public:
    explicit HermesConnection(QObject* parent = nullptr);
    ~HermesConnection() override;

    void init();

    // ISourceBackend interface
    void connectToRadio() override { /* use connectToRadio(info) */ }
    void disconnectFromRadio() override;
    State state() const override { return static_cast<State>(m_connState.load()); }
    Type type() const override { return Type::Hermes; }
    void setFrequency(uint64_t freqHz) override;
    uint64_t frequency() const override { return m_rxFreq; }
    void setPtt(bool tx) override;
    bool isPtt() const override { return m_ptt; }

    // Hermes-specific
    void connectToRadio(const HermesRadioInfo& info);
    void setRX1Frequency(uint32_t freqHz);
    void sendCommand(uint8_t addr, uint32_t data);
    void sendCommand(uint8_t addr, const QByteArray& data);

signals:
    void discoveryReplyReceived(const HermesDiscoveryReply& reply);
    void responseReceived(uint8_t raddr, uint32_t rdata, bool ack);
    void temperatureUpdated(float tempC);
    void powerUpdated(uint16_t fwdPower, uint16_t revPower);
    void cwKeyChanged(bool keyDown);

private slots:
    void onReadyRead();
    void onWatchdogTimeout();

private:
    void processReply(const QByteArray& data);

    QUdpSocket* m_socket{nullptr};
    QTimer* m_watchdogTimer{nullptr};
    QHostAddress m_radioAddress;
    uint16_t m_radioPort{1025};
    std::atomic<int> m_connState{0};  // ISourceBackend::State
    QString m_errorString;

    static constexpr int WATCHDOG_INTERVAL_MS = 500;
    uint32_t m_rxFreq{14074000};
    uint32_t m_txFreq{14074000};
    bool m_ptt{false};
};

} // namespace MasterSDR
