#pragma once

#include "core/HermesProtocol.h"
#include "core/HermesDiscovery.h"

#include <QObject>
#include <QTimer>
#include <QUdpSocket>
#include <QHostAddress>
#include <atomic>

namespace MasterSDR {

class HermesConnection : public QObject {
    Q_OBJECT

public:
    enum class State { Disconnected, Connecting, Connected, Error };
    Q_ENUM(State)

    explicit HermesConnection(QObject* parent = nullptr);
    ~HermesConnection() override;

    void init();
    void connectToRadio(const HermesRadioInfo& info);
    void disconnectFromRadio();

    State state() const { return m_state.load(); }
    QString errorString() const { return m_errorString; }

    void sendCommand(uint8_t addr, uint32_t data);
    void sendCommand(uint8_t addr, const QByteArray& data);

signals:
    void stateChanged(State newState);
    void connected();
    void disconnected();
    void errorOccurred(const QString& message);
    void discoveryReplyReceived(const HermesDiscoveryReply& reply);
    void responseReceived(uint8_t raddr, uint32_t rdata, bool ack);
    void temperatureUpdated(float tempC);
    void powerUpdated(uint16_t fwdPower, uint16_t revPower);
    void pttStateChanged(bool ptt);
    void cwKeyChanged(bool keyDown);

public slots:
    void setFrequency(uint32_t freqHz);
    void setRX1Frequency(uint32_t freqHz);
    void startRadio();
    void stopRadio();
    void setMox(bool active);

private slots:
    void onReadyRead();
    void onWatchdogTimeout();

private:
    void processReply(const QByteArray& data);

    QUdpSocket* m_socket{nullptr};
    QTimer* m_watchdogTimer{nullptr};
    QHostAddress m_radioAddress;
    uint16_t m_radioPort{1025};
    std::atomic<State> m_state{State::Disconnected};
    QString m_errorString;

    static constexpr int WATCHDOG_INTERVAL_MS = 500;
    uint32_t m_rxFreq{14074000};
    uint32_t m_txFreq{14074000};
};

} // namespace MasterSDR
