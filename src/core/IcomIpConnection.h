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
    void retryAuth() { }  // No-op; SYN/ACK handles retry

private:
    void processCtrlPacket(uint16_t type, uint16_t seq,
                           uint32_t sndId, uint32_t rcvId,
                           const QByteArray& payload);
    void processSerialData(const QByteArray& civPayload);
    void sendCivCommand(uint8_t cmd, uint8_t subCmd, const QByteArray& data = QByteArray());

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

    uint32_t m_localId{0};
    uint32_t m_radioId{0};
    uint32_t m_radioSerialPort{0};
    uint32_t m_radioAudioPort{0};
    uint16_t m_seq{0};
    bool m_authenticated{false};
    int m_authRetries{0};
    QString m_errorString;

    uint8_t m_civAddr{IcomCivProtocol::DEFAULT_CI_V_ADDR};
    IcomCivProtocol m_civProto;

    static constexpr int KEEPALIVE_MS = 3000;
    static constexpr int MAX_AUTH_RETRIES = 5;
};

} // namespace MasterSDR
