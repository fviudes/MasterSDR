#pragma once

#include "core/IcomCivProtocol.h"

#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QHostAddress>
#include <atomic>

namespace MasterSDR {

class IcomIpConnection : public QObject {
    Q_OBJECT

public:
    enum class State { Disconnected, Connecting, Authenticating, Connected, Error };
    Q_ENUM(State)

    explicit IcomIpConnection(QObject* parent = nullptr);
    ~IcomIpConnection() override;

    void init();
    void connectToRadio(const QString& host, uint16_t ctrlPort,
                        const QString& username, const QString& password);
    void disconnectFromRadio();

    State state() const { return m_state.load(); }
    QString errorString() const { return m_errorString; }
    QString radioId() const { return m_radioId; }

    void sendCivCommand(uint8_t cmd, uint8_t subCmd, const QByteArray& data = QByteArray());
    void sendCivCommand(const QByteArray& rawCivFrame);

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
    void setMode(const QString& mode);

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onError(QAbstractSocket::SocketError error);
    void onAuthTimerTimeout();
    void onWatchdogTimeout();

private:
    void processAuthChallenge(const QByteArray& data);
    void processCivPacket(const QByteArray& data);
    QByteArray wrapCivPacket(const QByteArray& civFrame);
    void sendPacket(const QByteArray& data);

    QTcpSocket* m_socket{nullptr};
    QTimer* m_authTimer{nullptr};
    QTimer* m_watchdogTimer{nullptr};
    QHostAddress m_host;
    uint16_t m_port{50001};
    QString m_username;
    QString m_password;
    QByteArray m_readBuffer;
    std::atomic<State> m_state{State::Disconnected};
    QString m_errorString;
    QString m_radioId;

    uint8_t m_civAddr{IcomCivProtocol::DEFAULT_CI_V_ADDR};
    IcomCivProtocol m_civProto;
    bool m_authenticated{false};
    uint32_t m_packetSeq{0};
    uint8_t m_authChallenge{0};

    static constexpr int AUTH_TIMEOUT_MS = 10000;
    static constexpr int WATCHDOG_INTERVAL_MS = 1000;
    static constexpr uint8_t STX1 = 0xFE;
    static constexpr uint8_t STX2 = 0xFE;
    static constexpr uint8_t HOST_ADDR = 0xE0;
};

} // namespace MasterSDR
