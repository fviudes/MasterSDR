#include "models/HermesModel.h"

#include <QDebug>

namespace MasterSDR {

HermesModel::HermesModel(QObject* parent)
    : QObject(parent)
{
    m_discovery = new HermesDiscovery(this);
    m_connection = new HermesConnection(this);
    connectConnectionSignals();
}

HermesModel::~HermesModel()
{
    disconnectFromRadio();
}

void HermesModel::connectConnectionSignals()
{
    connect(m_connection, &HermesConnection::connected, this, [this]() {
        m_connected = true;
        emit connectedChanged(true);
    });

    connect(m_connection, &HermesConnection::disconnected, this, [this]() {
        m_connected = false;
        emit connectedChanged(false);
    });

    connect(m_connection, &HermesConnection::temperatureUpdated, this, [this](float t) {
        m_temperature = t;
        emit temperatureUpdated(t);
    });

    connect(m_connection, &HermesConnection::powerUpdated, this, [this](uint16_t fwd, uint16_t rev) {
        m_forwardPower = static_cast<int>(fwd);
        m_reversePower = static_cast<int>(rev);
        emit powerUpdated(m_forwardPower, m_reversePower);
    });

    connect(m_connection, &HermesConnection::pttStateChanged, this, [this](bool ptt) {
        m_ptt = ptt;
        emit pttChanged(ptt);
    });
}

void HermesModel::connectToRadio(const HermesRadioInfo& info)
{
    m_currentRadio = info;

    QString boardName = (info.boardId == HermesProtocol::BOARD_ID_HL2)
        ? "Hermes Lite 2" : "Hermes Compatible";

    m_radioName = QString("%1 (%2)").arg(boardName, info.ipAddress);
    m_gatewareVersion = info.gatewareVersion;
    m_macAddress = info.mac;
    m_numReceivers = info.numReceivers;

    emit radioInfoChanged();

    m_connection->connectToRadio(info);

    qDebug() << "HermesModel: Connecting to" << m_radioName
             << "GW:" << m_gatewareVersion
             << "MAC:" << m_macAddress;
}

void HermesModel::disconnectFromRadio()
{
    m_connection->disconnectFromRadio();
}

void HermesModel::setFrequency(uint32_t freqHz)
{
    if (m_connected) {
        m_connection->setRX1Frequency(freqHz);
    }
}

void HermesModel::setMox(bool active)
{
    if (m_connected) {
        m_connection->setMox(active);
    }
}

} // namespace MasterSDR
