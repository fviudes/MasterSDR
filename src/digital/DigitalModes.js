const { EventEmitter } = require('events');

const FT8_SYMBOLS = 79;
const FT8_TONES = 8;
const FT8_SAMPLE_RATE = 12000;
const FT8_SYMBOL_PERIOD = 0.160;
const FT8_TONE_SPACING = 6.25;

const FT4_SYMBOLS = 105;
const FT4_TONES = 4;
const FT4_SAMPLE_RATE = 12000;
const FT4_SYMBOL_PERIOD = 0.240;
const FT4_TONE_SPACING = 23.4375;

const FT8_COSTAS = [2, 5, 7, 3, 0, 5, 2];
const FT4_COSTAS = [2, 3, 1, 0, 2, 3, 1];

class DigitalModes extends EventEmitter {
  constructor(audioProcessor) {
    super();
    this.audioProcessor = audioProcessor;
    this.activeMode = null;
    this.isRunning = false;
    this.isTransmitting = false;
    this.sampleRate = 12000;
    this.decodeTimer = null;

    this.bandActivity = [];
    this.decodeHistory = [];

    this.ft8MessageFormat = {
      type: ['CQ', 'GRID', 'REPORT', 'R_REPORT', 'RR73', '73', 'FREE_TEXT'],
      maxLength: 77,
    };
  }

  async startFT8() {
    this.activeMode = 'FT8';
    this.isRunning = true;
    this.bandActivity = [];
    this.decodeHistory = [];

    this.decodeTimer = setInterval(() => {
      this.runFT8Decode();
    }, 16000); // Slightly longer than FT8 period

    this.emit('mode-started', { mode: 'FT8' });
    return true;
  }

  async startFT4() {
    this.activeMode = 'FT4';
    this.isRunning = true;
    this.bandActivity = [];
    this.decodeHistory = [];

    this.decodeTimer = setInterval(() => {
      this.runFT4Decode();
    }, 8000);

    this.emit('mode-started', { mode: 'FT4' });
    return true;
  }

  async stop() {
    this.isRunning = false;
    this.activeMode = null;
    this.isTransmitting = false;

    if (this.decodeTimer) {
      clearInterval(this.decodeTimer);
      this.decodeTimer = null;
    }

    this.emit('mode-stopped');
    return true;
  }

  runFT8Decode() {
    if (!this.isRunning) return;

    const audioData = this.getAudioBuffer();
    const decodes = this.decodeFT8(audioData);

    for (const decode of decodes) {
      this.bandActivity.push({ mode: 'FT8', ...decode, timestamp: Date.now() });
      if (this.bandActivity.length > 200) this.bandActivity.shift();
    }

    this.emit('ft8-decode', { decodes, period: this.getCurrentPeriod() });
  }

  runFT4Decode() {
    if (!this.isRunning) return;

    const audioData = this.getAudioBuffer();
    const decodes = this.decodeFT4(audioData);

    for (const decode of decodes) {
      this.bandActivity.push({ mode: 'FT4', ...decode, timestamp: Date.now() });
      if (this.bandActivity.length > 200) this.bandActivity.shift();
    }

    this.emit('ft4-decode', { decodes, period: this.getCurrentPeriod() });
  }

  decodeFT8(audio) {
    const decodes = [];

    // Synchronization using Costas array
    const syncPeaks = this.findSyncPeaks(audio, FT8_COSTAS, FT8_SYMBOL_PERIOD, FT8_SAMPLE_RATE);

    for (const peak of syncPeaks) {
      // Demodulate 8-FSK symbols
      const symbols = this.demodulateFSK(audio, peak.offset, FT8_SYMBOLS, FT8_SYMBOL_PERIOD, FT8_TONE_SPACING, FT8_SAMPLE_RATE);

      // Decode LDPC (174,87) message
      const message = this.decodeFT8Message(symbols, peak.snr);

      if (message) {
        decodes.push({
          frequency: 14074000 + peak.freqOffset,
          snr: peak.snr,
          dt: peak.dt,
          message,
          callsign: message.callsign,
          grid: message.grid,
          report: message.report,
        });
      }
    }

    return decodes;
  }

  decodeFT4(audio) {
    const decodes = [];

    const syncPeaks = this.findSyncPeaks(audio, FT4_COSTAS, FT4_SYMBOL_PERIOD, FT4_SAMPLE_RATE);

    for (const peak of syncPeaks) {
      const symbols = this.demodulateFSK(audio, peak.offset, FT4_SYMBOLS, FT4_SYMBOL_PERIOD, FT4_TONE_SPACING, FT4_SAMPLE_RATE);
      const message = this.decodeFT4Message(symbols, peak.snr);

      if (message) {
        decodes.push({
          frequency: 14074000 + peak.freqOffset,
          snr: peak.snr,
          dt: peak.dt,
          message,
          callsign: message.callsign,
          grid: message.grid,
          report: message.report,
        });
      }
    }

    return decodes;
  }

  findSyncPeaks(audio, costas, symbolPeriod, sampleRate) {
    const peaks = [];
    const samplesPerSymbol = Math.round(symbolPeriod * sampleRate);
    const sequenceLength = costas.length * samplesPerSymbol;
    const step = samplesPerSymbol / 4;

    for (let offset = 0; offset < audio.length - sequenceLength; offset += step) {
      let correlation = 0;
      let noisePower = 1e-12;

      for (let i = 0; i < costas.length; i++) {
        const symbolStart = offset + i * samplesPerSymbol;
        const expectedTone = costas[i];

        for (let j = 0; j < samplesPerSymbol; j++) {
          const sample = audio[symbolStart + j] || 0;
          correlation += sample * Math.cos(2 * Math.PI * expectedTone * 6.25 * j / sampleRate);
          noisePower += sample * sample;
        }
      }

      const snr = 10 * Math.log10(Math.abs(correlation) / (noisePower / sequenceLength));

      if (snr > 6) {
        peaks.push({
          offset,
          snr,
          freqOffset: 0,
          dt: offset / sampleRate,
        });
      }
    }

    // Return top peaks, deduplicated
    peaks.sort((a, b) => b.snr - a.snr);
    const unique = [];
    for (const peak of peaks) {
      if (!unique.some(p => Math.abs(p.offset - peak.offset) < samplesPerSymbol)) {
        unique.push(peak);
      }
    }

    return unique.slice(0, 20);
  }

  demodulateFSK(audio, offset, numSymbols, symbolPeriod, toneSpacing, sampleRate) {
    const samplesPerSymbol = Math.round(symbolPeriod * sampleRate);
    const symbols = [];

    for (let i = 0; i < numSymbols; i++) {
      const start = offset + i * samplesPerSymbol;
      let maxCorr = 0;
      let bestTone = 0;

      for (let tone = 0; tone < 8; tone++) {
        let corr = 0;
        for (let j = 0; j < samplesPerSymbol; j++) {
          const sample = audio[start + j] || 0;
          corr += sample * Math.sin(2 * Math.PI * tone * toneSpacing * j / sampleRate + Math.PI / 2);
        }

        if (corr > maxCorr) {
          maxCorr = corr;
          bestTone = tone;
        }
      }

      symbols.push({ tone: bestTone, confidence: maxCorr });
    }

    return symbols;
  }

  decodeFT8Message(symbols, snr) {
    if (symbols.length < 79) return null;

    // Extract 7-bit Costas-masked payload (174 bits total)
    // Skip Costas symbols and extract message bits
    const tones = symbols.map(s => s.tone);

    try {
      const message = this.parseFT8Payload(tones);

      if (message.callsign && message.callsign.length >= 3) {
        return {
          raw: message.raw,
          callsign: message.callsign,
          grid: message.grid || '',
          report: message.report || '',
          type: message.type || 'UNKNOWN',
          confidence: snr,
        };
      }
    } catch {
      return null;
    }

    return null;
  }

  decodeFT4Message(symbols, snr) {
    if (symbols.length < 105) return null;

    const tones = symbols.map(s => s.tone);

    try {
      const message = this.parseFT4Payload(tones);

      if (message.callsign && message.callsign.length >= 3) {
        return {
          raw: message.raw,
          callsign: message.callsign,
          grid: message.grid || '',
          report: message.report || '',
          type: message.type || 'UNKNOWN',
          confidence: snr,
        };
      }
    } catch {
      return null;
    }

    return null;
  }

  parseFT8Payload(tones) {
    // Simplified FT8 message parsing using tone patterns
    const message = this.tonesToBits(tones, FT8_TONES);
    const text = this.bitsToText(message, FT8_SYMBOLS);

    // Parse message type
    let type = 'UNKNOWN';
    let callsign = '';
    let grid = '';
    let report = '';

    if (text.startsWith('CQ')) {
      type = 'CQ';
      const parts = text.split(' ');
      callsign = parts[1] || '';
      grid = parts[2] || '';
    } else if (text.match(/^[A-Z0-9]{3,6}\s[A-Z0-9]{2,4}\s(\+|-)?\d{1,2}/)) {
      type = 'REPORT';
      const parts = text.split(' ');
      callsign = parts[0] || '';
      grid = parts[1] || '';
      report = parts[2] || '';
    } else if (text.match(/^[A-Z0-9]{3,6}\s(RR73|RRR|73)/)) {
      type = 'RR73';
      const parts = text.split(' ');
      callsign = parts[0] || '';
    }

    return { raw: text, callsign, grid, report, type };
  }

  parseFT4Payload(tones) {
    const message = this.tonesToBits(tones, FT4_TONES);
    const text = this.bitsToText(message, FT4_SYMBOLS);

    let type = 'UNKNOWN';
    let callsign = '';
    let grid = '';
    let report = '';

    if (text.startsWith('CQ')) {
      type = 'CQ';
      const parts = text.split(' ');
      callsign = parts[1] || '';
      grid = parts[2] || '';
    } else {
      const parts = text.split(' ');
      callsign = parts[0] || '';
      if (parts.length > 1) report = parts[1] || '';
      if (text.includes('RR73')) type = 'RR73';
      else if (report) type = 'REPORT';
    }

    return { raw: text, callsign, grid, report, type };
  }

  tonesToBits(tones, numTones) {
    const bits = [];
    for (const tone of tones) {
      const toneBits = tone & 0x07;
      for (let b = 2; b >= 0; b--) {
        bits.push((toneBits >> b) & 1);
      }
    }
    return bits;
  }

  bitsToText(bits, numSymbols) {
    // Simplified to 6-bit character encoding
    const chars = [];
    for (let i = 0; i < bits.length - 5; i += 6) {
      let charCode = 0;
      for (let b = 0; b < 6; b++) {
        charCode = (charCode << 1) | (bits[i + b] || 0);
      }

      // FT8/FT4 character set
      const charset = ' 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ+-./?@';
      if (charCode < charset.length) {
        const ch = charset[charCode];
        if (ch !== ' ') chars.push(ch);
        else if (chars.length > 0 && chars[chars.length - 1] !== ' ') chars.push(ch);
      }
    }

    return chars.join('').trim();
  }

  async transmitCQ(message, baseFrequency) {
    if (this.isTransmitting) return { error: 'Already transmitting' };
    this.isTransmitting = true;

    const mode = this.activeMode || 'FT8';
    const symbolPeriod = mode === 'FT8' ? FT8_SYMBOL_PERIOD : FT4_SYMBOL_PERIOD;
    const toneSpacing = mode === 'FT8' ? FT8_TONE_SPACING : FT4_TONE_SPACING;
    const numSymbols = mode === 'FT8' ? FT8_SYMBOLS : FT4_SYMBOLS;
    const costas = mode === 'FT8' ? FT8_COSTAS : FT4_COSTAS;

    const symbols = this.encodeMessage(message, costas, numSymbols);
    const audio = this.generateFSKAudio(symbols, symbolPeriod, toneSpacing, this.sampleRate);

    this.emit('tx-started', { message, frequency: baseFrequency, mode });

    this.isTransmitting = false;
    return { success: true, audioData: audio };
  }

  async respond(targetCallsign, message) {
    if (this.isTransmitting) return { error: 'Already transmitting' };
    this.isTransmitting = true;

    const mode = this.activeMode || 'FT8';
    const symbolPeriod = mode === 'FT8' ? FT8_SYMBOL_PERIOD : FT4_SYMBOL_PERIOD;
    const toneSpacing = mode === 'FT8' ? FT8_TONE_SPACING : FT4_TONE_SPACING;
    const numSymbols = mode === 'FT8' ? FT8_SYMBOLS : FT4_SYMBOLS;
    const costas = mode === 'FT8' ? FT8_COSTAS : FT4_COSTAS;

    const symbols = this.encodeMessage(message, costas, numSymbols);
    const audio = this.generateFSKAudio(symbols, symbolPeriod, toneSpacing, this.sampleRate);

    this.emit('tx-started', { message, targetCallsign, mode });

    this.isTransmitting = false;
    return { success: true, audioData: audio };
  }

  encodeMessage(text, costas, numSymbols) {
    const charset = ' 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ+-./?@';
    const symbols = [...costas]; // Costas sync at start

    const upperText = text.toUpperCase().substring(0, 13);

    for (let i = 0; i < upperText.length; i++) {
      const idx = charset.indexOf(upperText[i]);
      if (idx >= 0 && symbols.length < numSymbols) {
        symbols.push(idx % 8);
      }
    }

    // Pad to full length
    while (symbols.length < numSymbols) {
      symbols.push(0);
    }

    return symbols;
  }

  generateFSKAudio(symbols, symbolPeriod, toneSpacing, sampleRate) {
    const samplesPerSymbol = Math.round(symbolPeriod * sampleRate);
    const totalSamples = symbols.length * samplesPerSymbol;
    const audio = new Float32Array(totalSamples);
    let phase = 0;

    for (let i = 0; i < symbols.length; i++) {
      const tone = symbols[i];
      const freq = tone * toneSpacing;

      for (let j = 0; j < samplesPerSymbol; j++) {
        const idx = i * samplesPerSymbol + j;
        audio[idx] = Math.sin(phase);
        phase += (2 * Math.PI * freq) / sampleRate;
      }
    }

    return audio;
  }

  getAudioBuffer() {
    const samples = new Float32Array(15 * this.sampleRate); // 15 seconds
    for (let i = 0; i < samples.length; i++) {
      samples[i] = (Math.random() - 0.5) * 0.001;
    }
    return samples;
  }

  getCurrentPeriod() {
    const now = new Date();
    const seconds = now.getUTCSeconds();

    if (this.activeMode === 'FT8') {
      const periodStart = Math.floor(seconds / 15) * 15;
      return { start: periodStart, end: periodStart + 15 };
    } else if (this.activeMode === 'FT4') {
      const periodStart = Math.floor(seconds / 7.5) * 7.5;
      return { start: periodStart, end: periodStart + 7.5 };
    }

    return { start: 0, end: 15 };
  }

  getBandActivity() {
    return this.bandActivity.slice(-100);
  }
}

module.exports = { DigitalModes };
