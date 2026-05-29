#include "core/IcomCivProtocol.h"

#include <QDebug>

namespace MasterSDR {

IcomCivProtocol::IcomCivProtocol(uint8_t civAddr)
    : m_civAddr(civAddr)
{
}

QByteArray IcomCivProtocol::buildCommand(uint8_t cmd, uint8_t subCmd, const QByteArray& data) const
{
    QByteArray pkt;
    pkt.append(static_cast<char>(PREAMBLE1));
    pkt.append(static_cast<char>(PREAMBLE2));
    pkt.append(static_cast<char>(m_civAddr));
    pkt.append(static_cast<char>(HOST_ADDR));
    pkt.append(static_cast<char>(cmd));
    // Include sub-command if non-zero, data exists, or command always needs it
    if (subCmd != 0 || !data.isEmpty() || cmd == CMD_RIG_ID) {
        pkt.append(static_cast<char>(subCmd));
    }
    pkt.append(data);
    pkt.append(static_cast<char>(TERMINATOR));
    return pkt;
}

QByteArray IcomCivProtocol::buildReadFreq() const
{
    return buildCommand(CMD_FREQ, 0);
}

QByteArray IcomCivProtocol::buildReadMode() const
{
    return buildCommand(CMD_MODE, 0);
}

QByteArray IcomCivProtocol::buildSetFreq(uint64_t freqHz) const
{
    QByteArray data = encodeBcdFreq(freqHz);
    return buildCommand(CMD_FREQ, 0, data);
}

QByteArray IcomCivProtocol::buildSetMode(CivMode mode) const
{
    QByteArray data;
    data.append(static_cast<char>(mode));
    data.append(static_cast<char>(0x00));
    return buildCommand(CMD_MODE, SUB_MODE_SET, data);
}

QByteArray IcomCivProtocol::buildSetPtt(bool tx) const
{
    QByteArray data;
    data.append(tx ? static_cast<char>(SUB_PTT_TX) : static_cast<char>(SUB_PTT_RX));
    return buildCommand(CMD_PTT, tx ? SUB_PTT_TX : SUB_PTT_RX, data);
}

QByteArray IcomCivProtocol::buildSetVfoB(uint64_t freqHz) const
{
    QByteArray data = encodeBcdFreq(freqHz);
    return buildCommand(CMD_VFO_B_FREQ, 0, data);
}

QByteArray IcomCivProtocol::buildSetSplit(bool on) const
{
    QByteArray data;
    data.append(on ? '\x01' : '\x00');
    return buildCommand(CMD_SPLIT, 0, data);
}

QByteArray IcomCivProtocol::buildReadSMeter() const
{
    return buildCommand(CMD_S_METER, SUB_SMETER);
}

CivResponse IcomCivProtocol::parseResponse(const QByteArray& data) const
{
    CivResponse resp;
    if (data.size() < 6) return resp;
    if (static_cast<uint8_t>(data[0]) != PREAMBLE1) return resp;
    if (static_cast<uint8_t>(data[1]) != PREAMBLE2) return resp;

    resp.toAddr   = static_cast<uint8_t>(data[2]);
    resp.fromAddr = static_cast<uint8_t>(data[3]);
    resp.cmd      = static_cast<uint8_t>(data[4]);

    // CMD_READ_VFO (0x03) has NO sub-command in response — BCD data starts at pos 5
    bool noSubCmd = (resp.cmd == CMD_READ_VFO);

    int termPos = data.indexOf(static_cast<char>(TERMINATOR), 5);
    if (termPos == -1) return resp;

    if (noSubCmd) {
        resp.subCmd = 0;
        resp.data = data.mid(5, termPos - 5);
    } else if (termPos > 6) {
        resp.subCmd = static_cast<uint8_t>(data[5]);
        resp.data = data.mid(6, termPos - 6);
    } else if (termPos > 5) {
        resp.subCmd = static_cast<uint8_t>(data[5]);
    } else {
        resp.subCmd = 0;
    }

    resp.valid = true;
    return resp;
}

bool IcomCivProtocol::isCompleteFrame(const QByteArray& buffer)
{
    for (int i = 0; i < buffer.size(); ++i) {
        if (static_cast<uint8_t>(buffer[i]) == TERMINATOR) {
            return true;
        }
    }
    return false;
}

uint64_t IcomCivProtocol::decodeBcdFreq(const QByteArray& data)
{
    uint64_t freq = 0;
    for (int i = 0; i < data.size() && i < 5; ++i) {
        uint8_t b = static_cast<uint8_t>(data[i]);
        freq = freq * 10 + ((b >> 4) & 0x0F);
        freq = freq * 10 + (b & 0x0F);
    }
    return freq;
}

QByteArray IcomCivProtocol::encodeBcdFreq(uint64_t freqHz)
{
    QByteArray data(5, '\0');
    for (int i = 4; i >= 0; --i) {
        uint8_t low  = static_cast<uint8_t>(freqHz % 10);
        freqHz /= 10;
        uint8_t high = static_cast<uint8_t>(freqHz % 10);
        freqHz /= 10;
        data[i] = static_cast<char>((high << 4) | low);
    }
    return data;
}

QString IcomCivProtocol::modeToString(CivMode mode)
{
    switch (mode) {
    case CivMode::LSB:  return "LSB";
    case CivMode::USB:  return "USB";
    case CivMode::AM:   return "AM";
    case CivMode::CW:   return "CW";
    case CivMode::CWR:  return "CWR";
    case CivMode::FM:   return "FM";
    case CivMode::RTTY: return "DIGL";
    case CivMode::DIG:  return "DIGU";
    default:            return "USB";
    }
}

IcomCivProtocol::CivMode IcomCivProtocol::modeFromString(const QString& mode)
{
    if (mode == "LSB")  return CivMode::LSB;
    if (mode == "USB")  return CivMode::USB;
    if (mode == "AM")   return CivMode::AM;
    if (mode == "CW")   return CivMode::CW;
    if (mode == "CWR")  return CivMode::CWR;
    if (mode == "FM")   return CivMode::FM;
    if (mode == "DIGL") return CivMode::RTTY;
    if (mode == "DIGU") return CivMode::DIG;
    return CivMode::USB;
}

uint8_t IcomCivProtocol::modelToCivAddress(const QString& model)
{
    if (model.contains("705"))   return 0xA4;
    if (model.contains("7300"))  return 0x94;
    if (model.contains("9700"))  return 0xA2;
    if (model.contains("7610"))  return 0x98;
    if (model.contains("7100"))  return 0x88;
    if (model.contains("785"))   return 0x8E;
    if (model.contains("756Pro")) return 0x64;
    if (model.contains("746"))   return 0x66;
    if (model.contains("9100"))  return 0x7C;
    if (model.contains("7200"))  return 0x76;
    if (model.contains("706"))   return 0x58;
    return DEFAULT_CI_V_ADDR;  // 0xA4 default
}

QString IcomCivProtocol::rigIdToModel(uint8_t rigId)
{
    switch (rigId) {
    case 0x6C: return QStringLiteral("IC-76");
    case 0x6E: return QStringLiteral("IC-78");
    case 0x72: return QStringLiteral("IC-7800");
    case 0x74: return QStringLiteral("IC-7100");
    case 0x76: return QStringLiteral("IC-7850/7851");
    case 0x78: return QStringLiteral("IC-7300");
    case 0x7A: return QStringLiteral("IC-9700");
    case 0x7C: return QStringLiteral("IC-7610");
    case 0x7E: return QStringLiteral("IC-705");
    case 0x80: return QStringLiteral("IC-9100");
    case 0x88: return QStringLiteral("IC-7200");
    case 0x8A: return QStringLiteral("IC-7410/9100");
    case 0x8E: return QStringLiteral("IC-7600");
    case 0x92: return QStringLiteral("IC-756Pro3");
    case 0x94: return QStringLiteral("IC-756Pro2");
    case 0xA2: return QStringLiteral("IC-706MkIIG");
    case 0xA4: return QStringLiteral("IC-756/756Pro");
    default:   return QString("ICOM-0x%1").arg(rigId, 2, 16, QChar('0')).toUpper();
    }
}

QByteArray IcomCivProtocol::buildReadRigId() const
{
    return buildCommand(CMD_RIG_ID, 0x00);
}

QString IcomCivProtocol::smeterValueToText(uint8_t value)
{
    int sUnit = value / 12;
    if (sUnit > 9) sUnit = 9;
    QString text = QString("S%1").arg(sUnit);
    if (value > 120) {
        int dbOverS9 = ((value - 121) / 20) * 10;
        text = QString("S9+%1").arg(dbOverS9);
    }
    return text;
}

float IcomCivProtocol::smeterToDbm(uint8_t value)
{
    float sUnit = value / 12.0f;
    return -127.0f + sUnit * 6.0f;
}

QByteArray IcomCivProtocol::buildReadSquelch() const
{
    return buildCommand(CMD_S_METER, SUB_SQUELCH);
}

QByteArray IcomCivProtocol::buildReadOvfStatus() const
{
    return buildCommand(CMD_S_METER, SUB_OVF_STATUS);
}

QByteArray IcomCivProtocol::buildReadTxFreq() const
{
    return buildCommand(CMD_FREQ, 0x03);
}

QByteArray IcomCivProtocol::buildReadSplit() const
{
    return buildCommand(CMD_SPLIT, 0);
}

QByteArray IcomCivProtocol::buildReadBkIn() const
{
    return buildCommand(CMD_PREAMP, SUB_BKIN);
}

QByteArray IcomCivProtocol::buildReadApf() const
{
    return buildCommand(CMD_PREAMP, SUB_APF);
}

QByteArray IcomCivProtocol::buildReadRfPower() const
{
    return buildCommand(CMD_MIC_GAIN, SUB_TX_POWER);
}

QByteArray IcomCivProtocol::buildReadRfGain() const
{
    return buildCommand(CMD_MIC_GAIN, SUB_RF_GAIN);
}

QByteArray IcomCivProtocol::buildReadPreamp() const
{
    return buildCommand(CMD_PREAMP, SUB_PREAMP);
}

QByteArray IcomCivProtocol::buildReadAttenuator() const
{
    return buildCommand(CMD_ATTENUATOR, 0);
}

QByteArray IcomCivProtocol::buildReadFilter() const
{
    return buildCommand(CMD_FILTER, 0x03);
}

QByteArray IcomCivProtocol::buildReadRxAntenna() const
{
    return buildCommand(CMD_RX_ANTENNA, 0);
}

} // namespace MasterSDR
