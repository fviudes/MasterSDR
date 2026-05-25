#pragma once

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QVector>
#include <functional>

namespace MasterSDR {

struct Ft8DecodeResult {
    double  freqHz{0.0};
    double  snrDb{0.0};
    double  dt{0.0};
    QString message;
    bool    lowConfidence{false};
    bool    isValid{false};
};

class Ft8Decoder : public QObject {
    Q_OBJECT

public:
    enum class Backend {
        Native,     // Fortran decoder compiled as shared library
        UdpProxy,   // Connect to running WSJT-X via UDP
        Simulation  // Generate synthetic decodes
    };

    explicit Ft8Decoder(QObject* parent = nullptr);
    ~Ft8Decoder() override;

    Backend backend() const { return m_backend; }
    QString backendName() const;

    bool initialize();
    void shutdown();
    bool isReady() const { return m_ready; }

    void setCallsign(const QString& myCall, const QString& myGrid);
    void setFrequencyRange(double lowHz, double highHz);

    QVector<Ft8DecodeResult> decode(const QByteArray& float32Pcm,
                                     int sampleRate,
                                     double dialFreqHz);

    void setAprioriCallback(std::function<void(int, QByteArray&)> cb);

signals:
    void backendChanged(Backend backend);

private:
    bool tryLoadNativeLibrary();
    bool tryConnectUdpProxy();
    QVector<Ft8DecodeResult> decodeNative(const QByteArray& audio, int rate, double dialHz);
    QVector<Ft8DecodeResult> decodeSimulation(double dialHz);

    Backend m_backend{Backend::Simulation};
    bool m_ready{false};
    QString m_myCall;
    QString m_myGrid;
    double m_freqLow{0.0};
    double m_freqHigh{5000.0};

    // Native library handles
    void* m_libHandle{nullptr};
    using Ft8DecodeFunc = int (*)(const float* audio, int samples, int rate,
                                   double dialFreq, double freqLow, double freqHigh,
                                   char* callsign, char* grid,
                                   double* freqs, double* snrs, double* dts,
                                   char* messages, int maxResults);
    Ft8DecodeFunc m_ft8Decode{nullptr};
};

} // namespace MasterSDR
