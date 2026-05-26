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

    // CI-V commands (from wfview/IC-9700 CI-V reference)
    static constexpr uint8_t CMD_FREQ        = 0x00;
    static constexpr uint8_t CMD_MODE        = 0x06;
    static constexpr uint8_t CMD_ATTENUATOR  = 0x11;
    static constexpr uint8_t CMD_MIC_GAIN    = 0x14;
    static constexpr uint8_t CMD_S_METER     = 0x15;
    static constexpr uint8_t CMD_PREAMP      = 0x16;
    static constexpr uint8_t CMD_RIG_ID      = 0x19;
    static constexpr uint8_t CMD_PTT         = 0x1C;
    static constexpr uint8_t CMD_SPECTRUM    = 0x27;
    static constexpr uint8_t CMD_BROADCAST   = 0x00;  // Address 0x00 = all radios respond

    // Sub-commands for CMD_MODE (0x06)
    static constexpr uint8_t SUB_MODE_SET    = 0x01;

    // Sub-commands for CMD_S_METER (0x15)
    static constexpr uint8_t SUB_SMETER      = 0x02;
    static constexpr uint8_t SUB_POWER_METER = 0x11;

    // Sub-commands for CMD_PREAMP (0x16)
    static constexpr uint8_t SUB_PREAMP      = 0x02;

    // Sub-commands for CMD_MIC_GAIN (0x14)
    static constexpr uint8_t SUB_MIC_GAIN    = 0x0B;
    static constexpr uint8_t SUB_RF_GAIN     = 0x02;
    static constexpr uint8_t SUB_TX_POWER    = 0x0A;
    static constexpr uint8_t SUB_SQUELCH     = 0x03;

    // Sub-commands for CMD_SPECTRUM (0x27)
    static constexpr uint8_t SUB_SCOPE_DATA  = 0x00;
    static constexpr uint8_t SUB_SCOPE_ENABLE = 0x11;
    static constexpr uint8_t SUB_SCOPE_MODE  = 0x15;
    static constexpr uint8_t SUB_SCOPE_SPAN  = 0x15;
    static constexpr uint8_t SUB_SCOPE_EDGE  = 0x16;
    static constexpr uint8_t SUB_SCOPE_REF   = 0x19;

    // Sub-commands for CMD_PTT (0x1C)
    static constexpr uint8_t SUB_PTT_TX      = 0x00;
    static constexpr uint8_t SUB_PTT_RX      = 0x01;

    // CI-V addresses (defaults from wfview)
    static constexpr uint8_t HOST_ADDR       = 0xE0;  // Controller address
    static constexpr uint8_t BROADCAST_ADDR  = 0x00;  // All radios respond

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
