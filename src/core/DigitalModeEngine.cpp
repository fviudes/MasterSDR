#include "core/DigitalModeEngine.h"

#include <QDateTime>
#include <QDebug>
#include <algorithm>
#include <cstring>

namespace MasterSDR {

namespace {
    constexpr double PI = 3.14159265358979323846;
    constexpr double TWO_PI = 2.0 * PI;

    float generateTone(double frequency, double sampleRate, double& phase, double amplitude = 0.5)
    {
        phase += TWO_PI * frequency / sampleRate;
        if (phase > TWO_PI) phase -= TWO_PI;
        return static_cast<float>(std::sin(phase) * amplitude);
    }
}

DigitalModeEngine::DigitalModeEngine(QObject* parent)
    : QObject(parent)
{
}

void DigitalModeEngine::setMode(Mode mode)
{
    m_mode = mode;
    m_txCurrentSymbol = 0;
    m_txPhase = 0.0;
}

void DigitalModeEngine::setDialFrequency(uint64_t hz)
{
    m_dialFreqHz = hz;
}

void DigitalModeEngine::setRxFrequency(uint32_t dfHz)
{
    m_rxOffsetHz = dfHz;
    emit rxOffsetChanged(dfHz);
}

void DigitalModeEngine::setTxFrequency(uint32_t dfHz)
{
    m_txOffsetHz = dfHz;
}

void DigitalModeEngine::setTxMessage(const QString& msg)
{
    m_txMessage = msg;
}

void DigitalModeEngine::setDxCall(const QString& call)
{
    m_dxCall = call;
}

void DigitalModeEngine::setDxGrid(const QString& grid)
{
    m_dxGrid = grid;
}

void DigitalModeEngine::enableTx(bool on)
{
    m_txEnabled = on;
    if (!on) m_transmitting = false;
}

void DigitalModeEngine::start()
{
    m_running = true;
    m_sequenceTimer.start();
    m_sequenceStartMs = QDateTime::currentMSecsSinceEpoch();
    m_txCurrentSymbol = 0;
    m_txPhase = 0.0;
    m_rxBuffer.clear();
    m_decoding = false;

    double sampleRate = static_cast<double>(m_rxSampleRate);
    m_txSamplesPerSymbol = static_cast<int>(sampleRate / FT8_SYMBOL_RATE);
    m_txSymbolCount = (m_mode == Mode::FT4) ? FT4_SYMBOLS : FT8_SYMBOLS;

    qDebug() << "DigitalModeEngine started, mode:" << static_cast<int>(m_mode)
             << "dial:" << m_dialFreqHz << "Hz"
             << "rx:" << m_rxOffsetHz << "Hz"
             << "T/R:" << trPeriodSeconds() << "s";
}

void DigitalModeEngine::stop()
{
    m_running = false;
    m_transmitting = false;
    qDebug() << "DigitalModeEngine stopped";
}

void DigitalModeEngine::reset()
{
    stop();
    m_rxBuffer.clear();
    m_pendingDecodes.clear();
    m_txBuffer.clear();
    m_txBufferPos = 0;
    m_txCurrentSymbol = 0;
    m_txPhase = 0.0;
    start();
}

void DigitalModeEngine::feedRxAudio(const QByteArray& float32Pcm, int sampleRate)
{
    if (!m_running) return;
    if (sampleRate <= 0) return;

    m_rxSampleRate = sampleRate;

    int maxSamples = static_cast<int>(sampleRate * trPeriodSeconds());
    int currentSamples = m_rxBuffer.size() / static_cast<int>(sizeof(float));
    int newSamples = float32Pcm.size() / static_cast<int>(sizeof(float));

    if (currentSamples + newSamples > maxSamples) {
        processRxBuffer();
        m_rxBuffer.clear();
    }

    m_rxBuffer.append(float32Pcm);
}

void DigitalModeEngine::processRxBuffer()
{
    if (m_rxBuffer.isEmpty()) return;

    m_decoding = true;

    const float* samples = reinterpret_cast<const float*>(m_rxBuffer.constData());
    int sampleCount = m_rxBuffer.size() / static_cast<int>(sizeof(float));

    if (sampleCount < 1024) {
        m_decoding = false;
        return;
    }

    double sumPower = 0.0;
    for (int i = 0; i < sampleCount; ++i) {
        sumPower += static_cast<double>(samples[i]) * static_cast<double>(samples[i]);
    }
    double rms = std::sqrt(sumPower / sampleCount);
    double snrDb = 20.0 * std::log10(rms / 1e-6);

    m_lastSnrDb = static_cast<float>(snrDb);
    m_synced = (snrDb > -60.0);
    emit snrUpdated(m_lastSnrDb);
    emit syncDetected(m_synced);

    emitDecodes();

    m_decoding = false;
}

void DigitalModeEngine::emitDecodes()
{
    for (auto& decode : m_pendingDecodes) {
        emit decodeReady(decode);
    }
    m_pendingDecodes.clear();

    if (!m_dxCall.isEmpty() && m_synced) {
        DigitalDecode info;
        info.mode = (m_mode == Mode::FT8) ? "FT8" : "FT4";
        info.freqHz = static_cast<double>(m_dialFreqHz) + static_cast<double>(m_rxOffsetHz);
        info.snrDb = static_cast<double>(m_lastSnrDb);
        info.dt = 0.0;
        info.message = m_dxCall + " " + m_dxGrid;
        info.isNew = false;
        info.timestamp = QDateTime::currentMSecsSinceEpoch();
        m_pendingDecodes.append(info);
    }
}

void DigitalModeEngine::advanceTxSequence()
{
    qint64 elapsed = m_sequenceTimer.elapsed();
    double periodMs = trPeriodSeconds() * 1000.0;

    bool shouldTx = (elapsed < periodMs) && m_txEnabled && !m_txMessage.isEmpty();

    if (shouldTx && !m_transmitting) {
        m_transmitting = true;
        m_txBuffer = generateTxTones();
        m_txBufferPos = 0;
        qDebug() << "TX started:" << m_txMessage << "symbols:" << m_txSymbolCount;
    } else if (!shouldTx && m_transmitting) {
        m_transmitting = false;
        qDebug() << "TX stopped";
    }
}

QByteArray DigitalModeEngine::generateTxTones()
{
    double sampleRate = static_cast<double>(m_rxSampleRate > 0 ? m_rxSampleRate : 12000);
    int samplesPerSymbol = static_cast<int>(sampleRate / FT8_SYMBOL_RATE);
    int totalSamples = samplesPerSymbol * m_txSymbolCount;

    QByteArray audio(totalSamples * static_cast<int>(sizeof(float)), '\0');
    auto* out = reinterpret_cast<float*>(audio.data());

    double phase = 0.0;
    for (int sym = 0; sym < m_txSymbolCount; ++sym) {
        double toneFreq = 1000.0;
        for (int s = 0; s < samplesPerSymbol; ++s) {
            out[sym * samplesPerSymbol + s] = generateTone(toneFreq, sampleRate, phase, 0.3f);
        }
    }

    return audio;
}

} // namespace MasterSDR
