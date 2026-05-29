const { EventEmitter } = require('events');
const { SerialPort } = require('serialport');

class KenwoodRadio extends EventEmitter {
  constructor(config) {
    super();
    this.config = config;
    this.portPath = config.port;
    this.baudRate = config.baudRate || 9600;
    this.model = config.model || 'TS-590';
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
        stopBits: config.stopBits || 1,
        parity: 'none',
        autoOpen: false,
      });

      this.port.open((err) => {
        if (err) {
          reject(new Error(`Erro ao abrir porta CAT Kenwood: ${err.message}`));
          return;
        }
        this.connected = true;

        this.port.on('data', (data) => {
          this.processCAT(data);
        });

        this.sendCATCommand('AI1'); // Auto-information on
        this.startPolling();
        this.emit('state', { connected: true });
        resolve();
      });
    });
  }

  sendCATCommand(cmd) {
    if (!this.port) return;
    const fullCmd = cmd + ';';
    this.port.write(fullCmd);
  }

  async setFrequency(freqHz) {
    const freqStr = freqHz.toString().padStart(11, '0');
    this.sendCATCommand(`FA${freqStr}`);
    this.emit('state', { frequency: freqHz });
  }

  async setMode(mode) {
    const modeMap = {
      LSB: '1', USB: '2', CW: '3', FM: '4', AM: '5',
      FSK: '6', CWR: '7', FT8: '2', FT4: '2',
    };
    const modeCode = modeMap[mode] || '2';
    this.sendCATCommand(`MD${modeCode}`);
    this.emit('state', { mode });
  }

  async setPTT(enable) {
    this.sendCATCommand(enable ? 'TX' : 'RX');
    this.emit('state', { ptt: enable });
  }

  queryFrequency() {
    this.sendCATCommand('FA');
  }

  queryMode() {
    this.sendCATCommand('MD');
  }

  queryS meter() {
    this.sendCATCommand('SM');
  }

  startPolling() {
    this.pollTimer = setInterval(() => {
      this.queryFrequency();
      setTimeout(() => this.queryMode(), 50);
    }, 300);
  }

  processCAT(data) {
    const text = data.toString().trim();
    const parts = text.split(';');

    for (const part of parts) {
      if (part.startsWith('FA')) {
        const freq = parseInt(part.substring(2));
        if (freq > 100000) {
          this.emit('state', { frequency: freq });
        }
      } else if (part.startsWith('MD')) {
        const modeMap = {
          '1': 'LSB', '2': 'USB', '3': 'CW', '4': 'FM',
          '5': 'AM', '6': 'FSK', '7': 'CWR',
        };
        const modeNum = part.substring(2);
        if (modeMap[modeNum]) {
          this.emit('state', { mode: modeMap[modeNum] });
        }
      }
    }
  }

  getSupportedModes() {
    return ['LSB', 'USB', 'CW', 'CWR', 'FM', 'AM', 'FSK', 'FT8', 'FT4', 'PSK'];
  }

  async disconnect() {
    if (this.pollTimer) {
      clearInterval(this.pollTimer);
      this.pollTimer = null;
    }
    if (this.port) {
      this.sendCATCommand('AI0');
      this.port.close();
      this.port = null;
    }
    this.connected = false;
    this.emit('disconnect');
  }
}

module.exports = { KenwoodRadio };
