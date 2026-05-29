#pragma once

#include "core/HermesDiscovery.h"
#include "core/HermesConnection.h"

#include <QObject>

namespace MasterSDR {

class HermesModel : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool connected READ isConnected NOTIFY connectedChanged)
    Q_PROPERTY(QString radioName READ radioName NOTIFY radioInfoChanged)
    Q_PROPERTY(QString gatewareVersion READ gatewareVersion NOTIFY radioInfoChanged)
    Q_PROPERTY(QString macAddress READ macAddress NOTIFY radioInfoChanged)
    Q_PROPERTY(int numReceivers READ numReceivers NOTIFY radioInfoChanged)
    Q_PROPERTY(float temperature READ temperature NOTIFY temperatureUpdated)
    Q_PROPERTY(int forwardPower READ forwardPower NOTIFY powerUpdated)
    Q_PROPERTY(int reversePower READ reversePower NOTIFY powerUpdated)
    Q_PROPERTY(float swr READ swr NOTIFY powerUpdated)
    Q_PROPERTY(bool ptt READ isPtt NOTIFY pttChanged)

public:
    explicit HermesModel(QObject* parent = nullptr);
    ~HermesModel() override;

    HermesDiscovery* discovery() { return m_discovery; }
    HermesConnection* connection() { return m_connection; }

    bool isConnected() const { return m_connected; }
    QString radioName() const { return m_radioName; }
    QString gatewareVersion() const { return m_gatewareVersion; }
    QString macAddress() const { return m_macAddress; }
    int numReceivers() const { return m_numReceivers; }
    float temperature() const { return m_temperature; }
    int forwardPower() const { return m_forwardPower; }
    int reversePower() const { return m_reversePower; }
    float swr() const;
    bool isPtt() const { return m_ptt; }

public slots:
    void connectToRadio(const HermesRadioInfo& info);
    void disconnectFromRadio();
    void setFrequency(uint32_t freqHz);
    void setMox(bool active);

signals:
    void connectedChanged(bool connected);
    void radioInfoChanged();
    void temperatureUpdated(float tempC);
    void powerUpdated(int fwd, int rev);
    void pttChanged(bool ptt);

private:
    void connectConnectionSignals();

    HermesDiscovery* m_discovery{nullptr};
    HermesConnection* m_connection{nullptr};
    HermesRadioInfo m_currentRadio;
    bool m_connected{false};
    QString m_radioName;
    QString m_gatewareVersion;
    QString m_macAddress;
    int m_numReceivers{1};
    float m_temperature{0.0f};
    int m_forwardPower{0};
    int m_reversePower{0};
    bool m_ptt{false};
};

} // namespace MasterSDR
