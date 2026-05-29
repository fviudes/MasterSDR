#include "core/HermesBackend.h"
#include "core/PanadapterStream.h"
#include "core/LogManager.h"
#include "models/MeterModel.h"

#include <QDebug>

namespace MasterSDR {

// ────────────────────────────────────────────────────────────────
//  Construction
// ────────────────────────────────────────────────────────────────

HermesBackend::HermesBackend(QObject* parent)
    : ISourceBackend(parent)
{
    wireBridgeSignals();
}

HermesBackend::~HermesBackend()
{
    disconnectFromRadio();
}

// ────────────────────────────────────────────────────────────────
//  Signal wiring: HermesToVita49Bridge → HermesBackend → ISourceBackend
// ────────────────────────────────────────────────────────────────

void HermesBackend::wireBridgeSignals()
{
    // Connection lifecycle
    connect(&m_bridge, &HermesToVita49Bridge::connected,
            this, &HermesBackend::onBridgeConnected);
    connect(&m_bridge, &HermesToVita49Bridge::disconnected,
            this, &HermesBackend::onBridgeDisconnected);
    connect(&m_bridge, &HermesToVita49Bridge::stateChanged,
            this, &HermesBackend::onBridgeStateChanged);
    connect(&m_bridge, &HermesToVita49Bridge::errorOccurred,
            this, &HermesBackend::errorOccurred);

    // ISourceBackend signals → forwarded directly
    connect(&m_bridge, &HermesToVita49Bridge::frequencyUpdated,
            this, &HermesBackend::onBridgeFrequencyUpdated);
    connect(&m_bridge, &HermesToVita49Bridge::modeUpdated,
            this, &HermesBackend::onBridgeModeUpdated);
    connect(&m_bridge, &HermesToVita49Bridge::sMeterUpdated,
            this, &HermesBackend::onBridgeSMeterUpdated);
    connect(&m_bridge, &HermesToVita49Bridge::pttStateChanged,
            this, &HermesBackend::onBridgePttChanged);
    connect(&m_bridge, &HermesToVita49Bridge::radioInfoUpdated,
            this, &HermesBackend::radioInfoUpdated);

    // Telemetry
    connect(&m_bridge, &HermesToVita49Bridge::temperatureUpdated,
            this, &HermesBackend::onBridgeTemperatureUpdated);
    connect(&m_bridge, &HermesToVita49Bridge::powerUpdated,
            this, &HermesBackend::onBridgePowerUpdated);

    // VITA-49 diagnostic → PCM audio extraction
    connect(&m_bridge, &HermesToVita49Bridge::vita49AudioPacket,
            this, &HermesBackend::onVita49AudioPacket);
    connect(&m_bridge, &HermesToVita49Bridge::vita49FftPacket,
            this, &HermesBackend::onVita49FftPacket);
    connect(&m_bridge, &HermesToVita49Bridge::vita49MeterPacket,
            this, &HermesBackend::onVita49MeterPacket);
}

// ────────────────────────────────────────────────────────────────
//  Connection lifecycle
// ────────────────────────────────────────────────────────────────

void HermesBackend::connectToRadio()
{
    if (m_state.load() == State::Connected || m_state.load() == State::Connecting) {
        return;
    }

    m_meterDefsSent = false;

    // Set up VITA-49 loopback target
    if (m_panStream) {
        quint16 streamPort = m_panStream->localPort();
        if (streamPort > 0) {
            m_bridge.setVita49Target(streamPort);
            qCDebug(lcConnection) << "HermesBackend: VITA-49 loopback on port" << streamPort;
        }
    }

    // Wire meter model before connecting
    if (m_meterModel) {
        applyMeterDefinitions(m_meterModel);
        m_meterDefsSent = true;
    }

    // Apply panadapter stream config
    if (m_panStream) {
        applyPanadapterDefinitions();
    }

    m_state.store(State::Connecting);
    emit stateChanged(State::Connecting);

    m_bridge.connectToRadio(m_host, m_radioPort);
}

void HermesBackend::disconnectFromRadio()
{
    m_bridge.disconnectFromRadio();
}

// ────────────────────────────────────────────────────────────────
//  ISourceBackend command dispatch (bidirectional)
// ────────────────────────────────────────────────────────────────

void HermesBackend::setFrequency(uint64_t freqHz)
{
    m_bridge.setFrequency(freqHz);
}

void HermesBackend::setMode(const QString& mode)
{
    m_bridge.setMode(mode);
}

void HermesBackend::setPtt(bool tx)
{
    m_bridge.setPtt(tx);
}

// ────────────────────────────────────────────────────────────────
//  PanadapterStream setup
// ────────────────────────────────────────────────────────────────

void HermesBackend::setPanadapterStream(PanadapterStream* stream)
{
    m_panStream = stream;
}

void HermesBackend::setMeterModel(MeterModel* meterModel)
{
    m_meterModel = meterModel;
}

void HermesBackend::applyPanadapterDefinitions()
{
    if (!m_panStream) return;

    quint32 panId = 0x40000000;
    m_panStream->registerPanStream(panId);

    // HL2 scope: ~504 FFT bins at 48kHz bandwidth, typically 100 pixels height
    m_panStream->setYPixels(panId, 100);
    m_panStream->setDbmRange(panId, -130.0f, -20.0f);

    qCDebug(lcConnection) << "HermesBackend: registered pan stream 0x40000000";
}

// ────────────────────────────────────────────────────────────────
//  Meter definition injection into MeterModel
// ────────────────────────────────────────────────────────────────

void HermesBackend::applyMeterDefinitions(MeterModel* model)
{
    // Temperature (RAD/PATEMP) — matches FlexRadio meter layout
    model->defineMeter(MeterDef{
        .index       = HermesMeterIndex::TEMPERATURE,
        .source      = QStringLiteral("RAD"),
        .sourceIndex = 0,
        .name        = QStringLiteral("PATEMP"),
        .unit        = QStringLiteral("degC"),
        .low         = 0.0,
        .high        = 100.0,
        .description = QStringLiteral("HL2 PA temperature (VITA-49 synthesized)"),
    });

    // Forward power (TX/FWDPWR)
    model->defineMeter(MeterDef{
        .index       = HermesMeterIndex::FWD_POWER,
        .source      = QStringLiteral("TX"),
        .sourceIndex = 0,
        .name        = QStringLiteral("FWDPWR"),
        .unit        = QStringLiteral("Watts"),
        .low         = 0.0,
        .high        = 10.0,
        .description = QStringLiteral("HL2 forward power (VITA-49 synthesized)"),
    });

    // Reverse power (TX/REVPWR)
    model->defineMeter(MeterDef{
        .index       = HermesMeterIndex::REV_POWER,
        .source      = QStringLiteral("TX"),
        .sourceIndex = 0,
        .name        = QStringLiteral("REVPWR"),
        .unit        = QStringLiteral("Watts"),
        .low         = 0.0,
        .high        = 10.0,
        .description = QStringLiteral("HL2 reverse power (VITA-49 synthesized)"),
    });

    // S-meter (SLC/LEVEL)
    model->defineMeter(MeterDef{
        .index       = HermesMeterIndex::SLICE0_LEVEL,
        .source      = QStringLiteral("SLC"),
        .sourceIndex = 0,
        .name        = QStringLiteral("LEVEL"),
        .unit        = QStringLiteral("dBm"),
        .low         = -150.0,
        .high        = 20.0,
        .description = QStringLiteral("HL2 S-meter from IQ magnitude (VITA-49 synthesized)"),
    });

    // PTT state
    model->defineMeter(MeterDef{
        .index       = HermesMeterIndex::PTT_STATE,
        .source      = QStringLiteral("TX"),
        .sourceIndex = 0,
        .name        = QStringLiteral("PTT"),
        .unit        = QStringLiteral(""),
        .low         = 0.0,
        .high        = 1.0,
        .description = QStringLiteral("HL2 PTT state"),
    });

    // TX on
    model->defineMeter(MeterDef{
        .index       = HermesMeterIndex::TX_ON,
        .source      = QStringLiteral("TX"),
        .sourceIndex = 1,
        .name        = QStringLiteral("TXON"),
        .unit        = QStringLiteral(""),
        .low         = 0.0,
        .high        = 1.0,
        .description = QStringLiteral("HL2 TX on flag"),
    });

    // ADC clip count
    model->defineMeter(MeterDef{
        .index       = HermesMeterIndex::ADC_CLIP,
        .source      = QStringLiteral("RAD"),
        .sourceIndex = 1,
        .name        = QStringLiteral("ADCCLIP"),
        .unit        = QStringLiteral("Percent"),
        .low         = 0.0,
        .high        = 100.0,
        .description = QStringLiteral("HL2 ADC clip count"),
    });

    // CW key
    model->defineMeter(MeterDef{
        .index       = HermesMeterIndex::CW_KEY,
        .source      = QStringLiteral("TX"),
        .sourceIndex = 2,
        .name        = QStringLiteral("CWKEY"),
        .unit        = QStringLiteral(""),
        .low         = 0.0,
        .high        = 1.0,
        .description = QStringLiteral("HL2 CW key state"),
    });

    // PA external trigger
    model->defineMeter(MeterDef{
        .index       = HermesMeterIndex::PA_EXT_TR,
        .source      = QStringLiteral("RAD"),
        .sourceIndex = 2,
        .name        = QStringLiteral("PAEXTTR"),
        .unit        = QStringLiteral(""),
        .low         = 0.0,
        .high        = 1.0,
        .description = QStringLiteral("HL2 PA external trigger"),
    });

    // Bias current
    model->defineMeter(MeterDef{
        .index       = HermesMeterIndex::BIAS_CURRENT,
        .source      = QStringLiteral("RAD"),
        .sourceIndex = 3,
        .name        = QStringLiteral("BIAS"),
        .unit        = QStringLiteral("Amps"),
        .low         = 0.0,
        .high        = 2.0,
        .description = QStringLiteral("HL2 bias current"),
    });

    qCDebug(lcConnection) << "HermesBackend: applied" << HermesMeterIndex::COUNT
             << "synthetic meter definitions";
}

// ────────────────────────────────────────────────────────────────
//  Bridge signal handlers
// ────────────────────────────────────────────────────────────────

void HermesBackend::onBridgeConnected()
{
    m_state.store(State::Connected);
    emit stateChanged(State::Connected);
    emit connected();

    m_radioModel = QStringLiteral("Hermes Lite 2");
    m_radioName = QStringLiteral("Hermes Lite 2");
    m_radioVersion = m_bridge.gatewareVersion();

    emit radioInfoUpdated();
}

void HermesBackend::onBridgeDisconnected()
{
    m_state.store(State::Disconnected);
    emit stateChanged(State::Disconnected);
    emit disconnected();
    m_meterDefsSent = false;
}

void HermesBackend::onBridgeStateChanged(HermesToVita49Bridge::ConnectionState newState)
{
    State s;
    switch (newState) {
    case HermesToVita49Bridge::ConnectionState::Disconnected:
        s = State::Disconnected; break;
    case HermesToVita49Bridge::ConnectionState::Connecting:
    case HermesToVita49Bridge::ConnectionState::Handshaking:
        s = State::Connecting; break;
    case HermesToVita49Bridge::ConnectionState::Connected:
        s = State::Connected; break;
    case HermesToVita49Bridge::ConnectionState::Error:
        s = State::Error; break;
    default:
        s = State::Disconnected; break;
    }
    m_state.store(s);
    emit stateChanged(s);
}

void HermesBackend::onBridgeFrequencyUpdated(uint64_t freqHz)
{
    emit frequencyUpdated(freqHz);
}

void HermesBackend::onBridgeModeUpdated(const QString& mode)
{
    emit modeUpdated(mode);
}

void HermesBackend::onBridgePttChanged(bool tx)
{
    emit pttStateChanged(tx);
}

void HermesBackend::onBridgeSMeterUpdated(int level)
{
    emit sMeterUpdated(level);
}

void HermesBackend::onBridgeTemperatureUpdated(float tempC)
{
    Q_UNUSED(tempC)
    // Handled via VITA-49 meter path → MeterModel
}

void HermesBackend::onBridgePowerUpdated(uint16_t fwdPow, uint16_t revPow)
{
    Q_UNUSED(fwdPow)
    Q_UNUSED(revPow)
    // Handled via VITA-49 meter path → MeterModel
}

// ────────────────────────────────────────────────────────────────
//  VITA-49 diagnostic forwarding → PCM audio extraction
// ────────────────────────────────────────────────────────────────

void HermesBackend::onVita49AudioPacket(const QByteArray& pkt)
{
    // Strip VITA-49 28-byte header, forward PCM to AudioEngine
    if (pkt.size() > HermesToVita49Bridge::VITA49_HEADER_BYTES) {
        QByteArray pcm = pkt.mid(HermesToVita49Bridge::VITA49_HEADER_BYTES);
        emit audioDataReady(pcm);
    }
}

void HermesBackend::onVita49FftPacket(const QByteArray& pkt)
{
    Q_UNUSED(pkt)
    // FFT handled by PanadapterStream via loopback
}

void HermesBackend::onVita49MeterPacket(const QByteArray& pkt)
{
    Q_UNUSED(pkt)
    // Meters handled by PanadapterStream → MeterModel via loopback
}

} // namespace MasterSDR
