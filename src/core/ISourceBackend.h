#pragma once

#include <QObject>
#include <QByteArray>
#include <QString>
#include <cstdint>

namespace MasterSDR {

// Abstract interface for all radio source backends.
// Each radio type (FlexRadio, Hermes, Icom IP, Kenwood, etc.)
// implements this interface to provide a unified API for
// frequency control, I/Q streaming, spectrum data, and audio.
class ISourceBackend : public QObject {
    Q_OBJECT

public:
    enum class Type { FlexRadio, Hermes, IcomIp, Kenwood, SerialCat };
    enum class State { Disconnected, Connecting, Connected, Error };
    Q_ENUM(State)

    explicit ISourceBackend(QObject* parent = nullptr) : QObject(parent) {}
    ~ISourceBackend() override = default;

    // ── Connection lifecycle ──────────────────────────────
    virtual void connectToRadio() = 0;
    virtual void disconnectFromRadio() = 0;
    virtual State state() const = 0;
    virtual Type type() const = 0;

    // ── Frequency control ─────────────────────────────────
    virtual void setFrequency(uint64_t freqHz) = 0;
    virtual uint64_t frequency() const = 0;

    // ── Mode control ──────────────────────────────────────
    virtual void setMode(const QString& mode) { Q_UNUSED(mode); }
    virtual QString mode() const { return {}; }

    // ── PTT ───────────────────────────────────────────────
    virtual void setPtt(bool tx) { Q_UNUSED(tx); }
    virtual bool isPtt() const { return false; }

    // ── S-meter ───────────────────────────────────────────
    virtual int sMeterLevel() const { return 0; }

    // ── I/Q stream ────────────────────────────────────────
    virtual QByteArray readIQ() { return {}; }

    // ── Spectrum/waterfall ────────────────────────────────
    virtual QByteArray readSpectrum() { return {}; }
    virtual int spectrumSize() const { return 0; }
    virtual double spectrumCenterHz() const { return 0; }
    virtual double spectrumSpanHz() const { return 0; }

    // ── Audio RX ──────────────────────────────────────────
    virtual QByteArray readAudio() { return {}; }
    virtual int audioSampleRate() const { return 0; }

    // ── Audio TX ──────────────────────────────────────────
    virtual void writeAudio(const QByteArray& pcm) { Q_UNUSED(pcm); }

    // ── Radio identification ──────────────────────────────
    virtual QString radioName() const { return m_radioName; }
    virtual QString radioModel() const { return m_radioModel; }
    virtual QString radioVersion() const { return m_radioVersion; }

signals:
    void stateChanged(State newState);
    void connected();
    void disconnected();
    void errorOccurred(const QString& message);
    void frequencyUpdated(uint64_t freqHz);
    void modeUpdated(const QString& mode);
    void sMeterUpdated(int level);
    void pttStateChanged(bool tx);
    void iqDataReady(const QByteArray& samples);
    void spectrumDataReady(const QByteArray& data);
    void audioDataReady(const QByteArray& pcm);
    void radioInfoUpdated();

protected:
    QString m_radioName;
    QString m_radioModel;
    QString m_radioVersion;
};

} // namespace MasterSDR
