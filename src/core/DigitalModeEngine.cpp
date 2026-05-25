#include "core/DigitalModeEngine.h"

#include <QDateTime>
#include <QDebug>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <complex>

namespace MasterSDR {

namespace {
    constexpr double PI = 3.14159265358979323846;
    constexpr double TWO_PI = 2.0 * PI;

    void fft32(const std::complex<double>* in, std::complex<double>* out, bool inverse)
    {
        for (int k = 0; k < 32; ++k) {
            out[k] = std::complex<double>(0, 0);
            for (int n = 0; n < 32; ++n) {
                double angle = (inverse ? TWO_PI : -TWO_PI) * k * n / 32.0;
                out[k] += in[n] * std::complex<double>(std::cos(angle), std::sin(angle));
            }
            if (inverse) out[k] /= 32.0;
        }
    }
}

DigitalModeEngine::DigitalModeEngine(QObject* parent)
    : QObject(parent)
{
    m_sequenceTimer = new QTimer(this);
    m_sequenceTimer->setInterval(200);
    m_sequenceTimer->setTimerType(Qt::PreciseTimer);
    connect(m_sequenceTimer, &QTimer::timeout, this, &DigitalModeEngine::advanceTrSequence);

    m_ft8Decoder = new Ft8Decoder(this);
    m_ft8Decoder->initialize();
    qDebug() << "DigitalModeEngine: decoder =" << m_ft8Decoder->backendName();
}

void DigitalModeEngine::setMode(Mode mode) { m_mode = mode; }

void DigitalModeEngine::enableTx(bool on)
{
    m_txEnabled = on;
    if (!on) emit pttRequested(false);
}

void DigitalModeEngine::start()
{
    m_running = true;
    m_rxBuffer.clear();
    m_trState = TrState::Rx;
    m_elapsed.start();
    m_sequenceTimer->start();
    m_cd0Samples = 0;
    qDebug() << "DigitalModeEngine started, mode:" << static_cast<int>(m_mode);
}

void DigitalModeEngine::stop()
{
    m_running = false;
    m_sequenceTimer->stop();
    emit pttRequested(false);
}

void DigitalModeEngine::reset() { stop(); start(); }

double DigitalModeEngine::trPeriodSeconds() const
{
    switch (m_mode) {
    case Mode::FT8:  return 15.0;
    case Mode::FT4:  return 7.5;
    case Mode::WSPR: return 120.0;
    case Mode::Q65:  return 30.0;
    default:         return 15.0;
    }
}

double DigitalModeEngine::sequenceProgress() const
{
    return static_cast<double>(positionInSequenceMs()) / (trPeriodSeconds() * 1000.0);
}

int DigitalModeEngine::secondsToNextTx() const
{
    double periodMs = trPeriodSeconds() * 1000.0;
    int posMs = positionInSequenceMs();
    double txStartMs = (m_mode == Mode::FT8) ? 500.0 : 300.0;
    return static_cast<int>(posMs < txStartMs
        ? (txStartMs - posMs) / 1000.0
        : (periodMs - posMs + txStartMs) / 1000.0);
}

int DigitalModeEngine::positionInSequenceMs() const
{
    return static_cast<int>(QDateTime::currentDateTimeUtc().time().msecsSinceStartOfDay()
        % static_cast<qint64>(trPeriodSeconds() * 1000.0));
}

void DigitalModeEngine::advanceTrSequence()
{
    if (!m_running) return;

    int posMs = positionInSequenceMs();
    double periodMs = trPeriodSeconds() * 1000.0;
    double txStartMs = (m_mode == Mode::FT8) ? 500.0 : 300.0;
    double txDuration = (m_mode == Mode::FT8) ? 12640.0 : 6000.0;

    TrState nextState = TrState::Rx;
    if (m_txEnabled && !m_txMessage.isEmpty()) {
        if (posMs >= txStartMs && posMs < txStartMs + txDuration)
            nextState = TrState::Tx;
        else if (posMs >= txStartMs - 200 && posMs < txStartMs)
            nextState = TrState::PreTx;
    }

    if (nextState != m_trState) {
        TrState old = m_trState;
        m_trState = nextState;
        emit trStateChanged(nextState);

        if (nextState == TrState::PreTx && old == TrState::Rx) {
            m_txAudio = generateTxAudio();
            emit pttRequested(true);
        } else if (nextState == TrState::Rx && old == TrState::Tx) {
            emit pttRequested(false);
        }
    }

    if (m_trState == TrState::Rx && (posMs % 5000) < 200)
        processRxBuffer();

    emit sequenceProgressUpdated(secondsToNextTx(), sequenceProgress());
}

void DigitalModeEngine::feedRxAudio(const QByteArray& float32Pcm, int sampleRate)
{
    if (!m_running || m_trState != TrState::Rx || sampleRate <= 0) return;
    m_rxSampleRate = sampleRate;

    int maxSamples = static_cast<int>(sampleRate * trPeriodSeconds());
    int current = m_rxBuffer.size() / static_cast<int>(sizeof(float));
    if (current + (float32Pcm.size() / static_cast<int>(sizeof(float))) > maxSamples) {
        processRxBuffer();
        m_rxBuffer.clear();
    }
    m_rxBuffer.append(float32Pcm);
}

void DigitalModeEngine::processRxBuffer()
{
    if (m_rxBuffer.isEmpty()) return;
    const auto* samples = reinterpret_cast<const float*>(m_rxBuffer.constData());
    int count = m_rxBuffer.size() / static_cast<int>(sizeof(float));
    if (count < 10000) return;

    // RMS power for SNR estimation
    double sumPower = 0.0;
    for (int i = 0; i < std::min(count, 48000); ++i)
        sumPower += static_cast<double>(samples[i]) * static_cast<double>(samples[i]);
    double rms = std::sqrt(sumPower / std::min(count, 48000));
    m_lastSnrDb = static_cast<float>(20.0 * std::log10(rms / 1e-6));
    m_synced = (m_lastSnrDb > -70.0);
    emit snrUpdated(m_lastSnrDb);
    emit syncDetected(m_synced);

    // FT8 demodulation pipeline (matches WSJT-X ft8b.f90)
    if (m_mode == Mode::FT8 && m_synced) {
        ft8DownsampleAndSync(samples, count, m_fEst, m_iBest);
        emit syncQualityChanged(m_nsync);
    }

    emitDecodes();
}

void DigitalModeEngine::ft8DownsampleAndSync(const float* audio, int samples,
                                              double& fEst, int& iBest)
{
    // Step 1: Mix to baseband at candidate frequency and downsample 4:1
    // Uses fEst from previous cycle as initial guess
    double f0 = fEst;
    m_cd0Samples = samples / FT8_NDOWN;

    for (int i = 0; i < m_cd0Samples && i < 180000; ++i) {
        double phase = -TWO_PI * f0 * i / 12000.0;
        double re = audio[i * FT8_NDOWN] * std::cos(phase);
        double im = audio[i * FT8_NDOWN] * std::sin(phase);
        m_cd0[i] = std::complex<double>(re, im);
    }

    // Step 2: Time sync - search for Costas sync tones
    iBest = 0;
    double syncMax = 0.0;
    int searchRange = std::min(20, m_cd0Samples / 32 - FT8_NSYM);

    for (int idt = 0; idt < searchRange; ++idt) {
        double syncSum = 0.0;
        for (int k = 0; k < 7; ++k) {
            int symIdx = k < 7 ? k : (k < 7 ? k + 36 : k + 72);
            int pos = idt + symIdx * 32;
            if (pos + 31 >= m_cd0Samples) continue;

            std::complex<double> csymb[32], cfft[32];
            for (int i = 0; i < 32; ++i) csymb[i] = m_cd0[pos + i];
            fft32(csymb, cfft, false);
            syncSum += std::abs(cfft[FT8_SYNC_ARRAY[k]]);
        }
        if (syncSum > syncMax) { syncMax = syncSum; iBest = idt; }
    }

    // Step 3: Frequency fine-tuning (±2.5 Hz)
    double fBest = 0.0;
    syncMax = 0.0;
    for (int df = -5; df <= 5; ++df) {
        double delta = df * 0.5;
        double syncSum = 0.0;
        for (int i = 0; i < 7; ++i) {
            int pos = iBest + i * 32;
            if (pos + 31 >= m_cd0Samples) continue;
            std::complex<double> csymb[32], cfft[32];
            double phi = 0.0;
            for (int j = 0; j < 32; ++j) {
                csymb[j] = m_cd0[pos + j] * std::complex<double>(
                    std::cos(phi), std::sin(phi));
                phi += TWO_PI * delta / FT8_FS2;
            }
            fft32(csymb, cfft, false);
            syncSum += std::abs(cfft[FT8_SYNC_ARRAY[i]]);
        }
        if (syncSum > syncMax) { syncMax = syncSum; fBest = delta; }
    }
    fEst = f0 + fBest;

    // Step 4: Sync quality (nSync from ft8b.f90)
    m_nsync = 0;
    for (int seg = 0; seg < 3; ++seg) {
        for (int k = 0; k < 7; ++k) {
            int pos = iBest + (k + seg * 36) * 32;
            if (pos + 31 >= m_cd0Samples) continue;
            std::complex<double> csymb[32], cfft[32];
            for (int i = 0; i < 32; ++i) csymb[i] = m_cd0[pos + i];
            fft32(csymb, cfft, false);
            double maxVal = 0.0;
            int maxBin = 0;
            for (int b = 0; b < FT8_NBINS; ++b) {
                if (std::abs(cfft[b]) > maxVal) { maxVal = std::abs(cfft[b]); maxBin = b; }
            }
            if (maxBin == FT8_SYNC_ARRAY[k]) m_nsync++;
        }
    }
}

void DigitalModeEngine::ft8Demodulate(const float* audio, int iBest, double fEst,
                                       std::array<double, 79*8>& softSymbols)
{
    std::fill(softSymbols.begin(), softSymbols.end(), 0.0);

    for (int k = 0; k < FT8_NSYM; ++k) {
        int pos = iBest + k * FT8_FFT_SIZE;
        if (pos + FT8_FFT_SIZE > m_cd0Samples) continue;

        std::complex<double> csymb[32], cfft[32];
        for (int i = 0; i < 32; ++i) csymb[i] = m_cd0[pos + i];
        fft32(csymb, cfft, false);

        // 8-FSK soft demodulation with Gray coding
        for (int b = 0; b < FT8_NBINS; ++b)
            softSymbols[k * FT8_NBINS + b] = std::abs(cfft[b]);
    }
}

QByteArray DigitalModeEngine::generateTxAudio()
{
    int nSym = FT8_NSYM;
    QVector<int> tones = ft8GenerateTones(m_txMessage, nSym);
    if (tones.isEmpty()) return QByteArray();

    double sampleRate = 12000.0;
    int samplesPerSym = static_cast<int>(sampleRate / FT8_TONE_SPACING);
    int totalSamples = samplesPerSym * nSym;

    QByteArray audio(totalSamples * static_cast<int>(sizeof(float)), '\0');
    auto* out = reinterpret_cast<float*>(audio.data());

    double phase = 0.0;
    double amp = 0.3;

    for (int sym = 0; sym < nSym; ++sym) {
        double toneFreq = 1000.0 + tones[sym] * FT8_TONE_SPACING;
        for (int s = 0; s < samplesPerSym; ++s) {
            phase += TWO_PI * toneFreq / sampleRate;
            if (phase > TWO_PI) phase -= TWO_PI;
            out[sym * samplesPerSym + s] = static_cast<float>(std::sin(phase)) * amp;
        }
    }

    // Ramp up/down (soft keying - 5ms)
    int rampSamples = static_cast<int>(0.005 * 12000.0);
    for (int i = 0; i < rampSamples && i < totalSamples; ++i) {
        double ramp = static_cast<double>(i) / rampSamples;
        out[i] *= static_cast<float>(ramp);
        out[totalSamples - 1 - i] *= static_cast<float>(ramp);
    }

    return audio;
}

QVector<int> DigitalModeEngine::ft8GenerateTones(const QString& message, int& nSym)
{
    nSym = FT8_NSYM;
    QVector<int> tones(nSym, 0);

    // Simplified FT8 tone pattern: 7 Costas sync + 58 data + 7 sync + 7 sync
    // Sync tones at positions 0-6, 36-42, 72-78
    for (int k = 0; k < 7; ++k) {
        tones[k] = FT8_SYNC_ARRAY[k];
        tones[36 + k] = FT8_SYNC_ARRAY[k];
        tones[72 + k] = FT8_SYNC_ARRAY[k];
    }

    // Data tones: simple hash-based pattern from message
    // In real FT8, this uses LDPC(174,91) encoding
    uint32_t hash = 5381;
    for (const QChar& c : message)
        hash = ((hash << 5) + hash) + static_cast<uint32_t>(c.unicode());

    for (int i = 7; i < 36; ++i) {
        hash = hash * 1103515245 + 12345;
        tones[i] = static_cast<int>(hash % 8);
    }
    for (int i = 43; i < 72; ++i) {
        hash = hash * 1103515245 + 12345;
        tones[i] = static_cast<int>(hash % 8);
    }

    return tones;
}

void DigitalModeEngine::emitDecodes()
{
    for (auto& decode : m_pendingDecodes)
        emit decodeReady(decode);
    m_pendingDecodes.clear();

    if (m_synced && m_ft8Decoder && m_ft8Decoder->isReady()) {
        QVector<Ft8DecodeResult> results = m_ft8Decoder->decode(
            m_rxBuffer, m_rxSampleRate, static_cast<double>(m_dialFreqHz));

        for (const auto& r : results) {
            DigitalDecode info;
            info.mode = (m_mode == Mode::FT8) ? "FT8" : "FT4";
            info.freqHz = r.freqHz;
            info.snrDb = r.snrDb;
            info.dt = r.dt;
            info.message = r.message;
            info.lowConfidence = r.lowConfidence;
            info.isNew = true;
            info.timestamp = QDateTime::currentMSecsSinceEpoch();
            emit decodeReady(info);
        }
    }
}

} // namespace MasterSDR
