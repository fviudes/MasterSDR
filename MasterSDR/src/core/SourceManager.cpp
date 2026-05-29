#include "core/SourceManager.h"

#include <QDebug>

namespace MasterSDR {

SourceManager::SourceManager(QObject* parent)
    : QObject(parent)
{
}

SourceManager::~SourceManager()
{
    clear();
}

void SourceManager::registerBackend(ISourceBackend* backend)
{
    if (!backend) return;
    backend->setParent(this);
    m_backends.append(backend);
}

void SourceManager::setActiveBackend(ISourceBackend* backend)
{
    if (m_active == backend) return;
    if (m_active) disconnectBackendSignals(m_active);
    m_active = backend;
    if (m_active) connectBackendSignals(m_active);
}

void SourceManager::clear()
{
    if (m_active) disconnectBackendSignals(m_active);
    m_active = nullptr;
    qDeleteAll(m_backends);
    m_backends.clear();
}

void SourceManager::connectBackendSignals(ISourceBackend* backend)
{
    connect(backend, &ISourceBackend::stateChanged, this, &SourceManager::stateChanged);
    connect(backend, &ISourceBackend::connected, this, &SourceManager::connected);
    connect(backend, &ISourceBackend::disconnected, this, &SourceManager::disconnected);
    connect(backend, &ISourceBackend::errorOccurred, this, &SourceManager::errorOccurred);
    connect(backend, &ISourceBackend::frequencyUpdated, this, &SourceManager::frequencyUpdated);
    connect(backend, &ISourceBackend::modeUpdated, this, &SourceManager::modeUpdated);
    connect(backend, &ISourceBackend::sMeterUpdated, this, &SourceManager::sMeterUpdated);
    connect(backend, &ISourceBackend::pttStateChanged, this, &SourceManager::pttStateChanged);
    connect(backend, &ISourceBackend::iqDataReady, this, &SourceManager::iqDataReady);
    connect(backend, &ISourceBackend::spectrumDataReady, this, &SourceManager::spectrumDataReady);
    connect(backend, &ISourceBackend::audioDataReady, this, &SourceManager::audioDataReady);
    connect(backend, &ISourceBackend::radioInfoUpdated, this, &SourceManager::radioInfoUpdated);
}

void SourceManager::disconnectBackendSignals(ISourceBackend* backend)
{
    disconnect(backend, nullptr, this, nullptr);
}

} // namespace MasterSDR
