#pragma once

#include "core/ISourceBackend.h"

#include <QObject>
#include <QMap>

namespace MasterSDR {

// Manages multiple radio source backends.
// Routes the active backend's signals to the UI.
class SourceManager : public QObject {
    Q_OBJECT

public:
    explicit SourceManager(QObject* parent = nullptr);
    ~SourceManager() override;

    // Register a backend (ownership transferred to SourceManager)
    void registerBackend(ISourceBackend* backend);

    // Set the active backend (connects signals, disconnects previous)
    void setActiveBackend(ISourceBackend* backend);
    ISourceBackend* activeBackend() const { return m_active; }

    // Convenience getters
    template<typename T> T* backend() const {
        for (auto* b : m_backends) {
            if (auto* casted = dynamic_cast<T*>(b)) return casted;
        }
        return nullptr;
    }

    // Disconnect and remove all backends
    void clear();

signals:
    // Proxied from active backend
    void stateChanged(ISourceBackend::State state);
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

private:
    void connectBackendSignals(ISourceBackend* backend);
    void disconnectBackendSignals(ISourceBackend* backend);

    QList<ISourceBackend*> m_backends;
    ISourceBackend* m_active{nullptr};
};

} // namespace MasterSDR
