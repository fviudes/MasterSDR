#include "core/IcomIpBackend.h"
#include "core/PanadapterStream.h"
#include "core/LogManager.h"
#include "models/MeterModel.h"

#include <QDebug>

namespace MasterSDR {

// ────────────────────────────────────────────────────────────────
//  Construction
// ────────────────────────────────────────────────────────────────

IcomIpBackend::IcomIpBackend(QObject* parent)
    : ISourceBackend(parent)
{
    wireBridgeSignals();
}

IcomIpBackend::~IcomIpBackend()
{
    disconnectFromRadio();
}

// ────────────────────────────────────────────────────────────────
//  Signal wiring: CivToVita49Bridge → IcomIpBackend → ISourceBackend
// ────────────────────────────────────────────────────────────────

void IcomIpBackend::wireBridgeSignals()
{
    connect(&m_bridge, &CivToVita49Bridge::connected,
            this, &IcomIpBackend::onBridgeConnected);
    connect(&m_bridge, &CivToVita49Bridge::disconnected,
            this, &IcomIpBackend::onBridgeDisconnected);
    connect(&m_bridge, &CivToVita49Bridge::stateChanged,
            this, &IcomIpBackend::onBridgeStateChanged);

    // ISourceBackend signals
    connect(&m_bridge, &CivToVita49Bridge::frequencyUpdated,
            this, &IcomIpBackend::onBridgeFrequencyUpdated);
    connect(&m_bridge, &CivToVita49Bridge::modeUpdated,
            this, &IcomIpBackend::onBridgeModeUpdated);
    connect(&m_bridge, &CivToVita49Bridge::sMeterUpdated,
            this, &IcomIpBackend::onBridgeSMeterUpdated);
    connect(&m_bridge, &CivToVita49Bridge::pttStateChanged,
            this, &IcomIpBackend::onBridgePttChanged);
    connect(&m_bridge, &CivToVita49Bridge::radioInfoUpdated,
            this, &IcomIpBackend::radioInfoUpdated);

    // Forward VITA-49 diagnostic signals
    connect(&m_bridge, &CivToVita49Bridge::vita49AudioPacket,
            this, &IcomIpBackend::onVita49AudioPacket);
    connect(&m_bridge, &CivToVita49Bridge::vita49FftPacket,
            this, &IcomIpBackend::onVita49FftPacket);
    connect(&m_bridge, &CivToVita49Bridge::vita49MeterPacket,
            this, &IcomIpBackend::onVita49MeterPacket);
}

// ────────────────────────────────────────────────────────────────
//  Connection lifecycle
// ────────────────────────────────────────────────────────────────

void IcomIpBackend::connectToRadio()
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
            qCDebug(lcConnection) << "IcomIpBackend: VITA-49 loopback on port" << streamPort;
        }
    }

    // Set Icom audio sample rate (IC-705: 8kHz, IC-7300/7610: 16kHz)
    m_bridge.setIcomAudioSampleRate(8000);

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

    m_bridge.connectToRadio(m_host, m_ctrlPort, m_serialPort, m_audioPort);
}

void IcomIpBackend::disconnectFromRadio()
{
    m_bridge.disconnectFromRadio();
}

// ────────────────────────────────────────────────────────────────
//  ISourceBackend command dispatch
// ────────────────────────────────────────────────────────────────

void IcomIpBackend::setFrequency(uint64_t freqHz)
{
    m_bridge.setFrequency(freqHz);
}

void IcomIpBackend::setMode(const QString& mode)
{
    m_bridge.setMode(mode);
}

void IcomIpBackend::setPtt(bool tx)
{
    m_bridge.setPtt(tx);
}

// ────────────────────────────────────────────────────────────────
//  PanadapterStream setup
// ────────────────────────────────────────────────────────────────

void IcomIpBackend::setPanadapterStream(PanadapterStream* stream)
{
    m_panStream = stream;
}

void IcomIpBackend::setMeterModel(MeterModel* meterModel)
{
    m_meterModel = meterModel;
}

void IcomIpBackend::applyPanadapterDefinitions()
{
    if (!m_panStream) return;

    // Register the synthetic pan stream for the bridge
    quint32 panId = 0x40000000;
    m_panStream->registerPanStream(panId);

    // Set FFT pixel dimensions (Icom scope: 475 bins, 80-100 pixels height)
    m_panStream->setYPixels(panId, 100);

    // dBm range for Icom scope (-130 to -20 dBm typical)
    m_panStream->setDbmRange(panId, -130.0f, -20.0f);

    qCDebug(lcConnection) << "IcomIpBackend: registered pan stream 0x40000000";
}

// ────────────────────────────────────────────────────────────────
//  Meter definition injection into MeterModel
// ────────────────────────────────────────────────────────────────

void IcomIpBackend::applyMeterDefinitions(MeterModel* model)
{
    // idx removed - IcomMeterIndex provides the indices directly
    model->defineMeter(MeterDef{
        .index       = IcomMeterIndex::SLICE0_LEVEL,
        .source      = QStringLiteral("SLC"),
        .sourceIndex = 0,
        .name        = QStringLiteral("LEVEL"),
        .unit        = QStringLiteral("dBm"),
        .low         = -150.0,
        .high        = 20.0,
        .description = QStringLiteral("Icom CI-V S-meter level (VITA-49 synthesized)"),
    });

    // SLICE0_SQUELCH: Squelch status
    model->defineMeter(MeterDef{
        .index       = IcomMeterIndex::SLICE0_SQUELCH,
        .source      = QStringLiteral("SLC"),
        .sourceIndex = 0,
        .name        = QStringLiteral("SQUELCH_OPEN"),
        .unit        = QStringLiteral(""),
        .low         = 0.0,
        .high        = 1.0,
        .description = QStringLiteral("Icom CI-V squelch open flag"),
    });

    // TX_POWER: RF output power
    model->defineMeter(MeterDef{
        .index       = IcomMeterIndex::TX_POWER,
        .source      = QStringLiteral("TX"),
        .sourceIndex = 0,
        .name        = QStringLiteral("FWDPWR"),
        .unit        = QStringLiteral("Watts"),
        .low         = 0.0,
        .high        = 100.0,
        .description = QStringLiteral("Icom CI-V TX power percentage (VITA-49 synthesized)"),
    });

    // RF_GAIN
    model->defineMeter(MeterDef{
        .index       = IcomMeterIndex::RF_GAIN,
        .source      = QStringLiteral("SLC"),
        .sourceIndex = 0,
        .name        = QStringLiteral("RFGAIN"),
        .unit        = QStringLiteral("Percent"),
        .low         = 0.0,
        .high        = 100.0,
        .description = QStringLiteral("Icom CI-V RF gain percentage"),
    });

    // MIC_LEVEL placeholder
    model->defineMeter(MeterDef{
        .index       = IcomMeterIndex::MIC_LEVEL,
        .source      = QStringLiteral("COD"),
        .sourceIndex = 0,
        .name        = QStringLiteral("MIC"),
        .unit        = QStringLiteral("dBFS"),
        .low         = -150.0,
        .high        = 0.0,
        .description = QStringLiteral("Icom mic level (VITA-49 synthesized, placeholder)"),
    });

    // PA_TEMP placeholder
    model->defineMeter(MeterDef{
        .index       = IcomMeterIndex::PA_TEMP,
        .source      = QStringLiteral("RAD"),
        .sourceIndex = 0,
        .name        = QStringLiteral("PATEMP"),
        .unit        = QStringLiteral("degC"),
        .low         = 0.0,
        .high        = 100.0,
        .description = QStringLiteral("Icom PA temperature (placeholder)"),
    });

    // SUPPLY_VOLTS placeholder
    model->defineMeter(MeterDef{
        .index       = IcomMeterIndex::SUPPLY_VOLTS,
        .source      = QStringLiteral("RAD"),
        .sourceIndex = 0,
        .name        = QStringLiteral("+13.8A"),
        .unit        = QStringLiteral("Volts"),
        .low         = 0.0,
        .high        = 20.0,
        .description = QStringLiteral("Icom supply voltage (placeholder)"),
    });

    qCDebug(lcConnection) << "IcomIpBackend: applied" << IcomMeterIndex::COUNT
             << "synthetic meter definitions";
}

// ────────────────────────────────────────────────────────────────
//  Bridge signal handlers
// ────────────────────────────────────────────────────────────────

void IcomIpBackend::onBridgeConnected()
{
    m_state.store(State::Connected);
    emit stateChanged(State::Connected);
    emit connected();

    m_radioModel = m_bridge.radioModel();
    m_radioName = QStringLiteral("Icom ") + m_radioModel;

    emit radioInfoUpdated();
}

void IcomIpBackend::onBridgeDisconnected()
{
    m_state.store(State::Disconnected);
    emit stateChanged(State::Disconnected);
    emit disconnected();

    m_meterDefsSent = false;
}

void IcomIpBackend::onBridgeStateChanged(CivToVita49Bridge::ConnectionState newState)
{
    State s;
    switch (newState) {
    case CivToVita49Bridge::ConnectionState::Disconnected:
        s = State::Disconnected; break;
    case CivToVita49Bridge::ConnectionState::Connecting:
    case CivToVita49Bridge::ConnectionState::Handshaking:
        s = State::Connecting; break;
    case CivToVita49Bridge::ConnectionState::Connected:
        s = State::Connected; break;
    case CivToVita49Bridge::ConnectionState::Error:
        s = State::Error; break;
    default:
        s = State::Disconnected; break;
    }
    m_state.store(s);
    emit stateChanged(s);
}

void IcomIpBackend::onBridgeFrequencyUpdated(uint64_t freqHz)
{
    emit frequencyUpdated(freqHz);
}

void IcomIpBackend::onBridgeModeUpdated(const QString& mode)
{
    emit modeUpdated(mode);
}

void IcomIpBackend::onBridgeSMeterUpdated(int level)
{
    emit sMeterUpdated(level);
}

void IcomIpBackend::onBridgePttChanged(bool tx)
{
    emit pttStateChanged(tx);
}

// ────────────────────────────────────────────────────────────────
//  VITA-49 diagnostic forwarding
// ────────────────────────────────────────────────────────────────

void IcomIpBackend::onVita49AudioPacket(const QByteArray& pkt)
{
    // Forward directly to audioDataReady for AudioEngine
    // Strip VITA-49 header to extract PCM
    if (pkt.size() > CivToVita49Bridge::VITA49_HEADER_BYTES) {
        QByteArray pcm = pkt.mid(CivToVita49Bridge::VITA49_HEADER_BYTES);
        emit audioDataReady(pcm);
    }
}

void IcomIpBackend::onVita49FftPacket(const QByteArray& pkt)
{
    Q_UNUSED(pkt)
    // FFT is handled by PanadapterStream via loopback
}

void IcomIpBackend::onVita49MeterPacket(const QByteArray& pkt)
{
    Q_UNUSED(pkt)
    // Meters are handled by PanadapterStream → MeterModel via loopback
}

} // namespace MasterSDR
