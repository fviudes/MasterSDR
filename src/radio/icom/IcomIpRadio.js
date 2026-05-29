const { EventEmitter } = require('events');
const dgram = require('dgram');

const ICOM_CI_V_ADDRESSES = {
  IC_7300: 0x94,
  IC_7610: 0x98,
  IC_9700: 0xA2,
  IC_705: 0xA4,
  IC_7100: 0x88,
  IC_7851: 0x8E,
  DEFAULT: 0xA4,
};

const PKT_TYPE = {
  DATA: 0x00,
  NACK: 0x01,
  SYN: 0x03,
  SYN_ACK: 0x04,
  DISCON: 0x05,
  READY: 0x06,
  PING: 0x07,
};

const CIV = {
  PREAMBLE: 0xFE,
  TERMINATOR: 0xFD,
  HOST_ADDR: 0xE0,
  CMD_FREQ: 0x00,
  CMD_READ_VFO: 0x03,
  CMD_SET_FREQ: 0x05,
  CMD_MODE: 0x06,
  CMD_SPLIT: 0x0F,
  CMD_ATTENUATOR: 0x11,
  CMD_MIC_GAIN: 0x14,
  CMD_S_METER: 0x15,
  CMD_PREAMP: 0x16,
  CMD_RIG_ID: 0x19,
  CMD_PTT: 0x1C,
  CMD_SPECTRUM: 0x27,
  SUB_MODE_READ: 0x04,
  SUB_SQUELCH: 0x01,
  SUB_SMETER: 0x02,
  SUB_TX_POWER: 0x0A,
  SUB_RF_GAIN: 0x02,
  SUB_PREAMP: 0x02,
};

const KEEPALIVE_MS = 3000;
const CONNECT_TIMEOUT_MS = 8000;
const HEADER_SIZE = 16;

class IcomIpRadio extends EventEmitter {
  constructor(config) {
    super();
    this.config = config;
    this.ip = config.ip || '192.168.1.100';
    this.ctrlPort = config.ctrlPort || 50001;
    this.serialPort = config.serialPort || 50002;
    this.audioPort = config.audioPort || 50003;
    this.username = config.username || '';
    this.password = config.password || '';
    const modelKey = (config.model || '').replace(/-/g, '_').replace(/\s+/g, '_');
    this.ciVAddress = ICOM_CI_V_ADDRESSES[modelKey] || ICOM_CI_V_ADDRESSES.DEFAULT;

    this.ctrlSocket = null;
    this.audioSocket = null;
    this.keepAliveTimer = null;
    this.connected = false;

    this.sourcePort = 0;
    this.sourceId = 0;
    this.destPort = 0;
    this.destId = 0;
    this.seq = 0;
    this.pingSeq = 0;

    this.rxFreq = 14074000;
    this.rxMode = 'USB';
    this.ptt = false;
    this.sMeter = 0;
    this.txPower = 50;
    this.rfGain = 100;
    this.split = false;
    this.radioModel = '';
  }

  async connect() {
    return new Promise((resolve, reject) => {
      this.ctrlSocket = dgram.createSocket('udp4');
      this.audioSocket = dgram.createSocket('udp4');

      let boundCount = 0;
      const onBound = () => {
        boundCount++;
        if (boundCount >= 2) {
          this.sourcePort = this.ctrlSocket.address().port;
          this.sourceId = 0;
          this.sendCtrlPacket(PKT_TYPE.SYN, 0);
        }
      };

      const timeout = setTimeout(() => {
        this.cleanup();
        reject(new Error('Timeout: rádio Icom IP não respondeu'));
      }, CONNECT_TIMEOUT_MS);

      this.ctrlSocket.on('message', (msg, rinfo) => {
        if (rinfo.port === this.ctrlPort || rinfo.port === this.serialPort) {
          this.processCtrlMessage(msg, rinfo.port);
        }

        if (!this.connected) return;
        clearTimeout(timeout);
      });

      this.audioSocket.on('message', (msg) => {
        this.processAudioMessage(msg);
      });

      this.ctrlSocket.on('error', (err) => {
        clearTimeout(timeout);
        this.emit('error', err);
        reject(err);
      });

      this.audioSocket.on('error', (err) => {
        this.emit('error', err);
      });

      this.ctrlSocket.bind(0, () => onBound());
      this.audioSocket.bind(0, () => onBound());

      this.once('_connected', () => {
        clearTimeout(timeout);
        resolve();
      });
    });
  }

  buildPacket(typeCode, seq, dstPort, dstId, payload = null) {
    const payloadSize = payload ? payload.length : 0;
    const totalLen = HEADER_SIZE + payloadSize;
    const buf = Buffer.alloc(totalLen);

    buf.writeUInt32LE(totalLen, 0);
    buf.writeUInt16LE(typeCode, 4);
    buf.writeUInt16LE(seq, 6);
    buf.writeUInt16LE(this.sourcePort, 8);
    buf.writeUInt16LE(this.sourceId, 10);
    buf.writeUInt16LE(dstPort, 12);
    buf.writeUInt16LE(dstId, 14);

    if (payload) {
      payload.copy(buf, HEADER_SIZE);
    }

    return buf;
  }

  sendCtrlPacket(typeCode, seq, payload = null) {
    const pkt = this.buildPacket(typeCode, seq, this.destPort, this.destId, payload);
    this.ctrlSocket.send(pkt, 0, pkt.length, this.ctrlPort, this.ip);
  }

  sendSerialPacket(seq, civFrame) {
    const pkt = this.buildPacket(PKT_TYPE.DATA, seq, this.ctrlPort, this.destId, civFrame);
    this.ctrlSocket.send(pkt, 0, pkt.length, this.ctrlPort, this.ip);
  }

  processCtrlMessage(msg, senderPort) {
    if (msg.length >= 6 && msg[0] === CIV.PREAMBLE && msg[1] === CIV.PREAMBLE) {
      this.parseCivResponse(msg);
      return;
    }

    if (msg.length < HEADER_SIZE) return;

    const type = msg.readUInt16LE(4);
    const seq = msg.readUInt16LE(6);
    const srcPort = msg.readUInt16LE(8);
    const srcId = msg.readUInt16LE(10);

    if (senderPort === this.ctrlPort) {
      switch (type) {
        case PKT_TYPE.SYN_ACK:
          this.destPort = srcPort;
          this.destId = srcId;
          this.sendCtrlPacket(PKT_TYPE.READY, this.seq++);
          break;

        case PKT_TYPE.READY:
          if (!this.connected) {
            this.connected = true;
            this.startKeepAlive();
            this.registerChannels();
            this.pollInitialState();
            this.emit('state', { connected: true });
            this.emit('_connected');
          }
          break;

        case PKT_TYPE.PING:
          this.sendCtrlPacket(PKT_TYPE.PING, this.pingSeq++);
          break;

        case PKT_TYPE.DISCON:
          this.handleDisconnect();
          break;
      }
    }

    if (type === PKT_TYPE.DATA && msg.length > HEADER_SIZE) {
      const payload = msg.slice(HEADER_SIZE);
      this.extractCivFromPayload(payload);
    }
  }

  registerChannels() {
    const serialPkt = this.buildPacket(PKT_TYPE.DATA, this.seq++, this.serialPort, this.destId);
    this.ctrlSocket.send(serialPkt, 0, serialPkt.length, this.serialPort, this.ip);

    const audioPkt = this.buildPacket(PKT_TYPE.DATA, this.seq++, this.audioPort, this.destId);
    this.audioSocket.send(audioPkt, 0, audioPkt.length, this.audioPort, this.ip);
  }

  processAudioMessage(msg) {
    if (msg.length <= HEADER_SIZE) return;

    const pcm = msg.slice(HEADER_SIZE);

    if (pcm.length >= 400) {
      this.emit('spectrum-data', pcm);
    } else {
      this.emit('audio-data', pcm);
    }
  }

  startKeepAlive() {
    this.keepAliveTimer = setInterval(() => {
      if (!this.connected) return;

      this.sendCtrlPacket(PKT_TYPE.PING, this.pingSeq++);

      const serialIdle = this.buildPacket(PKT_TYPE.DATA, this.pingSeq++, this.serialPort, this.destId);
      this.ctrlSocket.send(serialIdle, 0, serialIdle.length, this.serialPort, this.ip);

      const audioIdle = this.buildPacket(PKT_TYPE.DATA, this.pingSeq++, this.audioPort, this.destId);
      this.audioSocket.send(audioIdle, 0, audioIdle.length, this.audioPort, this.ip);

      this.sendCivCommand(CIV.CMD_READ_VFO);
      this.sendCivCommand(CIV.CMD_MODE, CIV.SUB_MODE_READ);
      this.sendCivCommand(CIV.CMD_S_METER, CIV.SUB_SMETER);
      this.sendCivCommand(CIV.CMD_S_METER, CIV.SUB_SQUELCH);
      this.sendCivCommand(CIV.CMD_MIC_GAIN, CIV.SUB_TX_POWER);
      this.sendCivCommand(CIV.CMD_MIC_GAIN, CIV.SUB_RF_GAIN);
      this.sendCivCommand(CIV.CMD_SPLIT);
    }, KEEPALIVE_MS);
  }

  pollInitialState() {
    this.sendCivCommand(CIV.CMD_READ_VFO);
    this.sendCivCommand(CIV.CMD_MODE, CIV.SUB_MODE_READ);
    this.sendCivCommand(CIV.CMD_S_METER, CIV.SUB_SMETER);
    this.sendCivCommand(CIV.CMD_RIG_ID, 0x00);
    this.sendCivCommand(CIV.CMD_SPLIT);
  }

  buildCivFrame(cmd, subCmd = null, data = null) {
    const parts = [CIV.PREAMBLE, CIV.PREAMBLE, this.ciVAddress, CIV.HOST_ADDR, cmd];

    if (subCmd !== null && subCmd !== undefined) {
      parts.push(subCmd);
    }

    if (data) {
      for (let i = 0; i < data.length; i++) {
        parts.push(data[i]);
      }
    }

    parts.push(CIV.TERMINATOR);
    return Buffer.from(parts);
  }

  sendCivCommand(cmd, subCmd = null, data = null) {
    const frame = this.buildCivFrame(cmd, subCmd, data);
    this.sendSerialPacket(this.seq++, frame);
  }

  extractCivFromPayload(payload) {
    if (payload.length >= 5 && payload[0] === CIV.PREAMBLE && payload[1] === CIV.PREAMBLE) {
      this.parseCivResponse(payload);
      return;
    }

    if (payload.length >= 4 &&
        (payload[0] === CIV.HOST_ADDR || payload[0] === this.ciVAddress)) {
      const full = Buffer.alloc(payload.length + 2);
      full[0] = CIV.PREAMBLE;
      full[1] = CIV.PREAMBLE;
      payload.copy(full, 2);
      this.parseCivResponse(full);
    }
  }

  parseCivResponse(data) {
    if (data.length < 6) return;
    if (data[0] !== CIV.PREAMBLE || data[1] !== CIV.PREAMBLE) return;

    const toAddr = data[2];
    const fromAddr = data[3];
    const cmd = data[4];

    let termPos = -1;
    for (let i = 5; i < data.length; i++) {
      if (data[i] === CIV.TERMINATOR) { termPos = i; break; }
    }
    if (termPos === -1) return;

    const noSubCmd = (cmd === CIV.CMD_READ_VFO);
    let subCmd = 0;
    let responseData;

    if (noSubCmd) {
      responseData = data.slice(5, termPos);
    } else if (termPos > 6) {
      subCmd = data[5];
      responseData = data.slice(6, termPos);
    } else if (termPos > 5) {
      subCmd = data[5];
      responseData = Buffer.alloc(0);
    } else {
      responseData = Buffer.alloc(0);
    }

    this.handleCivResponse(cmd, subCmd, responseData);
  }

  handleCivResponse(cmd, subCmd, data) {
    switch (cmd) {
      case CIV.CMD_FREQ:
      case CIV.CMD_READ_VFO: {
        if (data.length >= 5) {
          const freq = this.decodeBcdFreq(data);
          if (freq > 0) {
            this.rxFreq = freq;
            this.emit('state', { frequency: freq });
          }
        }
        break;
      }

      case CIV.CMD_MODE: {
        if (data.length >= 1) {
          const mode = this.modeByteToString(data[0]);
          if (mode) {
            this.rxMode = mode;
            this.emit('state', { mode });
          }
        }
        break;
      }

      case CIV.CMD_S_METER: {
        if (data.length >= 1) {
          const level = data[0];
          if (subCmd === CIV.SUB_SMETER) {
            this.sMeter = level;
            this.emit('state', { sMeter: level, sMeterText: this.smeterToText(level) });
          } else if (subCmd === CIV.SUB_SQUELCH) {
            this.emit('state', { squelchOpen: level !== 0x00 });
          }
        }
        break;
      }

      case CIV.CMD_RIG_ID: {
        if (data.length >= 1) {
          const model = this.rigIdToModel(data[0]);
          this.radioModel = model;
          this.emit('state', { radioModel: model });
        }
        break;
      }

      case CIV.CMD_SPLIT: {
        if (data.length >= 1) {
          this.split = data[0] !== 0x00;
          this.emit('state', { split: this.split });
        }
        break;
      }

      case CIV.CMD_MIC_GAIN: {
        if (data.length >= 1) {
          if (subCmd === CIV.SUB_TX_POWER) {
            this.txPower = Math.round(data[0] * 100 / 255);
            this.emit('state', { power: this.txPower });
          } else if (subCmd === CIV.SUB_RF_GAIN) {
            this.rfGain = Math.round(data[0] * 100 / 255);
            this.emit('state', { rfGain: this.rfGain });
          }
        }
        break;
      }

      case CIV.CMD_PTT: {
        if (data.length >= 1) {
          this.ptt = data[0] !== 0x00;
          this.emit('state', { ptt: this.ptt });
        }
        break;
      }
    }
  }

  decodeBcdFreq(data) {
    let freq = 0;
    for (let i = 0; i < Math.min(data.length, 5); i++) {
      const b = data[i];
      freq = freq * 10 + ((b >> 4) & 0x0F);
      freq = freq * 10 + (b & 0x0F);
    }
    return freq;
  }

  encodeBcdFreq(freqHz) {
    const buf = Buffer.alloc(5);
    let f = freqHz;
    for (let i = 4; i >= 0; i--) {
      const low = f % 10;
      f = Math.floor(f / 10);
      const high = f % 10;
      f = Math.floor(f / 10);
      buf[i] = (high << 4) | low;
    }
    return buf;
  }

  modeByteToString(modeByte) {
    const map = {
      0x00: 'LSB', 0x01: 'USB', 0x02: 'AM', 0x03: 'CW',
      0x05: 'FM', 0x07: 'CWR', 0x08: 'DIGL', 0x17: 'DIGU',
    };
    return map[modeByte] || 'USB';
  }

  modeStringToByte(mode) {
    const map = {
      LSB: 0x00, USB: 0x01, AM: 0x02, CW: 0x03,
      FM: 0x05, CWR: 0x07, DIGL: 0x08, DIGU: 0x17,
      RTTY: 0x08, FT8: 0x01, FT4: 0x01,
    };
    return map[mode] !== undefined ? map[mode] : 0x01;
  }

  smeterToText(value) {
    const sUnit = Math.min(Math.floor(value / 12), 9);
    if (value > 120) {
      const dbOver = Math.floor((value - 121) / 20) * 10;
      return `S9+${dbOver}`;
    }
    return `S${sUnit}`;
  }

  rigIdToModel(rigId) {
    const map = {
      0x78: 'IC-7300', 0x7A: 'IC-9700', 0x7C: 'IC-7610',
      0x7E: 'IC-705', 0x74: 'IC-7100', 0x76: 'IC-7850/7851',
      0x80: 'IC-9100',
    };
    return map[rigId] || `ICOM-0x${rigId.toString(16).toUpperCase().padStart(2, '0')}`;
  }

  async setFrequency(freqHz) {
    this.rxFreq = freqHz;
    const bcd = this.encodeBcdFreq(freqHz);
    this.sendCivCommand(CIV.CMD_SET_FREQ, null, bcd);
    this.emit('state', { frequency: freqHz });
  }

  async setMode(mode) {
    this.rxMode = mode;
    const modeByte = this.modeStringToByte(mode);
    this.sendCivCommand(CIV.CMD_MODE, null, Buffer.from([modeByte, 0x00]));
    this.emit('state', { mode });
  }

  async setPTT(enable) {
    this.ptt = enable;
    this.sendCivCommand(CIV.CMD_PTT, 0x00, Buffer.from([enable ? 0x01 : 0x00]));
    this.emit('state', { ptt: enable });
  }

  async setSplit(enable) {
    this.split = enable;
    this.sendCivCommand(CIV.CMD_SPLIT, null, Buffer.from([enable ? 0x01 : 0x00]));
    this.emit('state', { split: enable });
  }

  getSupportedModes() {
    return ['LSB', 'USB', 'CW', 'CWR', 'AM', 'FM', 'DIGL', 'DIGU', 'FT8', 'FT4', 'RTTY'];
  }

  handleDisconnect() {
    this.connected = false;
    this.cleanup();
    this.emit('state', { connected: false });
    this.emit('disconnect');
  }

  cleanup() {
    if (this.keepAliveTimer) {
      clearInterval(this.keepAliveTimer);
      this.keepAliveTimer = null;
    }
    if (this.ctrlSocket) {
      try { this.ctrlSocket.close(); } catch (e) {}
      this.ctrlSocket = null;
    }
    if (this.audioSocket) {
      try { this.audioSocket.close(); } catch (e) {}
      this.audioSocket = null;
    }
  }

  async disconnect() {
    if (this.connected && this.ctrlSocket) {
      this.sendCtrlPacket(PKT_TYPE.DISCON, this.seq++);
    }
    this.connected = false;
    this.cleanup();
    this.emit('disconnect');
  }
}

module.exports = { IcomIpRadio, ICOM_CI_V_ADDRESSES };
