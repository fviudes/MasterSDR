#pragma once

#include <QByteArray>
#include <QString>
#include <QMap>
#include <cstdint>

namespace MasterSDR {

struct CivResponse {
    uint8_t toAddr{0};
    uint8_t fromAddr{0};
    uint8_t cmd{0};
    uint8_t subCmd{0};
    QByteArray data;
    bool valid{false};
};

class IcomCivProtocol {
public:
    static constexpr uint8_t DEFAULT_CI_V_ADDR = 0x70;
    static constexpr uint8_t HOST_ADDR = 0xE0;

    static constexpr uint8_t PREAMBLE1 = 0xFE;
    static constexpr uint8_t PREAMBLE2 = 0xFE;
    static constexpr uint8_t TERMINATOR = 0xFD;

    // CI-V commands
    static constexpr uint8_t CMD_FREQ        = 0x00;
    static constexpr uint8_t CMD_MODE        = 0x04;
    static constexpr uint8_t CMD_VFO         = 0x07;
    static constexpr uint8_t CMD_SPLIT       = 0x0F;
    static constexpr uint8_t CMD_READ_SPLIT  = 0x15;
    static constexpr uint8_t CMD_PTT         = 0x1C;
    static constexpr uint8_t CMD_OP_COND     = 0x1A;
    static constexpr uint8_t CMD_S_METER     = 0x1B;
    static constexpr uint8_t CMD_VFO_B_FREQ  = 0x25;
    static constexpr uint8_t CMD_SCOPE       = 0x27;

    // Sub commands for CMD_PTT (0x1C)
    static constexpr uint8_t SUB_PTT_TX  = 0x00;
    static constexpr uint8_t SUB_PTT_RX  = 0x01;

    // Sub commands for CMD_MODE (0x04 read, 0x05 write pending)
    static constexpr uint8_t SUB_MODE_READ  = 0x00;
    static constexpr uint8_t SUB_MODE_WRITE = 0x01;

    // CI-V mode codes (subset)
    enum class CivMode : uint8_t {
        LSB = 0x00, USB = 0x01, AM = 0x02, CW = 0x03,
        FM = 0x05, CWR = 0x07, RTTY = 0x08, DIG = 0x17
    };

    IcomCivProtocol(uint8_t civAddr = DEFAULT_CI_V_ADDR);

    void setCivAddress(uint8_t addr) { m_civAddr = addr; }
    uint8_t civAddress() const { return m_civAddr; }

    QByteArray buildCommand(uint8_t cmd, uint8_t subCmd, const QByteArray& data = QByteArray()) const;
    QByteArray buildReadFreq() const;
    QByteArray buildReadMode() const;
    QByteArray buildSetFreq(uint64_t freqHz) const;
    QByteArray buildSetMode(CivMode mode) const;
    QByteArray buildSetPtt(bool tx) const;
    QByteArray buildSetVfoB(uint64_t freqHz) const;
    QByteArray buildSetSplit(bool on) const;
    QByteArray buildReadSMeter() const;

    CivResponse parseResponse(const QByteArray& data) const;
    static bool isCompleteFrame(const QByteArray& buffer);
    static uint64_t decodeBcdFreq(const QByteArray& data);
    static QByteArray encodeBcdFreq(uint64_t freqHz);

    static QString modeToString(CivMode mode);
    static CivMode modeFromString(const QString& mode);

private:
    uint8_t m_civAddr{DEFAULT_CI_V_ADDR};
};

} // namespace MasterSDR
