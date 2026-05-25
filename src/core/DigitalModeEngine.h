#pragma once

#include "core/Ft8Decoder.h"

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QTimer>
#include <QElapsedTimer>
#include <QVector>
#include <QDateTime>
#include <complex>
#include <cmath>
#include <array>

namespace MasterSDR {

struct DigitalDecode {
    QString mode;
    double  freqHz{0.0};
    double  snrDb{0.0};
    double  dt{0.0};
    QString message;
    bool    lowConfidence{false};
    bool    isNew{true};
    qint64  timestamp{0};
};

class DigitalModeEngine : public QObject {
    Q_OBJECT

public:
    enum class Mode {
        FT8, FT4, JT65, JT9, WSPR, Q65, FST4, MSK144
    };
    Q_ENUM(Mode)

    enum class TrState { Rx, PreTx, Tx, PostTx };
    Q_ENUM(TrState)

    explicit DigitalModeEngine(QObject* parent = nullptr);

    void setMode(Mode mode);
    Mode mode() const { return m_mode; }
    void setDialFrequency(uint64_t hz) { m_dialFreqHz = hz; }
    uint64_t dialFrequency() const { return m_dialFreqHz; }
    void setRxOffset(uint32_t dfHz) { m_rxOffsetHz = dfHz; }
    uint32_t rxOffset() const { return m_rxOffsetHz; }
    void setTxOffset(uint32_t dfHz) { m_txOffsetHz = dfHz; }
    void setTxMessage(const QString& msg) { m_txMessage = msg; }
    QString txMessage() const { return m_txMessage; }
    void setDxCall(const QString& call) { m_dxCall = call; }
    void setDxGrid(const QString& grid) { m_dxGrid = grid; }
    void enableTx(bool on);
    bool isTxEnabled() const { return m_txEnabled; }
    double trPeriodSeconds() const;
    TrState trState() const { return m_trState; }
    double sequenceProgress() const;
    int secondsToNextTx() const;
    int nsync() const { return m_nsync; }

public slots:
    void feedRxAudio(const QByteArray& float32Pcm, int sampleRate);
    void start();
    void stop();
    void reset();

signals:
    void decodeReady(const DigitalDecode& decode);
    void trStateChanged(TrState state);
    void pttRequested(bool on);
    void txAudioReady(const QByteArray& float32Pcm, int sampleRate);
    void syncDetected(bool synced);
    void snrUpdated(float snrDb);
    void sequenceProgressUpdated(int secondsRemaining, double progress);
    void syncQualityChanged(int quality);

private:
    void advanceTrSequence();
    void processRxBuffer();
    void emitDecodes();
    QByteArray generateTxAudio();
    int positionInSequenceMs() const;

    // FT8 signal processing (matches WSJT-X architecture)
    void ft8DownsampleAndSync(const float* audio, int samples, double& fEst, int& iBest);
    void ft8Demodulate(const float* audio, int iBest, double fEst,
                       std::array<double, 79*8>& softSymbols);
    QVector<int> ft8GenerateTones(const QString& message, int& nSym);

    Mode m_mode{Mode::FT8};
    TrState m_trState{TrState::Rx};
    uint64_t m_dialFreqHz{14074000};
    uint32_t m_rxOffsetHz{1500};
    uint32_t m_txOffsetHz{1500};
    QString m_txMessage;
    QString m_dxCall;
    QString m_dxGrid;
    bool m_txEnabled{false};
    bool m_running{false};

    // RX buffer
    QByteArray m_rxBuffer;
    int m_rxSampleRate{12000};
    double m_lastSnrDb{-99.0};
    bool m_synced{false};

    // TX audio
    QByteArray m_txAudio;
    int m_txAudioPos{0};

    QVector<DigitalDecode> m_pendingDecodes;
    QElapsedTimer m_elapsed;
    QTimer* m_sequenceTimer{nullptr};
    Ft8Decoder* m_ft8Decoder{nullptr};

    // Signal processing state (matches dec_data_t)
    std::array<std::complex<double>, 180000> m_cd0;
    int m_cd0Samples{0};
    double m_fEst{0.0};
    int m_iBest{0};
    int m_nsync{0};
    int m_nhardErrors{-1};

    // FT8 constants (from ft8_params.f90)
    static constexpr int FT8_NSYM = 79;
    static constexpr int FT8_NDOWN = 4;
    static constexpr double FT8_FS2 = 12000.0 / FT8_NDOWN;
    static constexpr double FT8_DT2 = 1.0 / FT8_FS2;
    static constexpr double FT8_TONE_SPACING = 6.25;
    static constexpr int FT8_NP2 = 2812;
    static constexpr int FT8_FFT_SIZE = 32;
    static constexpr int FT8_NBINS = 8;

    // FT8 Costas sync array (flipped from original: 3,1,4,0,6,5,2)
    static constexpr int FT8_SYNC_ARRAY[7] = {3, 1, 4, 0, 6, 5, 2};

    // FT8 Gray coding map
    static constexpr int FT8_GRAY[8] = {0, 1, 3, 2, 6, 4, 7, 5};
};

} // namespace MasterSDR
