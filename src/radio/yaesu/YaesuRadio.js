const { EventEmitter } = require('events');
const { SerialPort } = require('serialport');

class YaesuRadio extends EventEmitter {
  constructor(config) {
    super();
    this.config = config;
    this.portPath = config.port;
    this.baudRate = config.baudRate || 38400;
    this.model = config.model || 'FT-991A';
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
        stopBits: 2,
        parity: 'none',
        autoOpen: false,
      });

      this.port.open((err) => {
        if (err) {
          reject(new Error(`Erro ao abrir porta CAT Yaesu: ${err.message}`));
          return;
        }
        this.connected = true;

        this.port.on('data', (data) => {
          this.processCAT(data);
        });

        this.startPolling();
        this.emit('state', { connected: true });
        resolve();
      });
    });
  }

  sendCATCommand(cmd) {
    if (!this.port) return;
    this.port.write(cmd + ';');
  }

  async setFrequency(freqHz) {
    const freqStr = freqHz.toString().padStart(9, '0');
    this.sendCATCommand(`FA${freqStr}`);
    this.emit('state', { frequency: freqHz });
  }

  async setMode(mode) {
    const modeMap = {
      LSB: '01', USB: '02', CW: '03', CWR: '07',
      AM: '04', FM: '05', RTTY: '08', PKT: '0C',
      C4FM: '08',
    };
    const modeCode = modeMap[mode] || '02';
    this.sendCATCommand(`MD0${modeCode}`);
    this.emit('state', { mode });
  }

  async setPTT(enable) {
    this.sendCATCommand(enable ? 'TX1' : 'RX');
    this.emit('state', { ptt: enable });
  }

  queryFrequency() {
    this.sendCATCommand('FA');
  }

  queryMode() {
    this.sendCATCommand('MD0');
  }

  queryMeter() {
    this.sendCATCommand('RM');
  }

  startPolling() {
    this.pollTimer = setInterval(() => {
      this.queryFrequency();
      setTimeout(() => this.queryMode(), 50);
      setTimeout(() => this.queryMeter(), 100);
    }, 500);
  }

  processCAT(data) {
    const response = data.toString().trim();

    if (response.startsWith('FA')) {
      const freq = parseInt(response.substring(2, 11));
      if (freq > 100000) {
        this.emit('state', { frequency: freq });
      }
    } else if (response.startsWith('MD')) {
      const modeMap = {
        '01': 'LSB', '02': 'USB', '03': 'CW', '04': 'AM',
        '05': 'FM', '07': 'CWR', '08': 'RTTY', '0C': 'PKT',
      };
      const modeCode = response.substring(2, 4);
      this.emit('state', { mode: modeMap[modeCode] || 'USB' });
    }
  }

  getSupportedModes() {
    return ['LSB', 'USB', 'CW', 'CWR', 'AM', 'FM', 'RTTY', 'PKT', 'C4FM'];
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

module.exports = { YaesuRadio };
