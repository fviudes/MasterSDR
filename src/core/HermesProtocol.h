#pragma once

#include <QByteArray>
#include <QString>
#include <QtEndian>
#include <cstdint>
#include <array>

namespace MasterSDR {

struct HermesDiscoveryReply {
    uint8_t     type{0};
    QString     mac;
    QString     gateware;
    uint8_t     boardId{0};
    bool        useEepromIp{false};
    bool        useEepromMac{false};
    bool        favorDhcp{false};
    QString     eepromIp;
    QString     eepromMac;
    uint8_t     numReceivers{1};
    uint8_t     widebandType{0};
    uint32_t    responseData{0};
    bool        extCwKey{false};
    bool        ptt{false};
    bool        paExtTr{false};
    bool        paIntTr{false};
    bool        txOn{false};
    bool        cwOn{false};
    uint8_t     adcClipCount{0};
    float       temperature{0.0f};
    uint16_t    forwardPower{0};
    uint16_t    reversePower{0};
    float       biasCurrent{0.0f};
    bool        txfifoRecovery{false};
    uint8_t     txfifoMsbs{0};
    QString     ipAddress;
    uint16_t    udpPort{1025};
};

struct HermesCommand {
    uint8_t control{0};
    uint32_t data{0};
};

class HermesProtocol {
public:
    static constexpr uint16_t DISCOVERY_PORT = 1025;
    static constexpr uint16_t DISCOVERY_PORT_ALT = 1024;
    static constexpr uint16_t COMMAND_PORT = 1025;
    static constexpr uint8_t  BOARD_ID_HL2 = 0x06;
    static constexpr uint8_t  BOARD_ID_HERMES = 0x01;

    static constexpr size_t DISCOVERY_MSG_SIZE = 60;
    static constexpr size_t COMMAND_FRAME_SIZE = 64;
    static constexpr size_t IQ_BUFFER_SIZE = 1024;
    static constexpr size_t WB_SAMPLES_PER_PACKET = 512;
    static constexpr size_t ADC_SAMPLE_RATE_HZ = 76800000;

    // Packet types
    static constexpr uint8_t PKT_DISCOVERY  = 0x02;
    static constexpr uint8_t PKT_START      = 0x04;
    static constexpr uint8_t PKT_COMMAND    = 0x05;
    static constexpr uint8_t PKT_WIDEBAND   = 0x04;

    // Discovery cookie
    static constexpr std::array<uint8_t, 2> DISCOVERY_COOKIE = {0xEF, 0xFE};

    // Start/stop flags
    static constexpr uint8_t START_RADIO    = 0x01;
    static constexpr uint8_t START_WIDEBAND = 0x02;
    static constexpr uint8_t START_DISABLE_WATCHDOG = 0x80;

    // Memory map addresses (Hermes Lite 2)
    static constexpr uint8_t ADDR_CONTROL      = 0x00;
    static constexpr uint8_t ADDR_TX_NCO       = 0x01;
    static constexpr uint8_t ADDR_RX1_NCO      = 0x02;
    static constexpr uint8_t ADDR_RX2_NCO      = 0x03;
    static constexpr uint8_t ADDR_RX3_NCO      = 0x04;
    static constexpr uint8_t ADDR_RX4_NCO      = 0x05;
    static constexpr uint8_t ADDR_RX5_NCO      = 0x06;
    static constexpr uint8_t ADDR_RX6_NCO      = 0x07;
    static constexpr uint8_t ADDR_RX7_NCO      = 0x08;
    static constexpr uint8_t ADDR_TX_DRIVE     = 0x09;
    static constexpr uint8_t ADDR_LNA_GAIN     = 0x0A;
    static constexpr uint8_t ADDR_TX_LNA       = 0x0E;
    static constexpr uint8_t ADDR_CWX          = 0x0F;
    static constexpr uint8_t ADDR_CW_HANG      = 0x10;
    static constexpr uint8_t ADDR_RX8_NCO      = 0x12;
    static constexpr uint8_t ADDR_PTT_BUFFER   = 0x17;
    static constexpr uint8_t ADDR_PREDISTORTION = 0x2B;
    static constexpr uint8_t ADDR_MISC         = 0x39;
    static constexpr uint8_t ADDR_REBOOT       = 0x3A;
    static constexpr uint8_t ADDR_AD9866_SPI   = 0x3B;
    static constexpr uint8_t ADDR_I2C1         = 0x3C;
    static constexpr uint8_t ADDR_I2C2         = 0x3D;
    static constexpr uint8_t ADDR_ERROR        = 0x3F;

    // Speed bits in ADDR_CONTROL
    static constexpr uint8_t SPEED_48K  = 0x00;
    static constexpr uint8_t SPEED_96K  = 0x01;
    static constexpr uint8_t SPEED_192K = 0x02;
    static constexpr uint8_t SPEED_384K = 0x03;

    // Discovery packet creation
    static QByteArray buildDiscoveryPacket();
    static QByteArray buildStartPacket(uint8_t flags);
    static QByteArray buildStopPacket(uint8_t flags);
    static QByteArray buildCommandPacket(uint8_t addr, uint32_t data);
    static QByteArray buildCommandPacket(uint8_t addr, const QByteArray& data);

    // Packet decoding
    static bool isValidReply(const QByteArray& data);
    static HermesDiscoveryReply decodeDiscoveryReply(const QByteArray& data, const QString& ipAddress, uint16_t port);
    static bool isWidebandPacket(const QByteArray& data);
    static uint32_t extractWidebandSequence(const QByteArray& data);

    // Frequency helpers (NCO frequency = freq_hz)
    static uint32_t hzToNco(double freqHz);
    static double ncoToHz(uint32_t nco);

    // Control word helpers
    static uint32_t buildControlWord(uint8_t speed, uint8_t numReceivers, bool duplex, bool mox);
    static uint32_t buildLnaGain(uint8_t gain, bool stepAttenuatorOn);
};

inline QByteArray HermesProtocol::buildDiscoveryPacket()
{
    QByteArray pkt(DISCOVERY_MSG_SIZE, '\0');
    pkt[0] = '\xEF';
    pkt[1] = '\xFE';
    pkt[2] = '\x02';
    return pkt;
}

inline QByteArray HermesProtocol::buildStartPacket(uint8_t flags)
{
    QByteArray pkt(COMMAND_FRAME_SIZE, '\0');
    pkt[0] = '\xEF';
    pkt[1] = '\xFE';
    pkt[2] = static_cast<char>(PKT_START);
    pkt[3] = static_cast<char>(flags);
    return pkt;
}

inline QByteArray HermesProtocol::buildStopPacket(uint8_t flags)
{
    return buildStartPacket(static_cast<uint8_t>(flags & ~(START_RADIO | START_WIDEBAND)));
}

inline QByteArray HermesProtocol::buildCommandPacket(uint8_t addr, uint32_t data)
{
    QByteArray pkt(COMMAND_FRAME_SIZE, '\0');
    pkt[0] = '\xEF';
    pkt[1] = '\xFE';
    pkt[2] = static_cast<char>(PKT_COMMAND);
    pkt[3] = '\x7F';
    pkt[4] = static_cast<char>(addr << 1);
    qToBigEndian(data, reinterpret_cast<uchar*>(pkt.data()) + 5);
    return pkt;
}

inline QByteArray HermesProtocol::buildCommandPacket(uint8_t addr, const QByteArray& data)
{
    QByteArray pkt(COMMAND_FRAME_SIZE, '\0');
    pkt[0] = '\xEF';
    pkt[1] = '\xFE';
    pkt[2] = static_cast<char>(PKT_COMMAND);
    pkt[3] = '\x7F';
    pkt[4] = static_cast<char>(addr << 1);
    for (int i = 0; i < std::min(static_cast<int>(data.size()), 4); ++i) {
        pkt[5 + i] = data[i];
    }
    return pkt;
}

inline bool HermesProtocol::isValidReply(const QByteArray& data)
{
    return data.size() >= 60
        && static_cast<uint8_t>(data[0]) == 0xEF
        && static_cast<uint8_t>(data[1]) == 0xFE;
}

inline HermesDiscoveryReply HermesProtocol::decodeDiscoveryReply(const QByteArray& data,
                                                                 const QString& ip, uint16_t port)
{
    HermesDiscoveryReply r;
    if (!isValidReply(data)) return r;

    r.ipAddress = ip;
    r.udpPort = port;
    r.type = static_cast<uint8_t>(data[2]);

    r.mac = QString("%1:%2:%3:%4:%5:%6")
        .arg(static_cast<uint8_t>(data[3]), 2, 16, QChar('0'))
        .arg(static_cast<uint8_t>(data[4]), 2, 16, QChar('0'))
        .arg(static_cast<uint8_t>(data[5]), 2, 16, QChar('0'))
        .arg(static_cast<uint8_t>(data[6]), 2, 16, QChar('0'))
        .arg(static_cast<uint8_t>(data[7]), 2, 16, QChar('0'))
        .arg(static_cast<uint8_t>(data[8]), 2, 16, QChar('0'));

    r.gateware = QString("%1.%2")
        .arg(static_cast<uint8_t>(data[9]))
        .arg(static_cast<uint8_t>(data[0x15]));

    r.boardId = static_cast<uint8_t>(data[0x0A]);
    r.numReceivers = static_cast<uint8_t>(data[0x13]);

    uint8_t config = static_cast<uint8_t>(data[0x0B]);
    r.useEepromIp  = (config & 0x80) != 0;
    r.useEepromMac = (config & 0x40) != 0;
    r.favorDhcp    = (config & 0x20) != 0;

    r.eepromIp = QString("%1.%2.%3.%4")
        .arg(static_cast<uint8_t>(data[0x0D]))
        .arg(static_cast<uint8_t>(data[0x0E]))
        .arg(static_cast<uint8_t>(data[0x0F]))
        .arg(static_cast<uint8_t>(data[0x10]));

    r.eepromMac = QString("%1:%2:%3:%4:%5:%6")
        .arg(static_cast<uint8_t>(data[3]), 2, 16, QChar('0'))
        .arg(static_cast<uint8_t>(data[4]), 2, 16, QChar('0'))
        .arg(static_cast<uint8_t>(data[5]), 2, 16, QChar('0'))
        .arg(static_cast<uint8_t>(data[6]), 2, 16, QChar('0'))
        .arg(static_cast<uint8_t>(data[0x11]), 2, 16, QChar('0'))
        .arg(static_cast<uint8_t>(data[0x12]), 2, 16, QChar('0'));

    uint8_t wbInfo = static_cast<uint8_t>(data[0x14]);
    r.widebandType = (wbInfo >> 6) & 0x03;

    r.responseData = qFromBigEndian<quint32>(
        reinterpret_cast<const uchar*>(data.constData()) + 0x17);

    uint8_t flags = static_cast<uint8_t>(data[0x1B]);
    r.extCwKey  = (flags & 0x80) != 0;
    r.ptt       = (flags & 0x40) != 0;
    r.paExtTr   = (flags & 0x20) != 0;
    r.paIntTr   = (flags & 0x10) != 0;
    r.txOn      = (flags & 0x08) != 0;
    r.cwOn      = (flags & 0x04) != 0;
    r.adcClipCount = flags & 0x03;

    uint16_t tempRaw = qFromBigEndian<quint16>(
        reinterpret_cast<const uchar*>(data.constData()) + 0x1C);
    r.temperature = (3.26f * (tempRaw / 4096.0f) - 0.5f) / 0.01f;

    r.forwardPower = qFromBigEndian<quint16>(
        reinterpret_cast<const uchar*>(data.constData()) + 0x1E);
    r.reversePower = qFromBigEndian<quint16>(
        reinterpret_cast<const uchar*>(data.constData()) + 0x20);

    uint16_t biasRaw = qFromBigEndian<quint16>(
        reinterpret_cast<const uchar*>(data.constData()) + 0x22);
    r.biasCurrent = ((3.26f * (biasRaw / 4096.0f)) / 50.0f) / 0.04f;

    uint8_t fifo = static_cast<uint8_t>(data[0x24]);
    r.txfifoRecovery = (fifo & 0x80) != 0;
    r.txfifoMsbs = fifo & 0x7F;

    return r;
}

inline bool HermesProtocol::isWidebandPacket(const QByteArray& data)
{
    return data.size() == static_cast<int>(IQ_BUFFER_SIZE)
        && static_cast<uint8_t>(data[3]) == PKT_WIDEBAND;
}

inline uint32_t HermesProtocol::extractWidebandSequence(const QByteArray& data)
{
    if (data.size() < 8) return 0;
    return qFromBigEndian<quint32>(
        reinterpret_cast<const uchar*>(data.constData()) + 4);
}

inline uint32_t HermesProtocol::hzToNco(double freqHz)
{
    return static_cast<uint32_t>(freqHz + 0.5);
}

inline double HermesProtocol::ncoToHz(uint32_t nco)
{
    return static_cast<double>(nco);
}

inline uint32_t HermesProtocol::buildControlWord(uint8_t speed, uint8_t numReceivers, bool duplex, bool mox)
{
    uint32_t ctrl = 0;
    ctrl |= (static_cast<uint32_t>(speed) & 0x03) << 24;
    ctrl |= (static_cast<uint32_t>(numReceivers & 0x0F)) << 3;
    if (duplex) ctrl |= (1u << 2);
    if (mox)    ctrl |= 1;
    return ctrl;
}

inline uint32_t HermesProtocol::buildLnaGain(uint8_t gain, bool stepAttenuatorOn)
{
    uint32_t val = gain & 0x3F;
    if (stepAttenuatorOn) val |= (1u << 5);
    return val;
}

} // namespace MasterSDR
