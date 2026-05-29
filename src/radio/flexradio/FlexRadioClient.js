const { EventEmitter } = require('events');
const net = require('net');

class FlexRadioClient extends EventEmitter {
  constructor(config) {
    super();
    this.config = config;
    this.ip = config.ip || '192.168.1.100';
    this.port = config.port || 4992;
    this.socket = null;
    this.connected = false;
    this.buffer = '';
    this.clientId = null;
    this.currentSlice = 'A';
  }

  async connect() {
    return new Promise((resolve, reject) => {
      this.socket = new net.Socket();

      const timeout = setTimeout(() => {
        reject(new Error('Timeout ao conectar FlexRadio'));
      }, 10000);

      this.socket.connect(this.port, this.ip, () => {
        clearTimeout(timeout);
        this.connected = true;
        this.sendCommand('c1|sub slice all');
        this.sendCommand('c2|sub pan all');
        this.sendCommand('c3|sub meter all');
        this.emit('state', { connected: true });
        resolve();
      });

      this.socket.on('data', (data) => {
        this.processData(data.toString());
      });

      this.socket.on('error', (err) => {
        clearTimeout(timeout);
        reject(err);
      });

      this.socket.on('close', () => {
        this.connected = false;
        this.emit('disconnect');
      });
    });
  }

  sendCommand(cmd) {
    if (this.socket && this.connected) {
      this.socket.write(cmd + '\n');
    }
  }

  processData(data) {
    this.buffer += data;
    const lines = this.buffer.split('\n');
    this.buffer = lines.pop() || '';

    for (const line of lines) {
      this.handleResponse(line.trim());
    }
  }

  handleResponse(line) {
    if (!line) return;

    if (line.startsWith('V1') || line.startsWith('H')) return;

    const parts = line.split('|');
    if (parts.length < 2) return;

    const prefix = parts[0];
    const status = parts[1];

    if (status === 'slice') {
      const slice = parts[2];
      const field = parts[3];
      const value = parts[4];

      if (slice === this.currentSlice) {
        switch (field) {
          case 'RF':
            this.emit('state', { frequency: parseFloat(value) * 1e6 });
            break;
          case 'mode':
            this.emit('state', { mode: value });
            break;
          case 'tx':
            this.emit('state', { ptt: value === '1' });
            break;
        }
      }
    }

    if (line.includes('client_id=')) {
      const match = line.match(/client_id=(\w+)/);
      if (match) this.clientId = match[1];
    }
  }

  async setFrequency(freqHz) {
    const freqMHz = (freqHz / 1e6).toFixed(6);
    this.sendCommand(`slice tune ${this.currentSlice} ${freqMHz}`);
    this.emit('state', { frequency: freqHz });
  }

  async setMode(mode) {
    const flexModeMap = {
      USB: 'USB', LSB: 'LSB', CW: 'CWU', AM: 'AM', FM: 'FM',
      RTTY: 'DIGU', FT8: 'DIGU', FT4: 'DIGU',
    };
    const flexMode = flexModeMap[mode] || 'USB';
    this.sendCommand(`slice set ${this.currentSlice} mode=${flexMode}`);
    this.emit('state', { mode });
  }

  async setPTT(enable) {
    this.sendCommand(`slice set ${this.currentSlice} tx=${enable ? '1' : '0'}`);
    this.emit('state', { ptt: enable });
  }

  getSupportedModes() {
    return ['USB', 'LSB', 'CW', 'CWU', 'CWL', 'AM', 'FM', 'NFM', 'DIGU', 'DIGL', 'SAM', 'SPEC'];
  }

  async disconnect() {
    if (this.socket) {
      this.sendCommand('c1|unsub slice all');
      this.socket.destroy();
      this.socket = null;
    }
    this.connected = false;
  }
}

module.exports = { FlexRadioClient };
