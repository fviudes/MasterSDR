const { EventEmitter } = require('events');
const dgram = require('dgram');

class HermesLite2 extends EventEmitter {
  constructor(config) {
    super();
    this.config = config;
    this.ip = config.ip || '192.168.10.3';
    this.port = config.port || 1024;
    this.socket = null;
    this.connected = false;
  }

  async connect() {
    this.socket = dgram.createSocket('udp4');

    return new Promise((resolve, reject) => {
      const timeout = setTimeout(() => {
        reject(new Error('Timeout ao conectar Hermes Lite 2'));
      }, 5000);

      this.socket.bind(0, () => {
        clearTimeout(timeout);
        this.connected = true;

        this.startDiscovery();
        this.emit('state', { connected: true });
        resolve();
      });

      this.socket.on('error', (err) => {
        clearTimeout(timeout);
        reject(err);
      });
    });
  }

  startDiscovery() {
    const discoveryPacket = Buffer.alloc(63);
    discoveryPacket.writeUInt32BE(0x7f800000, 0); // Magic
    discoveryPacket.write('HermesLite2_Discovery', 4, 'ascii');

    this.socket.send(discoveryPacket, 0, discoveryPacket.length, this.port, this.ip);

    this.socket.on('message', (msg) => {
      this.processPacket(msg);
    });
  }

  processPacket(msg) {
    if (msg.length < 4) return;
    const sync = msg.readUInt32BE(0);

    if (sync === 0x7F7F7F7F) {
      this.processIQData(msg);
    }
  }

  processIQData(msg) {
    const spectrum = [];
    const samplesPerFrame = (msg.length - 4) / 6; // 24-bit I/Q pairs
    for (let i = 0; i < samplesPerFrame && i < 512; i++) {
      const offset = 4 + i * 6;
      const iVal = (msg.readInt8(offset) << 16) | (msg.readUInt16BE(offset + 1));
      const qVal = (msg.readInt8(offset + 3) << 16) | (msg.readUInt16BE(offset + 4));
      spectrum.push({ i: iVal, q: qVal, magnitude: Math.sqrt(iVal * iVal + qVal * qVal) / 8388607 });
    }

    this.emit('iq-data', { spectrum });
  }

  async setFrequency(freqHz) {
    if (!this.socket) return;
    const packet = Buffer.alloc(16);
    packet.writeUInt32BE(0x00000000, 0);
    packet.writeBigUInt64BE(BigInt(freqHz), 4);
    this.socket.send(packet, 0, packet.length, this.port, this.ip);
  }

  async setMode(mode) {
    // Hermes Lite 2 is SDR - mode handled in software
    this.emit('state', { mode });
  }

  async setPTT(enable) {
    if (!this.socket) return;
    const packet = Buffer.alloc(8);
    packet.writeUInt32BE(0x00000001, 0);
    packet.writeUInt32BE(enable ? 1 : 0, 4);
    this.socket.send(packet, 0, packet.length, this.port, this.ip);
    this.emit('state', { ptt: enable });
  }

  getSupportedModes() {
    return ['USB', 'LSB', 'CW', 'AM', 'FM', 'RTTY', 'FT8', 'FT4', 'DIGU', 'DIGL'];
  }

  async disconnect() {
    if (this.socket) {
      this.socket.close();
      this.socket = null;
    }
    this.connected = false;
    this.emit('disconnect');
  }
}

module.exports = { HermesLite2 };
