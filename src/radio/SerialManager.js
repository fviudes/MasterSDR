const { SerialPort } = require('serialport');
const { EventEmitter } = require('events');

class SerialManager extends EventEmitter {
  constructor() {
    super();
    this.ports = [];
    this.activeConnections = new Map();
  }

  async refreshPorts() {
    this.ports = await SerialPort.list();
    return this.ports;
  }

  async getPorts() {
    if (this.ports.length === 0) await this.refreshPorts();
    return this.ports.map(p => ({
      path: p.path,
      manufacturer: p.manufacturer || 'Unknown',
      serialNumber: p.serialNumber,
      vendorId: p.vendorId,
      productId: p.productId,
      pnpId: p.pnpId,
    }));
  }

  async openConnection(portPath, baudRate = 9600, options = {}) {
    const port = new SerialPort({
      path: portPath,
      baudRate,
      dataBits: options.dataBits || 8,
      stopBits: options.stopBits || 1,
      parity: options.parity || 'none',
      autoOpen: false,
    });

    await new Promise((resolve, reject) => {
      port.open((err) => {
        if (err) reject(err);
        else resolve();
      });
    });

    const key = portPath;
    this.activeConnections.set(key, port);
    return port;
  }

  closeConnection(portPath) {
    const port = this.activeConnections.get(portPath);
    if (port) {
      port.close();
      this.activeConnections.delete(portPath);
    }
  }

  closeAll() {
    for (const [key, port] of this.activeConnections) {
      port.close();
    }
    this.activeConnections.clear();
  }
}

const serialManager = new SerialManager();

async function initSerialPorts() {
  return await serialManager.refreshPorts();
}

async function getSerialPorts() {
  return await serialManager.getPorts();
}

module.exports = { SerialManager, serialManager, initSerialPorts, getSerialPorts };
