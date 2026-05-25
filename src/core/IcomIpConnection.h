#pragma once

#include "core/IcomCivProtocol.h"

#include <QObject>
#include <QUdpSocket>
#include <QTimer>
#include <QHostAddress>
#include <atomic>

namespace MasterSDR {

class IcomIpConnection : public QObject {
    Q_OBJECT

public:
    enum class State { Disconnected, Connecting, Connected, Error };
    Q_ENUM(State)

    explicit IcomIpConnection(QObject* parent = nullptr);
    ~IcomIpConnection() override;

    void init();
    void connectToRadio(const QString& host, uint16_t ctrlPort,
                        uint16_t serialPort, uint16_t audioPort,
                        const QString& username, const QString& password);
    void disconnectFromRadio();

    State state() const { return m_state.load(); }
    QString errorString() const { return m_errorString; }

    void sendCivCommand(uint8_t cmd, uint8_t subCmd, const QByteArray& data = QByteArray());

signals:
    void stateChanged(State newState);
    void connected();
    void disconnected();
    void errorOccurred(const QString& message);
    void civResponseReceived(const CivResponse& response);
    void frequencyUpdated(uint64_t freqHz);
    void modeUpdated(const QString& mode);

public slots:
    void setFrequency(uint64_t freqHz);

private slots:
    void onReadyRead();
    void onWatchdogTimeout();

private:
    void processCivResponse(const QByteArray& data);
    void sendPacket(const QByteArray& data);

    QUdpSocket* m_socket{nullptr};
    QTimer* m_watchdogTimer{nullptr};
    QHostAddress m_host;
    uint16_t m_ctrlPort{50001};
    uint16_t m_serialPort{50002};
    uint16_t m_audioPort{50003};
    QString m_username;
    QString m_password;
    QByteArray m_readBuffer;
    std::atomic<State> m_state{State::Disconnected};
    QString m_errorString;

    uint8_t m_civAddr{IcomCivProtocol::DEFAULT_CI_V_ADDR};
    IcomCivProtocol m_civProto;
    uint32_t m_packetSeq{0};

    static constexpr int WATCHDOG_INTERVAL_MS = 500;
    static constexpr uint8_t STX1 = 0xFE;
    static constexpr uint8_t STX2 = 0xFE;
};

} // namespace MasterSDR
