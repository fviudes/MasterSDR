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

    void connectToRadio(const QString& host, uint16_t ctrlPort,
                        uint16_t serialPort, uint16_t audioPort,
                        const QString& username, const QString& password);
    void disconnectFromRadio();

    State state() const { return m_state.load(); }

    void sendCivCommand(uint8_t cmd, uint8_t subCmd, const QByteArray& data = QByteArray());

signals:
    void stateChanged(State newState);
    void connected();
    void disconnected();
    void errorOccurred(const QString& message);
    void frequencyUpdated(uint64_t freqHz);
    void modeUpdated(const QString& mode);

public slots:
    void setFrequency(uint64_t freqHz);

private slots:
    void onReadyRead();
    void onKeepAlive();
    void retryAuth();

private:
    void sendCtrlPacket(const QByteArray& payload);
    void sendSerialPacket(const QByteArray& civFrame);
    void sendAuthPacket();
    void processCtrlData(const QByteArray& data);
    void processSerialData(const QByteArray& data);

    QUdpSocket* m_socket{nullptr};
    QTimer* m_keepAliveTimer{nullptr};
    QTimer* m_authRetryTimer{nullptr};
    QHostAddress m_host;
    uint16_t m_ctrlPort{50001};
    uint16_t m_serialPort{50002};
    uint16_t m_audioPort{50003};
    QString m_username;
    QString m_password;
    std::atomic<State> m_state{State::Disconnected};

    uint8_t m_civAddr{IcomCivProtocol::DEFAULT_CI_V_ADDR};
    IcomCivProtocol m_civProto;
    bool m_authenticated{false};
    int m_authRetries{0};
    QString m_errorString;

    static constexpr int KEEPALIVE_MS = 1000;
    static constexpr int MAX_AUTH_RETRIES = 5;
    static constexpr uint8_t PKT_TYPE_AUTH = 0x01;
    static constexpr uint8_t PKT_TYPE_CIV  = 0x02;
    static constexpr uint8_t PKT_TYPE_IDLE = 0x03;
    static constexpr uint8_t PKT_TYPE_PING = 0x04;
};

} // namespace MasterSDR
