#pragma once

#include <QByteArray>
#include <QString>
#include <cstdint>

namespace MasterSDR {

struct KenwoodResponse {
    QString command;
    QString data;
    bool valid{false};
};

class KenwoodCatProtocol {
public:
    KenwoodCatProtocol() = default;

    QByteArray buildCommand(const QString& cmd) const;
    QByteArray buildReadFreqA() const;        // FA;
    QByteArray buildReadFreqB() const;        // FB;
    QByteArray buildSetFreqA(uint64_t freqHz) const;
    QByteArray buildSetFreqB(uint64_t freqHz) const;
    QByteArray buildReadMode() const;         // IF;
    QByteArray buildSetMode(const QString& mode) const;
    QByteArray buildSetPtt(bool tx) const;
    QByteArray buildReadPtt() const;
    QByteArray buildReadId() const;           // ID;
    QByteArray buildSetAutoInfo(bool on) const;
    QByteArray buildSetAg(uint8_t level) const;
    QByteArray buildReadStatus() const;       // IF;

    KenwoodResponse parseResponse(const QByteArray& data) const;
    static bool isCompleteFrame(const QByteArray& buffer);

    static QString modeToKenwood(const QString& flexMode);
    static QString kenwoodToFlexMode(const QString& kwMode);
    static uint64_t parseFrequency(const QString& freqStr);

    static constexpr char TERMINATOR = ';';

private:
    QString frequencyToString(uint64_t freqHz) const;
};

} // namespace MasterSDR
