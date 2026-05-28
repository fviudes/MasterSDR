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

private:
    QByteArray buildPacket(uint16_t typeCode, uint16_t seq,
                            const QByteArray& payload = QByteArray());
    void sendCtrlPacket(uint16_t typeCode, uint16_t seq, const QByteArray& payload = QByteArray());
    void sendSerialPacket(uint16_t seq, const QByteArray& civFrame);
    void sendCivCommand(uint8_t cmd, uint8_t subCmd, const QByteArray& data = QByteArray());
    void processPacket(const QByteArray& data, quint16 senderPort);

    QUdpSocket* m_socket{nullptr};
    QTimer* m_keepAliveTimer{nullptr};
    QHostAddress m_host;
    uint16_t m_ctrlPort{50001};
    uint16_t m_serialPort{50002};
    uint16_t m_audioPort{50003};
    QString m_username;
    QString m_password;
    std::atomic<State> m_state{State::Disconnected};

    uint16_t m_sourcePort{0};
    uint16_t m_sourceId{0};
    uint16_t m_destPort{0};
    uint16_t m_destId{0};
    uint16_t m_seq{0};
    uint16_t m_pingSeq{0};
    bool m_connected{false};

    uint8_t m_civAddr{IcomCivProtocol::DEFAULT_CI_V_ADDR};
    IcomCivProtocol m_civProto;

    static constexpr uint16_t TYPE_DATA    = 0x00;
    static constexpr uint16_t TYPE_NACK    = 0x01;
    static constexpr uint16_t TYPE_SYN     = 0x03;
    static constexpr uint16_t TYPE_SYN_ACK = 0x04;
    static constexpr uint16_t TYPE_DISCON  = 0x05;
    static constexpr uint16_t TYPE_READY   = 0x06;
    static constexpr uint16_t TYPE_PING    = 0x07;
    static constexpr int KEEPALIVE_MS = 3000;
};

} // namespace MasterSDR
