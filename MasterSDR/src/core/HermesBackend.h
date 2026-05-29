#pragma once

#include "core/ISourceBackend.h"
#include "core/HermesToVita49Bridge.h"

#include <QObject>
#include <QByteArray>
#include <QString>
#include <atomic>
#include <cstdint>

namespace MasterSDR {

class PanadapterStream;
class MeterModel;

// HermesBackend integrates a Hermes Lite 2 SDR into MasterSDR via
// the HL2 → VITA-49 translation bridge. It presents the standard
// ISourceBackend interface to SourceManager while internally routing
// all radio data through VITA-49 to PanadapterStream and MeterModel.
//
// Bidirectional full integration:
//
//   HL2 (UDP 1025)                  MasterSDR UI
//        │                               │
//   IQ data + status                 user commands
//        │                               │
//   ┌────▼──────────────────────┐  ┌─────▼──────────────┐
//   │ HermesToVita49Bridge      │  │ ISourceBackend      │
//   │ · FW → BW IQ → FFT/Audio  │  │ · setFrequency()    │
//   │ · status → meters         │  │ · setPtt()          │
//   │ · freq/PTT ← commands     │  │ · setMode()         │
//   └────┬──────────────────────┘  └────────────────────┘
//        │ VITA-49 loopback
//   ┌────▼─────────────────────┐
//   │ PanadapterStream /       │  core unchanged
//   │ AudioEngine / MeterModel │
//   └──────────────────────────┘
//
class HermesBackend : public ISourceBackend {
    Q_OBJECT

public:
    explicit HermesBackend(QObject* parent = nullptr);
    ~HermesBackend() override;

    // ── ISourceBackend interface ─────────────────────────────

    void connectToRadio() override;
    void disconnectFromRadio() override;
    State state() const override { return m_state.load(); }
    Type type() const override { return Type::Hermes; }

    void setFrequency(uint64_t freqHz) override;
    uint64_t frequency() const override { return m_bridge.frequency(); }

    void setMode(const QString& mode) override;
    QString mode() const override { return m_bridge.mode(); }

    void setPtt(bool tx) override;
    bool isPtt() const override { return m_bridge.isPtt(); }

    // ── Extended controls (Hermes-specific) ─────────────────

    void setTxDrive(uint8_t drive) { m_bridge.setTxDrive(drive); }
    void setLnaGain(uint8_t gain, bool stepAttenuator = false) { m_bridge.setLnaGain(gain, stepAttenuator); }

    // ── Telemetry accessors ─────────────────────────────────

    float temperature() const { return m_bridge.temperature(); }
    int forwardPower() const { return m_bridge.forwardPower(); }
    int reversePower() const { return m_bridge.reversePower(); }
    QString gatewareVersion() const { return m_bridge.gatewareVersion(); }
    QString macAddress() const { return m_bridge.macAddress(); }

    // ── Configuration ────────────────────────────────────────

    void setHost(const QString& host) { m_host = host; }
    void setRadioPort(uint16_t port) { m_radioPort = port; }

    // VITA-49/panadapter config
    void setPanadapterStream(PanadapterStream* stream);
    void setMeterModel(MeterModel* meterModel);

private slots:
    void onBridgeConnected();
    void onBridgeDisconnected();
    void onBridgeStateChanged(HermesToVita49Bridge::ConnectionState newState);
    void onBridgeFrequencyUpdated(uint64_t freqHz);
    void onBridgeModeUpdated(const QString& mode);
    void onBridgePttChanged(bool tx);
    void onBridgeSMeterUpdated(int level);
    void onBridgeTemperatureUpdated(float tempC);
    void onBridgePowerUpdated(uint16_t fwdPow, uint16_t revPow);
    void onVita49AudioPacket(const QByteArray& pkt);
    void onVita49FftPacket(const QByteArray& pkt);
    void onVita49MeterPacket(const QByteArray& pkt);

private:
    void wireBridgeSignals();
    void applyMeterDefinitions(MeterModel* model);
    void applyPanadapterDefinitions();

    HermesToVita49Bridge m_bridge;

    QString m_host{QStringLiteral("192.168.10.3")};
    uint16_t m_radioPort{1025};

    std::atomic<State> m_state{State::Disconnected};

    PanadapterStream* m_panStream{nullptr};
    MeterModel* m_meterModel{nullptr};

    bool m_meterDefsSent{false};
};

} // namespace MasterSDR
