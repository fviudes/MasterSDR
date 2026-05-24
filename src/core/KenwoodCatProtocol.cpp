#include "core/KenwoodCatProtocol.h"

#include <QDebug>

namespace MasterSDR {

QByteArray KenwoodCatProtocol::buildCommand(const QString& cmd) const
{
    return cmd.toUtf8();
}

QByteArray KenwoodCatProtocol::buildReadFreqA() const
{
    return buildCommand("FA;");
}

QByteArray KenwoodCatProtocol::buildReadFreqB() const
{
    return buildCommand("FB;");
}

QByteArray KenwoodCatProtocol::buildSetFreqA(uint64_t freqHz) const
{
    return buildCommand(QString("FA%1;").arg(frequencyToString(freqHz)));
}

QByteArray KenwoodCatProtocol::buildSetFreqB(uint64_t freqHz) const
{
    return buildCommand(QString("FB%1;").arg(frequencyToString(freqHz)));
}

QByteArray KenwoodCatProtocol::buildReadMode() const
{
    return buildCommand("IF;");
}

QByteArray KenwoodCatProtocol::buildSetMode(const QString& mode) const
{
    return buildCommand(QString("MD%1;").arg(modeToKenwood(mode)));
}

QByteArray KenwoodCatProtocol::buildSetPtt(bool tx) const
{
    return buildCommand(tx ? "TX;" : "RX;");
}

QByteArray KenwoodCatProtocol::buildReadPtt() const
{
    return buildCommand("IF;");
}

QByteArray KenwoodCatProtocol::buildReadId() const
{
    return buildCommand("ID;");
}

QByteArray KenwoodCatProtocol::buildSetAutoInfo(bool on) const
{
    return buildCommand(on ? "AI1;" : "AI0;");
}

QByteArray KenwoodCatProtocol::buildSetAg(uint8_t level) const
{
    return buildCommand(QString("AG0%1;").arg(level, 3, 10, QChar('0')));
}

QByteArray KenwoodCatProtocol::buildReadStatus() const
{
    return buildCommand("IF;");
}

KenwoodResponse KenwoodCatProtocol::parseResponse(const QByteArray& data) const
{
    KenwoodResponse resp;

    QString str = QString::fromUtf8(data).trimmed();
    if (str.isEmpty() || !str.endsWith(TERMINATOR)) return resp;

    int delimPos = str.indexOf(' ');
    if (delimPos >= 0) {
        resp.command = str.left(delimPos);
        resp.data = str.mid(delimPos + 1).chopped(1);
    } else {
        resp.command = str.chopped(1);
    }

    if (!resp.command.isEmpty()) resp.valid = true;
    return resp;
}

bool KenwoodCatProtocol::isCompleteFrame(const QByteArray& buffer)
{
    return buffer.contains(TERMINATOR);
}

QString KenwoodCatProtocol::modeToKenwood(const QString& flexMode)
{
    if (flexMode == "LSB")  return "1";
    if (flexMode == "USB")  return "2";
    if (flexMode == "CW")   return "3";
    if (flexMode == "FM")   return "4";
    if (flexMode == "AM")   return "5";
    if (flexMode == "DIGU") return "6";
    if (flexMode == "CWR")  return "7";
    if (flexMode == "DIGL") return "8";
    if (flexMode == "RTTY") return "8";
    return "2";
}

QString KenwoodCatProtocol::kenwoodToFlexMode(const QString& kwMode)
{
    if (kwMode == "1") return "LSB";
    if (kwMode == "2") return "USB";
    if (kwMode == "3") return "CW";
    if (kwMode == "4") return "FM";
    if (kwMode == "5") return "AM";
    if (kwMode == "6") return "DIGU";
    if (kwMode == "7") return "CWR";
    if (kwMode == "8") return "DIGL";
    return "USB";
}

uint64_t KenwoodCatProtocol::parseFrequency(const QString& freqStr)
{
    return static_cast<uint64_t>(freqStr.toLongLong());
}

QString KenwoodCatProtocol::frequencyToString(uint64_t freqHz) const
{
    return QString("%1").arg(freqHz, 11, 10, QChar('0'));
}

} // namespace MasterSDR
