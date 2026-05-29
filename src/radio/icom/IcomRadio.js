const { EventEmitter } = require('events');
const { SerialPort } = require('serialport');

const ICOM_CI_V_ADDRESSES = {
  IC_7300: 0x94,
  IC_7610: 0x98,
  IC_9700: 0xA2,
  IC_705: 0xA4,
  IC_7100: 0x88,
  IC_7851: 0x8E,
  DEFAULT: 0x94,
};

class IcomRadio extends EventEmitter {
  constructor(config) {
    super();
    this.config = config;
    this.portPath = config.port;
    this.baudRate = config.baudRate || 19200;
    this.ciVAddress = ICOM_CI_V_ADDRESSES[config.model] || ICOM_CI_V_ADDRESSES.DEFAULT;
    this.controllerAddress = 0xE0;
    this.port = null;
    this.connected = false;
    this.pollTimer = null;
  }

  async connect() {
    return new Promise((resolve, reject) => {
      this.port = new SerialPort({
        path: this.portPath,
        baudRate: this.baudRate,
        dataBits: 8,
        stopBits: 1,
        parity: 'none',
        autoOpen: false,
      });

      this.port.open((err) => {
        if (err) {
          reject(new Error(`Erro ao abrir porta CI-V: ${err.message}`));
          return;
        }

        this.connected = true;

        this.port.on('data', (data) => {
          this.processCI_V(data);
        });

        this.startPolling();
        this.emit('state', { connected: true });
        resolve();
      });
    });
  }

  buildCiVFrame(command, subCommand = null, data = []) {
    const frame = [];
    frame.push(0xFE); // Preamble 1
    frame.push(0xFE); // Preamble 2
    frame.push(this.ciVAddress); // Destination address
    frame.push(this.controllerAddress); // Controller address
    frame.push(command); // Command

    if (subCommand !== null) {
      frame.push(subCommand);
    }

    frame.push(...data);

    // BCD frequency encoding helper
    if (command === 0x05 || command === 0x00) {
      frame.push(0xFD); // End
      return frame;
    }

    frame.push(0xFD); // End marker
    return frame;
  }

  sendCiVCommand(command, subCommand = null, data = []) {
    if (!this.port) return;
    const frame = this.buildCiVFrame(command, subCommand, data);
    this.port.write(Buffer.from(frame));
  }

  async setFrequency(freqHz) {
    const freqBCD = this.freqToBCD(freqHz);
    this.sendCiVCommand(0x05, null, freqBCD);
    this.emit('state', { frequency: freqHz });
  }

  async setMode(mode) {
    const modeMap = {
      LSB: 0x00, USB: 0x01, AM: 0x02, CW: 0x03,
      RTTY: 0x04, FM: 0x05, CWR: 0x07, FT8: 0x01, FT4: 0x01,
    };
    const modeByte = modeMap[mode] || 0x01;
    this.sendCiVCommand(0x06, null, [modeByte]);
    this.emit('state', { mode });
  }

  async setPTT(enable) {
    this.sendCiVCommand(0x1C, 0x00, [enable ? 0x01 : 0x00]);
    this.emit('state', { ptt: enable });
  }

  freqToBCD(freqHz) {
    const freqStr = (freqHz).toString().padStart(10, '0');
    const bcd = [];
    for (let i = freqStr.length - 2; i >= 0; i -= 2) {
      bcd.push(parseInt(freqStr.substring(i, i + 2), 10));
    }
    while (bcd.length < 5) bcd.push(0);
    return bcd.slice(0, 5).reverse();
  }

  startPolling() {
    this.pollTimer = setInterval(() => {
      this.sendCiVCommand(0x03); // Read operating frequency
    }, 200);
  }

  processCI_V(data) {
    for (let i = 0; i < data.length; i++) {
      if (data[i] === 0xFE && data[i + 1] === 0xFE && data[i + 2] === 0xE0 && data[i + 3] === this.ciVAddress) {
        const cmd = data[i + 4];
        const response = data.slice(i + 5, data.indexOf(0xFD, i + 5));

        if (cmd === 0x03 || cmd === 0x00) {
          if (response.length >= 5) {
            const freq = this.bcdToFreq(response.slice(0, 5));
            this.emit('state', { frequency: freq });
          }
        }
        break;
      }
    }
  }

  bcdToFreq(bcd) {
    let freq = 0;
    for (let i = 0; i < bcd.length; i++) {
      freq = freq * 100 + ((bcd[i] >> 4) * 10 + (bcd[i] & 0x0F));
    }
    return freq;
  }

  getSupportedModes() {
    return ['LSB', 'USB', 'CW', 'CWR', 'AM', 'FM', 'RTTY', 'FT8', 'FT4', 'PSK'];
  }

  async disconnect() {
    if (this.pollTimer) {
      clearInterval(this.pollTimer);
      this.pollTimer = null;
    }
    if (this.port) {
      this.port.close();
      this.port = null;
    }
    this.connected = false;
    this.emit('disconnect');
  }
}

module.exports = { IcomRadio, ICOM_CI_V_ADDRESSES };
