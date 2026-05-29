#include "core/IcomIpBackend.h"
#include "core/LogManager.h"
#include "models/MeterModel.h"
#include "core/AudioEngine.h"

#include <QDebug>
#include <QtEndian>

namespace MasterSDR {

IcomIpBackend::IcomIpBackend(QObject* parent)
    : ISourceBackend(parent)
{
    wireBridgeSignals();
}

IcomIpBackend::~IcomIpBackend()
{
    disconnectFromRadio();
}

void IcomIpBackend::wireBridgeSignals()
{
    connect(&m_bridge, &CivToVita49Bridge::connected,
            this, &IcomIpBackend::onBridgeConnected);
    connect(&m_bridge, &CivToVita49Bridge::disconnected,
            this, &IcomIpBackend::onBridgeDisconnected);
    connect(&m_bridge, &CivToVita49Bridge::stateChanged,
            this, &IcomIpBackend::onBridgeStateChanged);
    connect(&m_bridge, &CivToVita49Bridge::errorOccurred,
            this, &IcomIpBackend::errorOccurred);

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

    // Icom-specific extended signals
    connect(&m_bridge, &CivToVita49Bridge::squelchStatusUpdated,
            this, &IcomIpBackend::onBridgeSquelchStatusUpdated);
    connect(&m_bridge, &CivToVita49Bridge::txPowerUpdated,
            this, &IcomIpBackend::onBridgeTxPowerUpdated);
    connect(&m_bridge, &CivToVita49Bridge::rfGainUpdated,
            this, &IcomIpBackend::onBridgeRfGainUpdated);
    connect(&m_bridge, &CivToVita49Bridge::splitUpdated,
            this, &IcomIpBackend::onBridgeSplitUpdated);
    connect(&m_bridge, &CivToVita49Bridge::preampUpdated,
            this, &IcomIpBackend::onBridgePreampUpdated);
    connect(&m_bridge, &CivToVita49Bridge::attenuatorUpdated,
            this, &IcomIpBackend::onBridgeAttenuatorUpdated);
    connect(&m_bridge, &CivToVita49Bridge::bkInUpdated,
            this, &IcomIpBackend::onBridgeBkInUpdated);
    connect(&m_bridge, &CivToVita49Bridge::apfUpdated,
            this, &IcomIpBackend::onBridgeApfUpdated);

    // VITA-49 → raw data extraction
    connect(&m_bridge, &CivToVita49Bridge::vita49AudioPacket,
            this, &IcomIpBackend::onVita49AudioPacket);
    connect(&m_bridge, &CivToVita49Bridge::vita49FftPacket,
            this, &IcomIpBackend::onVita49FftPacket);
    connect(&m_bridge, &CivToVita49Bridge::vita49MeterPacket,
            this, &IcomIpBackend::onVita49MeterPacket);
}

void IcomIpBackend::connectToRadio()
{
    connectToRadio(m_host, m_ctrlPort, m_serialPort, m_audioPort, m_username, m_password);
}

void IcomIpBackend::connectToRadio(const QString& host, uint16_t ctrlPort,
                                    uint16_t serialPort, uint16_t audioPort,
                                    const QString& username, const QString& password)
{
    if (m_state.load() == State::Connected || m_state.load() == State::Connecting)
        return;

    m_host = host;
    m_ctrlPort = ctrlPort;
    m_serialPort = serialPort;
    m_audioPort = audioPort;
    m_username = username;
    m_password = password;
    m_meterDefsSent = false;

    m_state.store(State::Connecting);
    emit stateChanged(State::Connecting);

    if (m_meterModel) {
        applyMeterDefinitions(m_meterModel);
        m_meterDefsSent = true;
    }

    // Enable VITA-49 emission even without loopback port
    // The bridge will emit vita49AudioPacket/FftPacket/MeterPacket signals
    // which we extract raw data from
    m_bridge.setVita49Enabled(true);

    m_bridge.connectToRadio(m_host, m_ctrlPort, m_serialPort, m_audioPort);
}

void IcomIpBackend::disconnectFromRadio()
{
    m_bridge.disconnectFromRadio();
}

void IcomIpBackend::setFrequency(uint64_t freqHz) { m_bridge.setFrequency(freqHz); }
void IcomIpBackend::setMode(const QString& mode)  { m_bridge.setMode(mode); }
void IcomIpBackend::setPtt(bool tx)               { m_bridge.setPtt(tx); }

void IcomIpBackend::setMeterModel(MeterModel* meterModel) { m_meterModel = meterModel; }
void IcomIpBackend::setAudioEngine(AudioEngine* audioEngine) { m_audioEngine = audioEngine; }

void IcomIpBackend::applyMeterDefinitions(MeterModel* model)
{
    if (!model) return;

    model->defineMeter(MeterDef{
        .index = IcomMeterIndex::SLICE0_LEVEL, .source = QStringLiteral("SLC"), .sourceIndex = 0,
        .name = QStringLiteral("LEVEL"), .unit = QStringLiteral("dBm"),
        .low = -150.0, .high = 20.0,
        .description = QStringLiteral("Icom CI-V S-meter"),
    });

    model->defineMeter(MeterDef{
        .index = IcomMeterIndex::TX_POWER, .source = QStringLiteral("TX"), .sourceIndex = 0,
        .name = QStringLiteral("FWDPWR"), .unit = QStringLiteral("Watts"),
        .low = 0.0, .high = 100.0,
        .description = QStringLiteral("Icom CI-V TX power"),
    });

    model->defineMeter(MeterDef{
        .index = IcomMeterIndex::RF_GAIN, .source = QStringLiteral("SLC"), .sourceIndex = 0,
        .name = QStringLiteral("RFGAIN"), .unit = QStringLiteral("Percent"),
        .low = 0.0, .high = 100.0,
        .description = QStringLiteral("Icom CI-V RF gain"),
    });
}

// ── Bridge signal handlers ──────────────────────────────────────

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

void IcomIpBackend::onBridgeStateChanged(CivToVita49Bridge::ConnectionState s)
{
    State ns = State::Disconnected;
    switch (s) {
    case CivToVita49Bridge::ConnectionState::Disconnected: ns = State::Disconnected; break;
    case CivToVita49Bridge::ConnectionState::Connecting: case CivToVita49Bridge::ConnectionState::Handshaking:
        ns = State::Connecting; break;
    case CivToVita49Bridge::ConnectionState::Connected: ns = State::Connected; break;
    case CivToVita49Bridge::ConnectionState::Error: ns = State::Error; break;
    }
    m_state.store(ns);
    emit stateChanged(ns);
}

void IcomIpBackend::onBridgeFrequencyUpdated(uint64_t f) { emit frequencyUpdated(f); }
void IcomIpBackend::onBridgeModeUpdated(const QString& m) { emit modeUpdated(m); }
void IcomIpBackend::onBridgeSMeterUpdated(int l)     { emit sMeterUpdated(l); }
void IcomIpBackend::onBridgePttChanged(bool t)       { emit pttStateChanged(t); }

// ── VITA-49 → raw data extraction ───────────────────────────────

void IcomIpBackend::onVita49AudioPacket(const QByteArray& pkt)
{
    if (pkt.size() <= CivToVita49Bridge::VITA49_HEADER_BYTES) return;

    // Extract raw PCM from VITA-49 payload
    // VITA-49 format: 28-byte header + float32 stereo BE payload
    QByteArray pcm = pkt.mid(CivToVita49Bridge::VITA49_HEADER_BYTES);
    emitAudioDataReady(pcm);

    if (m_audioEngine) {
        m_audioEngine->feedAudioData(pcm);
    }
}

void IcomIpBackend::onVita49FftPacket(const QByteArray& pkt)
{
    if (pkt.size() <= CivToVita49Bridge::VITA49_HEADER_BYTES + 12) return;

    // Extract scope data from VITA-49 FFT packet
    // 28-byte VITA-49 header + 12-byte FFT sub-header + uint16 BE bins
    const uint8_t* raw = reinterpret_cast<const uint8_t*>(pkt.constData());
    int dataStart = CivToVita49Bridge::VITA49_HEADER_BYTES + 12;
    int dataLen = pkt.size() - dataStart;
    int numBins = dataLen / 2;  // uint16 = 2 bytes per bin

    QByteArray scopeData;
    scopeData.reserve(numBins);
    for (int i = 0; i < numBins; ++i) {
        // Pixel Y value (0=top=max signal, yPixels-1=bottom=min signal)
        quint16 pixelY = qFromBigEndian<quint16>(raw + dataStart + i * 2);
        // Convert to 0-255 byte range (inverted: 0=bottom, 255=top)
        uint8_t byteVal = static_cast<uint8_t>(255 - qMin(255u, static_cast<unsigned>(pixelY * 255 / 100)));
        scopeData.append(static_cast<char>(byteVal));
    }
    emitSpectrumDataReady(scopeData);
}

void IcomIpBackend::onVita49MeterPacket(const QByteArray& pkt)
{
    Q_UNUSED(pkt)
    // Meters handled by MeterModel via VITA-49 signals or direct bridge updates
}

// ── Protected emit helpers (bypass ISourceBackend private signals) ──

void IcomIpBackend::emitAudioDataReady(const QByteArray& pcm)
{
    emit audioDataReady(pcm);
}

void IcomIpBackend::emitSpectrumDataReady(const QByteArray& scopeData)
{
    emit spectrumDataReady(scopeData);
}

} // namespace MasterSDR
