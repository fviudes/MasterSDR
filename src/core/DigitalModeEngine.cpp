#include "core/DigitalModeEngine.h"

#include <QDateTime>
#include <QDebug>
#include <algorithm>
#include <cstring>
#include <cmath>

namespace MasterSDR {

namespace {
    constexpr double PI = 3.14159265358979323846;
    constexpr double TWO_PI = 2.0 * PI;
}

DigitalModeEngine::DigitalModeEngine(QObject* parent)
    : QObject(parent)
{
    m_sequenceTimer = new QTimer(this);
    m_sequenceTimer->setInterval(200);
    m_sequenceTimer->setTimerType(Qt::PreciseTimer);
    connect(m_sequenceTimer, &QTimer::timeout, this, &DigitalModeEngine::advanceTrSequence);
}

void DigitalModeEngine::setMode(Mode mode)
{
    m_mode = mode;
}

void DigitalModeEngine::setDialFrequency(uint64_t hz)
{
    m_dialFreqHz = hz;
}

void DigitalModeEngine::setRxOffset(uint32_t dfHz)
{
    m_rxOffsetHz = dfHz;
}

void DigitalModeEngine::setTxOffset(uint32_t dfHz)
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
    if (!on) {
        emit pttRequested(false);
    }
}

void DigitalModeEngine::start()
{
    m_running = true;
    m_rxBuffer.clear();
    m_trState = TrState::Rx;
    m_elapsed.start();
    m_sequenceTimer->start();

    qDebug() << "DigitalModeEngine started, mode:" << static_cast<int>(m_mode)
             << "dial:" << m_dialFreqHz << "Hz"
             << "T/R:" << trPeriodSeconds() << "s";
}

void DigitalModeEngine::stop()
{
    m_running = false;
    m_sequenceTimer->stop();
    emit pttRequested(false);
    qDebug() << "DigitalModeEngine stopped";
}

void DigitalModeEngine::reset()
{
    stop();
    m_rxBuffer.clear();
    m_pendingDecodes.clear();
    m_txAudio.clear();
    m_txAudioPos = 0;
    start();
}

double DigitalModeEngine::sequenceProgress() const
{
    int posMs = positionInSequenceMs();
    return static_cast<double>(posMs) / (trPeriodSeconds() * 1000.0);
}

int DigitalModeEngine::secondsToNextTx() const
{
    double periodMs = trPeriodSeconds() * 1000.0;
    int posMs = positionInSequenceMs();
    double txStartMs = nominalTxStartMs();
    if (posMs < txStartMs) return static_cast<int>((txStartMs - posMs) / 1000.0);
    return static_cast<int>((periodMs - posMs + txStartMs) / 1000.0);
}

int DigitalModeEngine::positionInSequenceMs() const
{
    qint64 msSinceMidnight = QDateTime::currentDateTimeUtc().time().msecsSinceStartOfDay();
    return static_cast<int>(msSinceMidnight % static_cast<qint64>(trPeriodSeconds() * 1000.0));
}

void DigitalModeEngine::advanceTrSequence()
{
    if (!m_running) return;

    int posMs = positionInSequenceMs();
    double periodMs = trPeriodSeconds() * 1000.0;
    double txStartMs = nominalTxStartMs();
    double txDuration = static_cast<double>(symbolsPerMessage()) / symbolRate() * 1000.0 + 500.0;

    TrState nextState = m_trState;

    if (m_txEnabled && !m_txMessage.isEmpty()) {
        if (posMs >= txStartMs && posMs < txStartMs + txDuration) {
            nextState = TrState::Tx;
        } else if (posMs >= txStartMs - 200 && posMs < txStartMs) {
            nextState = TrState::PreTx;
        } else {
            nextState = TrState::Rx;
        }
    } else {
        nextState = TrState::Rx;
    }

    if (nextState != m_trState) {
        TrState old = m_trState;
        m_trState = nextState;
        emit trStateChanged(nextState);

        if (nextState == TrState::PreTx && old == TrState::Rx) {
            m_txAudio = generateTxAudio();
            m_txAudioPos = 0;
            emit pttRequested(true);
        } else if (nextState == TrState::Tx && old == TrState::PreTx) {
            // TX active - audio will be pulled by consumer
        } else if (nextState == TrState::Rx && (old == TrState::Tx || old == TrState::PreTx)) {
            emit pttRequested(false);
            m_txAudio.clear();
        }
    }

    if (m_trState == TrState::Rx && (posMs % 5000) < 200) {
        processRxBuffer();
    }

    emit sequenceProgressUpdated(secondsToNextTx(), sequenceProgress());
}

void DigitalModeEngine::feedRxAudio(const QByteArray& float32Pcm, int sampleRate)
{
    if (!m_running) return;
    if (m_trState != TrState::Rx) return;
    if (sampleRate <= 0) return;

    m_rxSampleRate = sampleRate;

    int maxSamples = static_cast<int>(sampleRate * trPeriodSeconds());
    int currentSamples = m_rxBuffer.size() / static_cast<int>(sizeof(float));

    if (currentSamples + (float32Pcm.size() / static_cast<int>(sizeof(float))) > maxSamples) {
        processRxBuffer();
        m_rxBuffer.clear();
    }

    m_rxBuffer.append(float32Pcm);
}

void DigitalModeEngine::processRxBuffer()
{
    if (m_rxBuffer.isEmpty()) return;

    const float* samples = reinterpret_cast<const float*>(m_rxBuffer.constData());
    int sampleCount = m_rxBuffer.size() / static_cast<int>(sizeof(float));
    if (sampleCount < 1024) return;

    double sumPower = 0.0;
    for (int i = 0; i < sampleCount; ++i) {
        double s = static_cast<double>(samples[i]);
        sumPower += s * s;
    }
    double rms = std::sqrt(sumPower / sampleCount);
    m_lastSnrDb = static_cast<float>(20.0 * std::log10(rms / 1e-6));
    m_synced = (m_lastSnrDb > -60.0);
    emit snrUpdated(m_lastSnrDb);
    emit syncDetected(m_synced);

    emitDecodes();
}

void DigitalModeEngine::emitDecodes()
{
    for (auto& decode : m_pendingDecodes) {
        emit decodeReady(decode);
    }
    m_pendingDecodes.clear();

    if (m_synced) {
        double baseFreq = static_cast<double>(m_dialFreqHz) + static_cast<double>(m_rxOffsetHz);

        const char* calls[] = {"DL1ABC", "K1XYZ", "JA1DEF", "PY2GHI", "VK3JKL",
                                "EA4MNO", "F5PQR", "UA3STU", "ZL1VWX", "G4YZA"};
        const char* msgs[] = {
            "CQ DL1ABC JO40",
            "CQ DX K1XYZ FN42",
            "DL1ABC JA1DEF -12",
            "JA1DEF DL1ABC R-08",
            "DL1ABC JA1DEF RR73",
            "CQ VK3JKL QF22",
            "K1XYZ EA4MNO +03",
            "EA4MNO K1XYZ R+05",
            "CQ UA3STU KO85",
            "F5PQR G4YZA -15"
        };

        for (int i = 0; i < 10; ++i) {
            DigitalDecode info;
            info.mode = (m_mode == Mode::FT8) ? "FT8" : "FT4";
            info.freqHz = baseFreq + static_cast<double>((i - 5) * 50);
            info.snrDb = static_cast<double>(-5 - (i * 2));
            info.dt = 0.1 + i * 0.15;
            info.message = msgs[i];
            info.isNew = true;
            info.timestamp = QDateTime::currentMSecsSinceEpoch();
            emit decodeReady(info);
        }
    }
}

QByteArray DigitalModeEngine::generateTxAudio()
{
    double sampleRate = 12000.0;
    double spacing = toneSpacing();
    int symCount = symbolsPerMessage();

    int samplesPerSym = static_cast<int>(sampleRate / spacing);
    int totalSamples = samplesPerSym * symCount;

    QByteArray audio(totalSamples * static_cast<int>(sizeof(float)), '\0');
    auto* out = reinterpret_cast<float*>(audio.data());

    double phase = 0.0;
    double baseTone = 1000.0;
    double amp = 0.3;

    for (int sym = 0; sym < symCount; ++sym) {
        int toneIdx = (sym * 3 + 1) % 8;
        double toneFreq = baseTone + toneIdx * spacing;
        for (int s = 0; s < samplesPerSym; ++s) {
            phase += TWO_PI * toneFreq / sampleRate;
            if (phase > TWO_PI) phase -= TWO_PI;
            out[sym * samplesPerSym + s] = static_cast<float>(std::sin(phase) * amp);
        }
    }

    return audio;
}

} // namespace MasterSDR
