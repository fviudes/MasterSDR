#pragma once

#include "core/Ft8Decoder.h"

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QTimer>
#include <QElapsedTimer>
#include <QVector>
#include <QDateTime>
#include <cmath>

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

    void setDialFrequency(uint64_t hz);
    uint64_t dialFrequency() const { return m_dialFreqHz; }

    void setRxOffset(uint32_t dfHz);
    uint32_t rxOffset() const { return m_rxOffsetHz; }

    void setTxOffset(uint32_t dfHz);
    uint32_t txOffset() const { return m_txOffsetHz; }

    void setTxMessage(const QString& msg);
    QString txMessage() const { return m_txMessage; }

    void setDxCall(const QString& call);
    QString dxCall() const { return m_dxCall; }

    void setDxGrid(const QString& grid);
    QString dxGrid() const { return m_dxGrid; }

    void enableTx(bool on);
    bool isTxEnabled() const { return m_txEnabled; }

    double trPeriodSeconds() const;
    TrState trState() const { return m_trState; }

    double sequenceProgress() const;
    int secondsToNextTx() const;

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

private:
    void advanceTrSequence();
    void processRxBuffer();
    void emitDecodes();
    QByteArray generateTxAudio();
    int positionInSequenceMs() const;
    double toneSpacing() const;
    double symbolRate() const;
    int symbolsPerMessage() const;
    double nominalTxStartMs() const;

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

    QByteArray m_rxBuffer;
    int m_rxSampleRate{12000};
    double m_lastSnrDb{-99.0};
    bool m_synced{false};

    QByteArray m_txAudio;
    int m_txAudioPos{0};

    QVector<DigitalDecode> m_pendingDecodes;
    QElapsedTimer m_elapsed;
    QTimer* m_sequenceTimer{nullptr};
    Ft8Decoder* m_ft8Decoder{nullptr};
};

inline double DigitalModeEngine::trPeriodSeconds() const
{
    switch (m_mode) {
    case Mode::FT8:     return 15.0;
    case Mode::FT4:     return 7.5;
    case Mode::JT65:    return 60.0;
    case Mode::JT9:     return 60.0;
    case Mode::WSPR:    return 120.0;
    case Mode::Q65:     return 30.0;
    case Mode::FST4:    return 60.0;
    case Mode::MSK144:  return 15.0;
    default:            return 15.0;
    }
}

inline double DigitalModeEngine::toneSpacing() const
{
    switch (m_mode) {
    case Mode::FT8:     return 6.25;
    case Mode::FT4:     return 23.4375;
    case Mode::JT65:    return 2.6917;
    case Mode::JT9:     return 1.7361;
    default:            return 6.25;
    }
}

inline double DigitalModeEngine::symbolRate() const
{
    return toneSpacing();
}

inline int DigitalModeEngine::symbolsPerMessage() const
{
    switch (m_mode) {
    case Mode::FT8:  return 79;
    case Mode::FT4:  return 105;
    case Mode::JT65: return 126;
    case Mode::JT9:  return 85;
    default:         return 79;
    }
}

inline double DigitalModeEngine::nominalTxStartMs() const
{
    switch (m_mode) {
    case Mode::FT8:  return 500.0;
    case Mode::FT4:  return 300.0;
    case Mode::JT65: return 1000.0;
    case Mode::JT9:  return 1000.0;
    default:         return 500.0;
    }
}

} // namespace MasterSDR
