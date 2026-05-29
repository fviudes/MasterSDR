#pragma once

#include "core/IcomCivProtocol.h"
#include "core/KenwoodCatProtocol.h"

#include <QObject>
#include <QSerialPort>
#include <QTimer>
#include <QByteArray>
#include <functional>

namespace MasterSDR {

enum class CatProtocolType {
    None,
    IcomCiv,
    KenwoodCat,
    Rigctl
};

class SerialCatPort : public QObject {
    Q_OBJECT

public:
    explicit SerialCatPort(QObject* parent = nullptr);
    ~SerialCatPort() override;

    void setProtocolType(CatProtocolType type) { m_protocolType = type; }
    CatProtocolType protocolType() const { return m_protocolType; }

    void setCivAddress(uint8_t addr) { m_civAddr = addr; }
    uint8_t civAddress() const { return m_civAddr; }

    bool openPort(const QString& portName, qint32 baudRate,
                  QSerialPort::DataBits dataBits = QSerialPort::Data8,
                  QSerialPort::StopBits stopBits = QSerialPort::OneStop,
                  QSerialPort::Parity parity = QSerialPort::NoParity);
    void closePort();
    bool isOpen() const;

    void sendFrequency(uint64_t freqHz);
    void sendMode(const QString& mode);
    void sendPtt(bool tx);
    void sendSplitFreq(uint64_t freqHz);
    void sendSplit(bool enabled);
    void requestUpdate();

    using FreqCallback = std::function<void(uint64_t freqHz)>;
    using ModeCallback = std::function<void(const QString& mode)>;
    using StatusCallback = std::function<void(uint64_t freq, const QString& mode,
                                              bool ptt, int sMeter)>;

    void onFrequencyReceived(FreqCallback cb) { m_freqCallback = std::move(cb); }
    void onModeReceived(ModeCallback cb) { m_modeCallback = std::move(cb); }
    void onStatusReceived(StatusCallback cb) { m_statusCallback = std::move(cb); }

signals:
    void portOpened(const QString& portName);
    void portClosed();
    void portError(const QString& message);
    void frequencyUpdated(uint64_t freqHz);
    void modeUpdated(const QString& mode);
    void pttUpdated(bool tx);
    void radioIdReceived(const QString& id);

private slots:
    void onReadyRead();
    void onPollTimer();

private:
    void processIcomData();
    void processKenwoodData();
    void processIcomResponse(const CivResponse& resp);
    void processKenwoodResponse(const KenwoodResponse& resp);
    void sendRaw(const QByteArray& data);

    QSerialPort* m_serialPort{nullptr};
    QTimer* m_pollTimer{nullptr};
    QByteArray m_readBuffer;
    CatProtocolType m_protocolType{CatProtocolType::None};
    uint8_t m_civAddr{IcomCivProtocol::DEFAULT_CI_V_ADDR};
    IcomCivProtocol m_civProto;
    KenwoodCatProtocol m_kwProto;

    FreqCallback m_freqCallback;
    ModeCallback m_modeCallback;
    StatusCallback m_statusCallback;

    static constexpr int POLL_INTERVAL_MS = 500;
};

} // namespace MasterSDR
