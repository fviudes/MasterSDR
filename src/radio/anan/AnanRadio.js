const { EventEmitter } = require('events');
const dgram = require('dgram');

class AnanRadio extends EventEmitter {
  constructor(config) {
    super();
    this.config = config;
    this.ip = config.ip || '192.168.1.100';
    this.commandPort = config.commandPort || 1024;
    this.dataPort = config.dataPort || 1025;
    this.cmdSocket = null;
    this.dataSocket = null;
    this.connected = false;
  }

  async connect() {
    this.cmdSocket = dgram.createSocket('udp4');
    this.dataSocket = dgram.createSocket('udp4');

    return new Promise((resolve, reject) => {
      let bound = 0;
      const onBound = () => {
        bound++;
        if (bound >= 2) {
          this.connected = true;
          this.sendCommand('start');
          this.emit('state', { connected: true });
          resolve();
        }
      };

      this.cmdSocket.bind(0, onBound);
      this.dataSocket.bind(0, onBound);

      const timeout = setTimeout(() => reject(new Error('Timeout ANAN')), 5000);
      this.cmdSocket.on('message', () => clearTimeout(timeout));

      this.dataSocket.on('message', (msg) => {
        this.processIQData(msg);
      });
    });
  }

  sendCommand(cmd) {
    const buf = Buffer.alloc(64);
    buf.write(cmd, 0, cmd.length, 'ascii');
    this.cmdSocket.send(buf, 0, 64, this.commandPort, this.ip);
  }

  processIQData(msg) {
    const spectrum = [];
    for (let i = 0; i < msg.length - 3 && i < 1024; i += 4) {
      const i16 = msg.readInt16LE(i);
      const q16 = msg.readInt16LE(i + 2);
      spectrum.push({ i: i16, q: q16, magnitude: Math.sqrt(i16 * i16 + q16 * q16) / 32767 });
    }
    this.emit('iq-data', { spectrum });
  }

  async setFrequency(freqHz) {
    this.sendCommand(`freq ${freqHz}`);
    this.emit('state', { frequency: freqHz });
  }

  async setMode(mode) {
    this.sendCommand(`mode ${mode}`);
    this.emit('state', { mode });
  }

  async setPTT(enable) {
    this.sendCommand(`ptt ${enable ? 'on' : 'off'}`);
    this.emit('state', { ptt: enable });
  }

  getSupportedModes() {
    return ['USB', 'LSB', 'CW', 'AM', 'FM', 'DIGU', 'DIGL', 'SAM', 'SPEC'];
  }

  async disconnect() {
    if (this.cmdSocket) { this.cmdSocket.close(); this.cmdSocket = null; }
    if (this.dataSocket) { this.dataSocket.close(); this.dataSocket = null; }
    this.connected = false;
    this.emit('disconnect');
  }
}

module.exports = { AnanRadio };
