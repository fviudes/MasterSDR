const { EventEmitter } = require('events');
const { HermesLite2 } = require('../radio/hermeslite/HermesLite2');
const { AnanRadio } = require('../radio/anan/AnanRadio');
const { FlexRadioClient } = require('../radio/flexradio/FlexRadioClient');
const { IcomRadio } = require('../radio/icom/IcomRadio');
const { IcomIpRadio } = require('../radio/icom/IcomIpRadio');
const { YaesuRadio } = require('../radio/yaesu/YaesuRadio');
const { KenwoodRadio } = require('../radio/kenwood/KenwoodRadio');

const RADIO_TYPES = {
  HERMES_LITE_2: 'hermes_lite_2',
  ANAN: 'anan',
  FLEXRADIO: 'flexradio',
  ICOM: 'icom',
  ICOM_IP: 'icom_ip',
  YAESU: 'yaesu',
  KENWOOD: 'kenwood',
};

class RadioController extends EventEmitter {
  constructor() {
    super();
    this.radio = null;
    this.currentType = null;
    this.state = {
      connected: false,
      frequency: 14250000, // 14.250 MHz default
      mode: 'USB',
      ptt: false,
      vfo: 'A',
      split: false,
      txFrequency: null,
      power: 100,
      micGain: 50,
      rfGain: 100,
      squelch: 0,
      band: '20m',
      antenna: 1,
    };
  }

  async connect(config) {
    this.disconnect();

    this.currentType = config.type;

    switch (config.type) {
      case RADIO_TYPES.HERMES_LITE_2:
        this.radio = new HermesLite2(config);
        break;
      case RADIO_TYPES.ANAN:
        this.radio = new AnanRadio(config);
        break;
      case RADIO_TYPES.FLEXRADIO:
        this.radio = new FlexRadioClient(config);
        break;
      case RADIO_TYPES.ICOM:
        this.radio = new IcomRadio(config);
        break;
      case RADIO_TYPES.ICOM_IP:
        this.radio = new IcomIpRadio(config);
        break;
      case RADIO_TYPES.YAESU:
        this.radio = new YaesuRadio(config);
        break;
      case RADIO_TYPES.KENWOOD:
        this.radio = new KenwoodRadio(config);
        break;
      default:
        throw new Error(`Tipo de rádio não suportado: ${config.type}`);
    }

    this.radio.on('state', (newState) => {
      Object.assign(this.state, newState);
      this.emit('state', this.state);
    });

    this.radio.on('error', (error) => {
      this.emit('error', error);
    });

    this.radio.on('disconnect', () => {
      this.state.connected = false;
      this.emit('state', this.state);
    });

    if (this.radio.on) {
      this.radio.on('spectrum-data', (data) => this.emit('spectrum-data', data));
      this.radio.on('audio-data', (data) => this.emit('audio-data', data));
    }

    await this.radio.connect();
    this.state.connected = true;

    return this.state;
  }

  async disconnect() {
    if (this.radio) {
      await this.radio.disconnect();
      this.radio = null;
    }
    this.currentType = null;
    this.state.connected = false;
    return this.state;
  }

  getState() {
    return this.state;
  }

  async setFrequency(freqHz) {
    if (!this.radio) throw new Error('Rádio não conectado');
    this.state.frequency = freqHz;
    await this.radio.setFrequency(freqHz);
    return this.state;
  }

  async setMode(mode) {
    if (!this.radio) throw new Error('Rádio não conectado');
    this.state.mode = mode;
    await this.radio.setMode(mode);
    return this.state;
  }

  async setPTT(enable) {
    if (!this.radio) throw new Error('Rádio não conectado');
    this.state.ptt = enable;
    await this.radio.setPTT(enable);
    return this.state;
  }

  async getSupportedModes() {
    if (!this.radio) return ['USB', 'LSB', 'CW', 'AM', 'FM', 'RTTY', 'DIGITAL'];
    return this.radio.getSupportedModes();
  }
}

module.exports = { RadioController, RADIO_TYPES };
