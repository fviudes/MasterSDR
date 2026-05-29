#pragma once

#include "core/ISourceBackend.h"
#include "core/CivToVita49Bridge.h"

#include <QObject>
#include <QByteArray>
#include <atomic>
#include <cstdint>

namespace MasterSDR {

class MeterModel;
class AudioEngine;

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

    // ── Icom-specific connect (called by MainWindow) ──────────────────────

    void connectToRadio(const QString& host, uint16_t ctrlPort,
                        uint16_t serialPort, uint16_t audioPort,
                        const QString& username, const QString& password);

    // ── CI-V config ──────────────────────────────────────────

    void setCivAddress(uint8_t addr) { m_bridge.setCivAddress(addr); }

    // ── Extended controls ────────────────────────────────────

    void setSplit(bool on) { m_bridge.setSplit(on); }
    void setTxPower(int pct) { m_bridge.setTxPower(pct); }
    void setRfGain(int pct) { m_bridge.setRfGain(pct); }
    void setAttenuator(bool on) { m_bridge.setAttenuator(on); }
    void setPreamp(int level) { m_bridge.setPreamp(level); }

    // ── External pipeline setup ────────────────────────────────

    void setMeterModel(MeterModel* meterModel);
    void setAudioEngine(AudioEngine* audioEngine);

signals:
    // ── Same signals MainWindow.cpp expects from IcomIpConnection ──────────

    void squelchStatusUpdated(bool open);
    void txPowerUpdated(int pct);
    void rfGainUpdated(int pct);
    void splitUpdated(bool on);
    void preampUpdated(int level);
    void attenuatorUpdated(bool on);
    void bkInUpdated(int mode);
    void apfUpdated(int mode);

protected:
    void emitAudioDataReady(const QByteArray& pcm);
    void emitSpectrumDataReady(const QByteArray& scopeData);

private slots:
    void onBridgeConnected();
    void onBridgeDisconnected();
    void onBridgeStateChanged(CivToVita49Bridge::ConnectionState newState);
    void onBridgeFrequencyUpdated(uint64_t freqHz);
    void onBridgeModeUpdated(const QString& mode);
    void onBridgeSMeterUpdated(int level);
    void onBridgePttChanged(bool tx);

    void onBridgeSquelchStatusUpdated(bool open) { emit squelchStatusUpdated(open); }
    void onBridgeTxPowerUpdated(int pct) { emit txPowerUpdated(pct); }
    void onBridgeRfGainUpdated(int pct) { emit rfGainUpdated(pct); }
    void onBridgeSplitUpdated(bool on) { emit splitUpdated(on); }
    void onBridgePreampUpdated(int level) { emit preampUpdated(level); }
    void onBridgeAttenuatorUpdated(bool on) { emit attenuatorUpdated(on); }
    void onBridgeBkInUpdated(int mode) { emit bkInUpdated(mode); }
    void onBridgeApfUpdated(int mode) { emit apfUpdated(mode); }

    // VITA-49 diagnosis → data extraction
    void onVita49AudioPacket(const QByteArray& pkt);
    void onVita49FftPacket(const QByteArray& pkt);
    void onVita49MeterPacket(const QByteArray& pkt);

private:
    void wireBridgeSignals();
    void applyMeterDefinitions(MeterModel* model);

    CivToVita49Bridge m_bridge;

    MeterModel* m_meterModel{nullptr};
    AudioEngine* m_audioEngine{nullptr};

    QString m_host{QStringLiteral("192.168.1.100")};
    uint16_t m_ctrlPort{50001};
    uint16_t m_serialPort{50002};
    uint16_t m_audioPort{50003};
    QString m_username;
    QString m_password;

    std::atomic<State> m_state{State::Disconnected};
    bool m_meterDefsSent{false};
};

} // namespace MasterSDR
