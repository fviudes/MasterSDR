#pragma once

#include "core/IcomCivProtocol.h"
#include "core/ISourceBackend.h"

#include <QUdpSocket>
#include <QTimer>
#include <QHostAddress>
#include <atomic>

namespace MasterSDR {

class IcomIpConnection : public ISourceBackend {
    Q_OBJECT

public:
    explicit IcomIpConnection(QObject* parent = nullptr);
    ~IcomIpConnection() override;

    void connectToRadio(const QString& host, uint16_t ctrlPort,
                        uint16_t serialPort, uint16_t audioPort,
                        const QString& username, const QString& password);

    // ISourceBackend interface
    void connectToRadio() override { /* use parameterized version */ }
    void disconnectFromRadio() override;
    State state() const override { return m_state.load(); }
    Type type() const override { return Type::IcomIp; }
    void setFrequency(uint64_t freqHz) override;
    uint64_t frequency() const override { return m_rxFreq; }
    void setMode(const QString& mode) override;
    QString mode() const override { return m_rxMode; }
    void setPtt(bool tx) override;
    bool isPtt() const override { return m_ptt; }
    int sMeterLevel() const override { return m_sMeter; }

    void setCivAddress(uint8_t addr) { m_civProto.setCivAddress(addr); m_civAddr = addr; }

signals:
    void squelchStatusUpdated(bool open);
    void txPowerUpdated(int pct);
    void rfGainUpdated(int pct);
    void splitUpdated(bool on);
    void preampUpdated(int level);
    void attenuatorUpdated(bool on);
    void bkInUpdated(int mode);
    void apfUpdated(int mode);

private slots:
    void onReadyRead();
    void onKeepAlive();
    void onAudioReady();

private:
    QByteArray buildPacket(uint16_t typeCode, uint16_t seq,
                            const QByteArray& payload = QByteArray());
    QByteArray buildPacketFor(uint16_t typeCode, uint16_t seq,
                               uint16_t dstPort, uint16_t dstId,
                               const QByteArray& payload = QByteArray());
    void sendCtrlPacket(uint16_t typeCode, uint16_t seq, const QByteArray& payload = QByteArray());
    void sendSerialPacket(uint16_t seq, const QByteArray& civFrame);
    void sendCivCommand(uint8_t cmd, uint8_t subCmd, const QByteArray& data = QByteArray());
    void processPacket(const QByteArray& data, quint16 senderPort);

    QUdpSocket* m_socket{nullptr};
    QUdpSocket* m_audioSocket{nullptr};
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

    uint64_t m_rxFreq{14074000};
    QString m_rxMode{QLatin1String("USB")};
    bool m_ptt{false};
    int m_sMeter{0};
    int m_txPower{50};
    int m_rfGain{100};
    bool m_split{false};
    int m_preamp{0};
    bool m_attenuator{false};
    int m_bkInMode{0};
    int m_apfMode{0};

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
