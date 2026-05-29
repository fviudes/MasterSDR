#pragma once

#include "core/ISourceBackend.h"
#include "core/CivToVita49Bridge.h"

#include <QUdpSocket>
#include <QTimer>
#include <QHostAddress>
#include <QByteArray>
#include <atomic>
#include <cstdint>

namespace MasterSDR {

class PanadapterStream;
class MeterModel;

// IcomIpBackend integrates an Icom IP (LAN) radio into MasterSDR via
// the CI-V → VITA-49 translation bridge. It presents the standard
// ISourceBackend interface to SourceManager while internally routing
// all radio data through VITA-49 to PanadapterStream and MeterModel.
//
// Pipeline:
//
//   Icom Radio (UDP 50001/50002/50003)
//        │ CI-V + PCM + Scope
//   ┌────▼─────────────────────────┐
//   │  CivToVita49Bridge           │  CI-V → VITA-49 translation
//   │  · VITA-49 audio (0x03E3)   │
//   │  · VITA-49 FFT   (0x8003)   │
//   │  · VITA-49 meter (0x8002)   │
//   │  · Status line generation    │
//   └────┬─────────────────────────┘
//        │ loopback UDP (localhost)
//   ┌────▼─────────────────────────┐
//   │  PanadapterStream (existing) │  core MasterSDR
//   │  MeterModel (existing)       │  unchanged
//   │  AudioEngine (existing)      │
//   └──────────────────────────────┘
//
class IcomIpBackend : public ISourceBackend {
    Q_OBJECT

public:
    explicit IcomIpBackend(QObject* parent = nullptr);
    ~IcomIpBackend() override;

    // ── ISourceBackend interface ─────────────────────────────

    void connectToRadio() override;
    void disconnectFromRadio() override;
    State state() const override { return m_state.load(); }
    Type type() const override { return Type::IcomIp; }

    void setFrequency(uint64_t freqHz) override;
    uint64_t frequency() const override { return m_bridge.frequency(); }

    void setMode(const QString& mode) override;
    QString mode() const override { return m_bridge.mode(); }

    void setPtt(bool tx) override;
    bool isPtt() const override { return m_bridge.isPtt(); }
    int sMeterLevel() const override { return m_bridge.sMeterLevel(); }

    // ── Extended controls (Icom-specific) ────────────────────

    void setSplit(bool on) { m_bridge.setSplit(on); }
    void setTxPower(int pct) { m_bridge.setTxPower(pct); }
    void setRfGain(int pct) { m_bridge.setRfGain(pct); }
    void setAttenuator(bool on) { m_bridge.setAttenuator(on); }
    void setPreamp(int level) { m_bridge.setPreamp(level); }

    // ── Configuration ────────────────────────────────────────

    // Radio connection parameters
    void setHost(const QString& host) { m_host = host; }
    void setCtrlPort(uint16_t port) { m_ctrlPort = port; }
    void setSerialPort(uint16_t port) { m_serialPort = port; }
    void setAudioPort(uint16_t port) { m_audioPort = port; }
    void setCivAddress(uint8_t addr) { m_bridge.setCivAddress(addr); }

    // VITA-49/panadapter config
    void setPanCenter(uint64_t centerHz) { m_bridge.setPanCenter(centerHz); }
    void setPanSpan(uint32_t spanHz) { m_bridge.setPanSpan(spanHz); }

    // Set the PanadapterStream to feed VITA-49 data into.
    // The bridge will send synthesized VITA-49 packets to the stream's loopback port.
    void setPanadapterStream(PanadapterStream* stream);

    // Add a MeterModel to receive synthetic meter definitions.
    // Called before connectToRadio().
    void setMeterModel(MeterModel* meterModel);

private slots:
    void onBridgeConnected();
    void onBridgeDisconnected();
    void onBridgeStateChanged(CivToVita49Bridge::ConnectionState newState);
    void onBridgeFrequencyUpdated(uint64_t freqHz);
    void onBridgeModeUpdated(const QString& mode);
    void onBridgeSMeterUpdated(int level);
    void onBridgePttChanged(bool tx);

    // Forwarded VITA-49 diagnostic signals → main thread
    void onVita49AudioPacket(const QByteArray& pkt);
    void onVita49FftPacket(const QByteArray& pkt);
    void onVita49MeterPacket(const QByteArray& pkt);

private:
    void wireBridgeSignals();
    void applyMeterDefinitions(MeterModel* model);
    void applyPanadapterDefinitions();

    CivToVita49Bridge m_bridge;

    QString m_host{QStringLiteral("192.168.1.100")};
    uint16_t m_ctrlPort{50001};
    uint16_t m_serialPort{50002};
    uint16_t m_audioPort{50003};

    std::atomic<State> m_state{State::Disconnected};

    // External references (NOT owned)
    PanadapterStream* m_panStream{nullptr};
    MeterModel* m_meterModel{nullptr};

    // Meter definitions sent flag
    bool m_meterDefsSent{false};
};

} // namespace MasterSDR
