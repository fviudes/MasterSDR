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

class IcomCivProtocol;
class PanadapterStream;

// Meter indices for Icom radios (synthesized to match FlexRadio meter layout).
// These are sent as VITA-49 PCC 0x8002 and correspond to MeterDef entries
// defined via synthetic "meter" status lines.
namespace IcomMeterIndex {
    static constexpr quint16 SLICE0_LEVEL   = 0;   // S-meter (dBm) via CI-V 0x15/0x02
    static constexpr quint16 SLICE0_SQUELCH = 1;   // Squelch open flag (0/1)
    static constexpr quint16 TX_POWER       = 10;  // TX Power (%) via CI-V 0x14/0x0A
    static constexpr quint16 RF_GAIN        = 11;  // RF Gain (%) via CI-V 0x14/0x02
    static constexpr quint16 MIC_LEVEL      = 12;  // Mic gain via CI-V 0x14/0x0B
    static constexpr quint16 HW_ALC         = 13;  // Placeholder: CI-V s-meter diff usage
    static constexpr quint16 PA_TEMP        = 20;  // Temperature (not directly from CI-V; placeholder)
    static constexpr quint16 SUPPLY_VOLTS   = 21;  // Supply voltage placeholder
    static constexpr quint16 COUNT          = 22;
}

// Synthesizes VITA-49 packets from Icom CI-V radio data, allowing an
// Icom IP (LAN) radio to appear as a native FlexRadio-equivalent source
// to MasterSDR's core pipeline (PanadapterStream, AudioEngine, MeterModel).
//
// Architecture:
//
//   Icom IP Radio (UDP 50001/50002/50003)
//           │
//   ┌───────▼────────────────────┐
//   │  CivToVita49Bridge         │
//   │  · Icom IP handshake       │
//   │  · CI-V command parsing    │
//   │  · VITA-49 packet synth    │
//   │  · Status line generation  │
//   │  · Bidirectional cmd trans │
//   └───────┬────────────────────┘
//           │ VITA-49 UDP loopback
//   ┌───────▼────────────────────┐
//   │  PanadapterStream          │  ← core MasterSDR (unchanged)
//   │  AudioEngine / MeterModel  │
//   └────────────────────────────┘
//
// VITA-49 loopback: bridge sends PCC 0x03E3 (audio), 0x8003 (FFT),
// 0x8002 (meters) to PanadapterStream's bound port on localhost.
// Spectrum scope data from Icom audio port is repacked as VITA-49 FFT bins.
//
class CivToVita49Bridge : public QObject {
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

    explicit CivToVita49Bridge(QObject* parent = nullptr);
    ~CivToVita49Bridge() override;

    // ── Connection ──────────────────────────────────────────

    // Address of Icom radio. ctrlPort=50001, serialPort=50002, audioPort=50003.
    void connectToRadio(const QString& host,
                        uint16_t ctrlPort = 50001,
                        uint16_t serialPort = 50002,
                        uint16_t audioPort = 50003);

    void disconnectFromRadio();

    // Set a known CI-V address for the target radio model
    void setCivAddress(uint8_t addr);

    // Set the local loopback VITA-49 target (where PanadapterStream listens)
    void setVita49Target(uint16_t udpPort);

    // ── State ───────────────────────────────────────────────

    ConnectionState connectionState() const { return m_state; }
    bool isConnected() const { return m_connected; }
    QString errorString() const { return m_errorString; }

    // ── Radio identity ──────────────────────────────────────
    uint64_t frequency() const { return m_rxFreq; }
    QString mode() const { return m_rxMode; }
    bool isPtt() const { return m_ptt; }
    int sMeterLevel() const { return m_sMeter; }
    QString radioModel() const { return m_radioModel; }

    // ── Commands (from application → Icom) ──────────────────

    void setFrequency(uint64_t freqHz);
    void setMode(const QString& mode);
    void setPtt(bool tx);
    void setSplit(bool on);
    void setTxPower(int pct);       // 0-100
    void setRfGain(int pct);        // 0-100
    void setAttenuator(bool on);
    void setPreamp(int level);      // 0=off, 1=10dB, 2=20dB

    // ── VITA-49 synthesis control ───────────────────────────

    // Enable/disable VITA-49 auto-synthesis. When enabled,
    // audio, scope, and meter data are automatically formatted
    // and forwarded to the PanadapterStream loopback port.
    void setVita49Enabled(bool on);
    bool isVita49Enabled() const { return m_vita49Enabled; }

    // Configure the simulated panadapter stream ID
    void setPanStreamId(quint32 streamId) { m_panStreamId = streamId; }

    // Set panadapter frequency range for FFT bin mapping
    void setPanCenter(uint64_t centerHz);
    void setPanSpan(uint32_t spanHz);
    void setPanPixelCount(int xPixels, int yPixels);

    // Set audio sample rate (Icom sends 8kHz or 16kHz depending on model)
    void setIcomAudioSampleRate(int sampleRateHz) { m_audioSampleRate = sampleRateHz; }

signals:
    // ── Direct state signals (compatible with RendererUI widgets) ──

    void stateChanged(ConnectionState newState);
    void connected();
    void disconnected();
    void errorOccurred(const QString& message);

    void frequencyUpdated(uint64_t freqHz);
    void modeUpdated(const QString& mode);
    void sMeterUpdated(int level);
    void pttStateChanged(bool tx);
    void radioInfoUpdated();

    // Splinter status
    void splitUpdated(bool on);
    void txPowerUpdated(int pct);
    void rfGainUpdated(int pct);
    void preampUpdated(int level);
    void attenuatorUpdated(bool on);
    void bkInUpdated(int mode);
    void apfUpdated(int mode);

    // ── VITA-49 data forwarded (for diagnostic use) ──

    // Emitted when VITA-49 formatted audio is ready
    void vita49AudioPacket(const QByteArray& packet);
    // Emitted when VITA-49 FFT/spectrum packet is ready
    void vita49FftPacket(const QByteArray& packet);
    // Emitted when VITA-49 meter packet is ready
    void vita49MeterPacket(const QByteArray& packet);

private slots:
    void onCtrlReadyRead();
    void onAudioReadyRead();
    void onKeepAlive();

private:
    // ── Icom IP packet protocol (16-byte header) ────────────

    static constexpr uint16_t TYPE_DATA    = 0x00;
    static constexpr uint16_t TYPE_NACK    = 0x01;
    static constexpr uint16_t TYPE_SYN     = 0x03;
    static constexpr uint16_t TYPE_SYN_ACK = 0x04;
    static constexpr uint16_t TYPE_DISCON  = 0x05;
    static constexpr uint16_t TYPE_READY   = 0x06;
    static constexpr uint16_t TYPE_PING    = 0x07;
    static constexpr int HEADER_SIZE = 16;
    static constexpr int KEEPALIVE_MS = 3000;

    QByteArray buildIcomPacket(uint16_t type, uint16_t seq,
                                uint16_t dstPort, uint16_t dstId,
                                const QByteArray& payload = {});
    void sendIcomCtrlPacket(uint16_t type, uint16_t seq,
                             uint16_t dstPort, uint16_t dstId,
                             const QByteArray& payload = {});
    void sendIcomCivCommand(uint8_t cmd, uint8_t subCmd = 0,
                             const QByteArray& data = {});

    // ── CI-V response processing ────────────────────────────

    void processIcomPacket(const QByteArray& data, quint16 senderPort);
    void parseCivResponse(const QByteArray& data);
    void handleCivResponse(uint8_t cmd, uint8_t subCmd, const QByteArray& data);

    // ── VITA-49 synthesis ──────────────────────────────────

    void sendVita49ToLoopback(const QByteArray& pkt);
    void synthesizeVita49Audio(const QByteArray& rawPcm);
    void synthesizeVita49Fft(const QByteArray& scopeData);
    void synthesizeVita49Meters();

public:
    static constexpr quint32 FLEX_OUI    = 0x001C2D;
    static constexpr quint32 FLEX_ICC    = 0x534C;
    static constexpr quint16 PCC_IF_DATA = 0x03E3;
    static constexpr quint16 PCC_FFT     = 0x8003;
    static constexpr quint16 PCC_METER   = 0x8002;
    static constexpr int VITA49_HEADER_BYTES = 28;

private:

    QByteArray buildVita49Header(quint16 pcc, quint32 streamId, quint32 payloadBytes);
    QByteArray buildVita49AudioPacket(const QByteArray& pcmPayload);
    QByteArray buildVita49FftPacket(const QVector<float>& bins, quint16 startBin, quint16 totalBins);
    QByteArray buildVita49MeterPacket(const QVector<quint16>& ids, const QVector<qint16>& vals);

    // ── Status line generation ──────────────────────────────

    QString buildSliceStatus(int index) const;

    // ── BCD conversion helpers ──────────────────────────────

    static uint64_t decodeBcdFreq(const uint8_t* data, int len);
    static QByteArray encodeBcdFreq(uint64_t freqHz);
    static QString modeByteToString(uint8_t modeByte);
    static uint8_t modeStringToByte(const QString& mode);
    static QString rigIdToModel(uint8_t rigId);

    // ── Networking ──────────────────────────────────────────

    QUdpSocket* m_ctrlSocket{nullptr};
    QUdpSocket* m_audioSocket{nullptr};
    QHostAddress m_host;
    uint16_t m_ctrlPort{50001};
    uint16_t m_serialPort{50002};
    uint16_t m_audioPort{50003};

    // VITA-49 loopback
    uint16_t m_vita49Port{0};
    QHostAddress m_vita49Addr;  // localhost

    uint16_t m_sourcePort{0};
    uint16_t m_sourceId{0};
    uint16_t m_destPort{0};
    uint16_t m_destId{0};
    uint16_t m_seq{0};
    uint16_t m_pingSeq{0};

    bool m_connected{false};
    bool m_vita49Enabled{true};
    ConnectionState m_state{ConnectionState::Disconnected};
    QString m_errorString;

    QTimer* m_keepAliveTimer{nullptr};

    // ── CI-V protocol ───────────────────────────────────────

    uint8_t m_civAddr{0xA4};  // Default IC-705/IC-7300

    // ── Radio state cache ───────────────────────────────────

    uint64_t m_rxFreq{14074000};
    QString m_rxMode{QStringLiteral("USB")};
    QString m_radioModel{QStringLiteral("ICOM")};
    bool m_ptt{false};
    int m_sMeter{0};
    int m_squelchOpen{0};
    int m_txPower{50};
    int m_rfGain{100};
    bool m_split{false};
    bool m_attenuator{false};
    int m_preamp{0};
    int m_bkInMode{0};
    int m_apfMode{0};

    // ── VITA-49 synthesis state ─────────────────────────────

    quint32 m_panStreamId{0x40000000};
    uint64_t m_panCenterHz{14074000};
    uint32_t m_panSpanHz{192000};
    int m_panXPixels{1024};
    int m_panYPixels{100};        // Icom scope height (usually 80-100)
    int m_audioSampleRate{8000};  // IC-705 default

    // FFT frame assembly
    uint32_t m_fftFrameIndex{0};
    int m_scopeBinsPerPacket{475};

    // Audio sequence counter (4-bit)
    uint8_t m_audioSeq{0};
    uint8_t m_fftSeq{0};
    uint8_t m_meterSeq{0};

    // Timestamps
    QElapsedTimer m_elapsed;
};

} // namespace MasterSDR
