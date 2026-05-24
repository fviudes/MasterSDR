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
    if (subCmd != 0 || !data.isEmpty()) {
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
    return buildCommand(CMD_MODE, 0, data);
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
    return buildCommand(CMD_S_METER, 0);
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
    resp.subCmd   = (data.size() > 6) ? static_cast<uint8_t>(data[5]) : 0;

    int dataStart = 5;
    if (resp.cmd == CMD_MODE || resp.cmd == CMD_PTT) {
        dataStart = 6;
    }

    int termPos = data.indexOf(static_cast<char>(TERMINATOR), dataStart);
    if (termPos > dataStart) {
        resp.data = data.mid(dataStart, termPos - dataStart);
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

} // namespace MasterSDR
