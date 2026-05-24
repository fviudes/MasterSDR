#pragma once

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QTimer>
#include <QElapsedTimer>
#include <QVector>
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
        FT8,
        FT4,
        JT65,
        JT9,
        WSPR,
        Q65,
        FST4,
        MSK144
    };
    Q_ENUM(Mode)

    explicit DigitalModeEngine(QObject* parent = nullptr);

    void setMode(Mode mode);
    Mode mode() const { return m_mode; }

    void setDialFrequency(uint64_t hz);
    uint64_t dialFrequency() const { return m_dialFreqHz; }

    void setRxFrequency(uint32_t dfHz);
    uint32_t rxFrequency() const { return m_rxOffsetHz; }

    void setTxFrequency(uint32_t dfHz);
    uint32_t txFrequency() const { return m_txOffsetHz; }

    void setTxMessage(const QString& msg);
    QString txMessage() const { return m_txMessage; }

    void setDxCall(const QString& call);
    QString dxCall() const { return m_dxCall; }

    void setDxGrid(const QString& grid);
    QString dxGrid() const { return m_dxGrid; }

    void enableTx(bool on);
    bool isTxEnabled() const { return m_txEnabled; }

    double trPeriodSeconds() const;

public slots:
    void feedRxAudio(const QByteArray& float32Pcm, int sampleRate);
    void start();
    void stop();
    void reset();

signals:
    void decodeReady(const DigitalDecode& decode);
    void statusChanged();
    void syncDetected(bool synced);
    void snrUpdated(float snrDb);
    void txAudioReady(const QByteArray& float32Mono, int sampleRate);
    void rxOffsetChanged(uint32_t dfHz);

private:
    void generateFt8Tones();
    void generateFt4Tones();
    void processRxBuffer();
    void emitDecodes();
    QByteArray generateTxTones();
    void advanceTxSequence();

    Mode m_mode{Mode::FT8};
    uint64_t m_dialFreqHz{14074000};
    uint32_t m_rxOffsetHz{1500};
    uint32_t m_txOffsetHz{1500};
    QString m_txMessage;
    QString m_dxCall;
    QString m_dxGrid;
    bool m_txEnabled{false};
    bool m_transmitting{false};
    bool m_decoding{false};
    bool m_running{false};

    QByteArray m_rxBuffer;
    int m_rxSampleRate{12000};

    QByteArray m_txBuffer;
    int m_txBufferPos{0};
    int m_txSamplesPerSymbol{0};
    int m_txSymbolCount{79};
    int m_txCurrentSymbol{0};
    double m_txPhase{0.0};

    QVector<DigitalDecode> m_pendingDecodes;
    QElapsedTimer m_sequenceTimer;
    qint64 m_sequenceStartMs{0};
    double m_lastSnrDb{-99.0};
    bool m_synced{false};

    static constexpr int RX_BUFFER_MAX = 48000 * 30;
    static constexpr double FT8_TONE_SPACING = 6.25;
    static constexpr double FT4_TONE_SPACING = 23.4375;
    static constexpr int FT8_SYMBOLS = 79;
    static constexpr int FT4_SYMBOLS = 105;
    static constexpr double FT8_SYMBOL_RATE = 6.25;
    static constexpr double FT4_SYMBOL_RATE = 23.4375;
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

} // namespace MasterSDR
