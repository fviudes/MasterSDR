#include "core/Ft8Decoder.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDateTime>
#include <QLibrary>
#include <QFile>
#include <QDir>
#include <algorithm>
#include <cstring>
#include <cmath>

namespace MasterSDR {

namespace {
    constexpr int MAX_DECODES = 50;

    // FT8 tone frequencies normalized index
    int ft8SyncTones[7] = {3, 1, 4, 0, 6, 5, 2};
}

Ft8Decoder::Ft8Decoder(QObject* parent)
    : QObject(parent)
{
}

Ft8Decoder::~Ft8Decoder()
{
    shutdown();
}

QString Ft8Decoder::backendName() const
{
    switch (m_backend) {
    case Backend::Native:     return "Native Fortran Decoder";
    case Backend::UdpProxy:   return "WSJT-X UDP Proxy";
    case Backend::Simulation: return "Simulation";
    default:                  return "Unknown";
    }
}

bool Ft8Decoder::initialize()
{
    if (tryLoadNativeLibrary()) return true;
    if (tryConnectUdpProxy()) return true;

    m_backend = Backend::Simulation;
    m_ready = true;
    emit backendChanged(m_backend);
    qDebug() << "Ft8Decoder: using" << backendName();
    return true;
}

void Ft8Decoder::shutdown()
{
    if (m_libHandle) {
        m_ft8Decode = nullptr;
        m_libHandle = nullptr;
    }
    m_ready = false;
}

void Ft8Decoder::setCallsign(const QString& myCall, const QString& myGrid)
{
    m_myCall = myCall;
    m_myGrid = myGrid;
}

void Ft8Decoder::setFrequencyRange(double lowHz, double highHz)
{
    m_freqLow = lowHz;
    m_freqHigh = highHz;
}

QVector<Ft8DecodeResult> Ft8Decoder::decode(const QByteArray& float32Pcm,
                                              int sampleRate,
                                              double dialFreqHz)
{
    switch (m_backend) {
    case Backend::Native:
        return decodeNative(float32Pcm, sampleRate, dialFreqHz);
    case Backend::UdpProxy:
        // UDP proxy: decode is async, results come via signal
        return {};
    case Backend::Simulation:
    default:
        return decodeSimulation(dialFreqHz);
    }
}

bool Ft8Decoder::tryLoadNativeLibrary()
{
#ifdef HAVE_FT8_NATIVE
    QStringList libPaths;
    libPaths << QDir::currentPath() + "/libft8decoder.so"
             << QDir::currentPath() + "/ft8decoder.dll"
             << QCoreApplication::applicationDirPath() + "/libft8decoder.so"
             << QCoreApplication::applicationDirPath() + "/ft8decoder.dll";

    for (const auto& path : libPaths) {
        m_libHandle = QLibrary(path).resolve("ft8_decode");
        if (m_libHandle) {
            m_ft8Decode = reinterpret_cast<Ft8DecodeFunc>(
                QLibrary(path).resolve("ft8_decode"));
            if (m_ft8Decode) {
                m_backend = Backend::Native;
                m_ready = true;
                emit backendChanged(m_backend);
                qDebug() << "Ft8Decoder: loaded native library from" << path;
                return true;
            }
        }
    }
#endif
    return false;
}

bool Ft8Decoder::tryConnectUdpProxy()
{
    // WSJT-X UDP proxy on localhost:2237
    // This would receive decodes from a running WSJT-X instance
    // For now, not implemented - would use network module
    return false;
}

QVector<Ft8DecodeResult> Ft8Decoder::decodeNative(const QByteArray& audio,
                                                    int rate,
                                                    double dialHz)
{
    QVector<Ft8DecodeResult> results;
    if (!m_ft8Decode || audio.isEmpty()) return results;

    double freqs[MAX_DECODES];
    double snrs[MAX_DECODES];
    double dts[MAX_DECODES];
    char msgBuf[MAX_DECODES * 38];

    const auto* samples = reinterpret_cast<const float*>(audio.constData());
    int sampleCount = audio.size() / static_cast<int>(sizeof(float));

    char call[13] = {};
    char grid[7] = {};
    std::strncpy(call, m_myCall.toLatin1().constData(), 12);
    std::strncpy(grid, m_myGrid.toLatin1().constData(), 6);

    int count = m_ft8Decode(samples, sampleCount, rate, dialHz,
                             m_freqLow, m_freqHigh,
                             call, grid,
                             freqs, snrs, dts, msgBuf, MAX_DECODES);

    for (int i = 0; i < count && i < MAX_DECODES; ++i) {
        Ft8DecodeResult r;
        r.freqHz = freqs[i];
        r.snrDb = snrs[i];
        r.dt = dts[i];
        r.message = QString::fromLatin1(msgBuf + i * 38).trimmed();
        r.isValid = !r.message.isEmpty();
        results.append(r);
    }

    return results;
}

QVector<Ft8DecodeResult> Ft8Decoder::decodeSimulation(double dialHz)
{
    QVector<Ft8DecodeResult> results;

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
        Ft8DecodeResult r;
        r.freqHz = dialHz + 1500.0 + (i - 5) * 60.0;
        r.snrDb = -5.0 - i * 2.5;
        r.dt = 0.1 + i * 0.15;
        r.message = msgs[i];
        r.lowConfidence = (r.snrDb < -20.0);
        r.isValid = true;
        results.append(r);
    }

    return results;
}

} // namespace MasterSDR
