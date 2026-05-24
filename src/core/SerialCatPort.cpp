#include "core/SerialCatPort.h"

#include <QDebug>

namespace MasterSDR {

SerialCatPort::SerialCatPort(QObject* parent)
    : QObject(parent)
    , m_civProto(IcomCivProtocol::DEFAULT_CI_V_ADDR)
{
    m_serialPort = new QSerialPort(this);
    connect(m_serialPort, &QSerialPort::readyRead, this, &SerialCatPort::onReadyRead);

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(POLL_INTERVAL_MS);
    connect(m_pollTimer, &QTimer::timeout, this, &SerialCatPort::onPollTimer);
}

SerialCatPort::~SerialCatPort()
{
    closePort();
}

bool SerialCatPort::openPort(const QString& portName, qint32 baudRate,
                              QSerialPort::DataBits dataBits,
                              QSerialPort::StopBits stopBits,
                              QSerialPort::Parity parity)
{
    m_serialPort->setPortName(portName);
    m_serialPort->setBaudRate(baudRate);
    m_serialPort->setDataBits(dataBits);
    m_serialPort->setStopBits(stopBits);
    m_serialPort->setParity(parity);
    m_serialPort->setFlowControl(QSerialPort::NoFlowControl);

    if (!m_serialPort->open(QIODevice::ReadWrite)) {
        QString err = QString("Failed to open %1: %2").arg(portName, m_serialPort->errorString());
        emit portError(err);
        qWarning() << err;
        return false;
    }

    m_serialPort->clear();
    m_readBuffer.clear();

    qDebug() << "SerialCatPort opened:" << portName << "@" << baudRate << "baud"
             << "protocol:" << static_cast<int>(m_protocolType);

    if (m_protocolType == CatProtocolType::KenwoodCat) {
        sendRaw(m_kwProto.buildSetAutoInfo(true));
    }

    m_pollTimer->start();
    emit portOpened(portName);
    return true;
}

void SerialCatPort::closePort()
{
    m_pollTimer->stop();
    if (m_serialPort->isOpen()) {
        m_serialPort->close();
    }
    m_readBuffer.clear();
    emit portClosed();
}

bool SerialCatPort::isOpen() const
{
    return m_serialPort->isOpen();
}

void SerialCatPort::sendFrequency(uint64_t freqHz)
{
    QByteArray cmd;
    if (m_protocolType == CatProtocolType::IcomCiv) {
        cmd = m_civProto.buildSetFreq(freqHz);
    } else if (m_protocolType == CatProtocolType::KenwoodCat) {
        cmd = m_kwProto.buildSetFreqA(freqHz);
    }
    sendRaw(cmd);
}

void SerialCatPort::sendMode(const QString& mode)
{
    QByteArray cmd;
    if (m_protocolType == CatProtocolType::IcomCiv) {
        cmd = m_civProto.buildSetMode(IcomCivProtocol::modeFromString(mode));
    } else if (m_protocolType == CatProtocolType::KenwoodCat) {
        cmd = m_kwProto.buildSetMode(mode);
    }
    sendRaw(cmd);
}

void SerialCatPort::sendPtt(bool tx)
{
    QByteArray cmd;
    if (m_protocolType == CatProtocolType::IcomCiv) {
        cmd = m_civProto.buildSetPtt(tx);
    } else if (m_protocolType == CatProtocolType::KenwoodCat) {
        cmd = m_kwProto.buildSetPtt(tx);
    }
    sendRaw(cmd);
}

void SerialCatPort::sendSplitFreq(uint64_t freqHz)
{
    QByteArray cmd;
    if (m_protocolType == CatProtocolType::IcomCiv) {
        cmd = m_civProto.buildSetVfoB(freqHz);
    } else if (m_protocolType == CatProtocolType::KenwoodCat) {
        cmd = m_kwProto.buildSetFreqB(freqHz);
    }
    sendRaw(cmd);
}

void SerialCatPort::sendSplit(bool enabled)
{
    if (m_protocolType == CatProtocolType::IcomCiv) {
        sendRaw(m_civProto.buildSetSplit(enabled));
    }
}

void SerialCatPort::requestUpdate()
{
    QByteArray cmd;
    if (m_protocolType == CatProtocolType::IcomCiv) {
        cmd = m_civProto.buildReadFreq();
    } else if (m_protocolType == CatProtocolType::KenwoodCat) {
        cmd = m_kwProto.buildReadStatus();
    }
    sendRaw(cmd);
}

void SerialCatPort::sendRaw(const QByteArray& data)
{
    if (!m_serialPort->isOpen() || data.isEmpty()) return;
    m_serialPort->write(data);
    m_serialPort->flush();
}

void SerialCatPort::onReadyRead()
{
    m_readBuffer.append(m_serialPort->readAll());

    if (m_protocolType == CatProtocolType::IcomCiv) {
        processIcomData();
    } else if (m_protocolType == CatProtocolType::KenwoodCat) {
        processKenwoodData();
    }
}

void SerialCatPort::onPollTimer()
{
    if (!m_serialPort->isOpen()) return;

    if (m_protocolType == CatProtocolType::IcomCiv) {
        sendRaw(m_civProto.buildReadFreq());
        sendRaw(m_civProto.buildReadMode());
    } else if (m_protocolType == CatProtocolType::KenwoodCat) {
        sendRaw(m_kwProto.buildReadStatus());
    }
}

void SerialCatPort::processIcomData()
{
    while (IcomCivProtocol::isCompleteFrame(m_readBuffer)) {
        int termPos = m_readBuffer.indexOf(static_cast<char>(IcomCivProtocol::TERMINATOR));
        if (termPos < 0) break;

        QByteArray frame = m_readBuffer.left(termPos + 1);
        m_readBuffer.remove(0, termPos + 1);

        if (frame.size() < 6) continue;

        CivResponse resp = m_civProto.parseResponse(frame);
        if (!resp.valid) continue;

        processIcomResponse(resp);
    }
}

void SerialCatPort::processIcomResponse(const CivResponse& resp)
{
    switch (resp.cmd) {
    case IcomCivProtocol::CMD_FREQ: {
        uint64_t freq = IcomCivProtocol::decodeBcdFreq(resp.data);
        emit frequencyUpdated(freq);
        if (m_freqCallback) m_freqCallback(freq);
        break;
    }
    case IcomCivProtocol::CMD_MODE: {
        if (!resp.data.isEmpty()) {
            auto mode = static_cast<IcomCivProtocol::CivMode>(static_cast<uint8_t>(resp.data[0]));
            QString modeStr = IcomCivProtocol::modeToString(mode);
            emit modeUpdated(modeStr);
            if (m_modeCallback) m_modeCallback(modeStr);
        }
        break;
    }
    case IcomCivProtocol::CMD_S_METER: {
        if (!resp.data.isEmpty() && m_statusCallback) {
            int sMeter = static_cast<int>(static_cast<uint8_t>(resp.data[0]));
            m_statusCallback(0, QString(), false, sMeter);
        }
        break;
    }
    default:
        break;
    }
}

void SerialCatPort::processKenwoodData()
{
    while (KenwoodCatProtocol::isCompleteFrame(m_readBuffer)) {
        int termPos = m_readBuffer.indexOf(KenwoodCatProtocol::TERMINATOR);
        if (termPos < 0) break;

        QByteArray frame = m_readBuffer.left(termPos + 1);
        m_readBuffer.remove(0, termPos + 1);

        KenwoodResponse resp = m_kwProto.parseResponse(frame);
        if (!resp.valid) continue;

        processKenwoodResponse(resp);
    }
}

void SerialCatPort::processKenwoodResponse(const KenwoodResponse& resp)
{
    if (resp.command == "IF") {
        QStringList fields = resp.data.split(',');
        if (fields.size() >= 6) {
            uint64_t freq = KenwoodCatProtocol::parseFrequency(fields[0]);
            QString mode = KenwoodCatProtocol::kenwoodToFlexMode(fields[4]);

            emit frequencyUpdated(freq);
            emit modeUpdated(mode);
            if (m_freqCallback) m_freqCallback(freq);
            if (m_modeCallback) m_modeCallback(mode);
        }
    } else if (resp.command == "FA") {
        uint64_t freq = KenwoodCatProtocol::parseFrequency(resp.data);
        emit frequencyUpdated(freq);
        if (m_freqCallback) m_freqCallback(freq);
    } else if (resp.command == "ID") {
        emit radioIdReceived(resp.data.trimmed());
    }
}

} // namespace MasterSDR
