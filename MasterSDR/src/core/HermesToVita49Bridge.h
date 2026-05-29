#pragma once

#include <QObject>
#include <QUdpSocket>
#include <QTimer>
#include <QHostAddress>
#include <QByteArray>
#include <QElapsedTimer>
#include <QVector>
#include <atomic>
#include <cstdint>

namespace MasterSDR {

class PanadapterStream;

namespace HermesMeterIndex {
    static constexpr quint16 TEMPERATURE    = 0;
    static constexpr quint16 FWD_POWER      = 1;
    static constexpr quint16 REV_POWER      = 2;
    static constexpr quint16 SWR            = 3;
    static constexpr quint16 ADC_CLIP       = 4;
    static constexpr quint16 PTT_STATE      = 5;
    static constexpr quint16 CW_KEY         = 6;
    static constexpr quint16 TX_FIFO        = 7;
    static constexpr quint16 TX_ON          = 8;
    static constexpr quint16 PA_EXT_TR      = 9;
    static constexpr quint16 BIAS_CURRENT   = 10;
    static constexpr quint16 SLICE0_LEVEL   = 16;  // S-meter (derived from wideband magnitude)
    static constexpr quint16 COUNT          = 17;
}

// Synthesizes VITA-49 packets from Hermes Lite 2 radio data,
// allowing an HL2 to appear as a native FlexRadio-equivalent source
// to MasterSDR's core pipeline (PanadapterStream, AudioEngine, MeterModel).
//
// Architecture:
//
//   Hermes Lite 2 (UDP 1025)
//          │ 1024-byte IQ + 60-byte status
//   ┌──────▼─────────────────────────┐
//   │  HermesToVita49Bridge          │
//   │  · HL2 protocol (EF FE)        │
//   │  · IQ → software FFT           │
//   │  · IQ → VITA-49 audio extract  │
//   │  · Status → VITA-49 meters     │
//   │  · Command: freq/PTT → NCO reg │
//   └──────┬─────────────────────────┘
//          │ VITA-49 UDP loopback
//   ┌──────▼─────────────────────────┐
//   │  PanadapterStream (core)       │  ← unchanged
//   │  AudioEngine / MeterModel      │
//   └─────────────────────────────── ┘
//
class HermesToVita49Bridge : public QObject {
    Q_OBJECT

public:
    enum class ConnectionState {
        Disconnected,
        Connecting,
        Handshaking,
        Connected,
        Error
    };
    Q_ENUM(ConnectionState)

    explicit HermesToVita49Bridge(QObject* parent = nullptr);
    ~HermesToVita49Bridge() override;

    // ── Connection ──────────────────────────────────────────

    void connectToRadio(const QString& host, uint16_t port = 1025);
    void disconnectFromRadio();

    void setVita49Target(uint16_t udpPort);

    // ── State ───────────────────────────────────────────────

    ConnectionState connectionState() const { return m_state; }
    bool isConnected() const { return m_connected; }
    QString errorString() const { return m_errorString; }

    uint64_t frequency() const { return m_rxFreq; }
    QString mode() const { return m_rxMode; }
    bool isPtt() const { return m_ptt; }
    int sMeterLevel() const { return m_sMeter; }
    QString radioModel() const { return m_radioModel; }
    QString gatewareVersion() const { return m_gatewareVersion; }
    QString macAddress() const { return m_macAddress; }
    float temperature() const { return m_temperature; }
    int forwardPower() const { return m_forwardPower; }
    int reversePower() const { return m_reversePower; }

    // ── Commands (application → HL2) ─────────────────────────

    void setFrequency(uint64_t freqHz);
    void setMode(const QString& mode);
    void setPtt(bool tx);
    void setTxDrive(uint8_t drive);
    void setLnaGain(uint8_t gain, bool stepAttenuator = false);

    // ── VITA-49 synthesis control ───────────────────────────

    void setVita49Enabled(bool on);
    bool isVita49Enabled() const { return m_vita49Enabled; }
    void setPanStreamId(quint32 streamId) { m_panStreamId = streamId; }
    void setPanPixelCount(int xPixels, int yPixels);

signals:
    void stateChanged(ConnectionState newState);
    void connected();
    void disconnected();
    void errorOccurred(const QString& message);

    void frequencyUpdated(uint64_t freqHz);
    void modeUpdated(const QString& mode);
    void sMeterUpdated(int level);
    void pttStateChanged(bool tx);
    void radioInfoUpdated();

    void temperatureUpdated(float tempC);
    void powerUpdated(uint16_t fwdPow, uint16_t revPow);
    void cwKeyChanged(bool keyDown);
    void adcClipCountUpdated(uint8_t count);

    // VITA-49 diagnostic
    void vita49AudioPacket(const QByteArray& packet);
    void vita49FftPacket(const QByteArray& packet);
    void vita49MeterPacket(const QByteArray& packet);

private slots:
    void onReadyRead();
    void onWatchdog();

private:
    // ── HL2 protocol constants ──────────────────────────────

    static constexpr uint8_t  PKT_DISCOVERY  = 0x02;
    static constexpr uint8_t  PKT_START      = 0x04;
    static constexpr uint8_t  PKT_COMMAND    = 0x05;
    static constexpr uint8_t  START_RADIO    = 0x01;
    static constexpr uint8_t  START_WIDEBAND  = 0x02;
    static constexpr uint8_t  START_DISABLE_WATCHDOG = 0x80;
    static constexpr uint8_t  ADDR_CONTROL   = 0x00;
    static constexpr uint8_t  ADDR_RX1_NCO   = 0x02;
    static constexpr uint8_t  ADDR_TX_DRIVE  = 0x09;
    static constexpr uint8_t  ADDR_LNA_GAIN  = 0x0A;
    static constexpr uint8_t  SPEED_48K      = 0x00;
    static constexpr uint8_t  SPEED_96K      = 0x01;
    static constexpr size_t   CMD_FRAME_SIZE = 64;
    static constexpr size_t   DISCOVERY_SIZE = 60;
    static constexpr size_t   IQ_BUFFER_SIZE = 1024;
    static constexpr size_t   IQ_SAMPLES     = 504;  // 1008 bytes / 2 bytes = 504 int16 samples
    static constexpr uint32_t ADC_RATE_HZ    = 76800000;
    static constexpr int      FFT_SIZE       = 1024;
    static constexpr int      WATCHDOG_MS    = 500;

    // ── HL2 protocol builders ───────────────────────────────

    QByteArray buildDiscoveryPacket();
    QByteArray buildStartPacket(uint8_t flags);
    QByteArray buildCommandPacket(uint8_t addr, uint32_t data);
    uint32_t buildControlWord(uint8_t speed, uint8_t numRx, bool duplex, bool mox);

    // ── Packet processing ──────────────────────────────────

    void processDatagram(const QByteArray& data);
    void processWidebandPacket(const QByteArray& data);
    void processStatusReply(const QByteArray& data);

    // ── IQ → software FFT ──────────────────────────────────

    void computeSoftwareFft(const QByteArray& widebandPayload);
    void computeAudioExtract(const QByteArray& widebandPayload);

    // ── VITA-49 synthesis ───────────────────────────────────

    void sendVita49ToLoopback(const QByteArray& pkt);

    // ── VITA-49 synthesis constants (public for Backend access) ──

public:
    static constexpr quint32 FLEX_OUI    = 0x001C2D;
    static constexpr quint32 FLEX_ICC    = 0x534C;
    static constexpr quint16 PCC_IF_DATA = 0x03E3;
    static constexpr quint16 PCC_FFT     = 0x8003;
    static constexpr quint16 PCC_METER   = 0x8002;
    static constexpr int VITA49_HEADER_BYTES = 28;

private:

    QByteArray buildVita49Header(quint16 pcc, quint32 streamId, quint32 payloadBytes);
    QByteArray buildVita49FftPacket(const QVector<float>& bins, quint16 startBin, quint16 totalBins);
    QByteArray buildVita49MeterPacket(const QVector<quint16>& ids, const QVector<qint16>& vals);
    void synthesizeVita49Meters();

    // ── Networking ──────────────────────────────────────────

    QUdpSocket* m_socket{nullptr};
    QTimer* m_watchdogTimer{nullptr};
    QHostAddress m_host;
    uint16_t m_radioPort{1025};
    uint16_t m_vita49Port{0};
    QHostAddress m_vita49Addr;

    bool m_connected{false};
    bool m_vita49Enabled{true};
    ConnectionState m_state{ConnectionState::Disconnected};
    QString m_errorString;

    // ── Radio identity ──────────────────────────────────────

    uint64_t m_rxFreq{14074000};
    QString m_rxMode{QStringLiteral("USB")};
    QString m_radioModel{QStringLiteral("Hermes Lite 2")};
    QString m_gatewareVersion;
    QString m_macAddress;
    bool m_ptt{false};
    int m_sMeter{0};

    // ── Telemetry (from 60-byte status) ─────────────────────

    float m_temperature{0.0f};
    uint16_t m_forwardPower{0};
    uint16_t m_reversePower{0};
    uint8_t m_adcClipCount{0};
    bool m_cwKey{false};
    bool m_txOn{false};
    bool m_paExtTr{false};
    bool m_paIntTr{false};
    float m_biasCurrent{0.0f};
    uint8_t m_boardId{0x06};  // HL2
    uint8_t m_numReceivers{1};

    // ── VITA-49 synthesis state ────────────────────────────

    quint32 m_panStreamId{0x40000000};
    int m_panXPixels{1024};
    int m_panYPixels{100};

    uint32_t m_fftFrameIndex{0};
    uint8_t m_audioSeq{0};
    uint8_t m_fftSeq{0};
    uint8_t m_meterSeq{0};

    // FFT working buffers
    QVector<float> m_fftInBuf;
    QVector<float> m_hanningWindow;

    QElapsedTimer m_elapsed;
};

} // namespace MasterSDR
