const { EventEmitter } = require('events');

class AudioProcessor extends EventEmitter {
  constructor() {
    super();
    this.sampleRate = 48000;
    this.fftSize = 2048;
    this.waterfallSize = 1024;
    this.isRunning = false;
    this.audioBuffer = new Float32Array(this.fftSize * 2);
    this.bufferIndex = 0;
    this.waterfallHistory = [];
    this.maxWaterfallLines = 256;
    this.frameCount = 0;
    this.processingInterval = null;
  }

  async start() {
    this.isRunning = true;
    this.waterfallHistory = [];

    this.processingInterval = setInterval(() => {
      this.generateFFTFrame();
    }, 1000 / 30); // 30 FPS

    return true;
  }

  async stop() {
    this.isRunning = false;
    if (this.processingInterval) {
      clearInterval(this.processingInterval);
      this.processingInterval = null;
    }
    return true;
  }

  pushAudioSamples(samples) {
    if (!this.isRunning) return;

    const floatSamples = new Float32Array(samples);
    for (let i = 0; i < floatSamples.length && this.bufferIndex < this.audioBuffer.length; i++) {
      this.audioBuffer[this.bufferIndex++] = floatSamples[i];
    }

    if (this.bufferIndex >= this.fftSize) {
      this.processFrame();
      this.bufferIndex = 0;
    }
  }

  processFrame() {
    const fftData = this.computeFFT(this.audioBuffer.slice(0, this.fftSize));
    this.updateWaterfall(fftData);

    this.emit('spectrum', {
      data: Array.from(fftData),
      sampleRate: this.sampleRate,
      fftSize: this.fftSize,
    });

    this.frameCount++;
  }

  generateFFTFrame() {
    const fftData = this.computeFFT(this.audioBuffer.slice(0, this.fftSize));
    this.updateWaterfall(fftData);

    this.emit('spectrum', {
      data: Array.from(fftData),
      sampleRate: this.sampleRate,
      fftSize: this.fftSize,
    });
  }

  computeFFT(samples) {
    const fftSize = this.fftSize;
    const halfSize = fftSize / 2;
    const real = new Float32Array(fftSize);
    const imag = new Float32Array(fftSize);

    // Apply Hann window
    for (let i = 0; i < samples.length; i++) {
      const window = 0.5 * (1 - Math.cos((2 * Math.PI * i) / (fftSize - 1)));
      real[i] = samples[i] * window;
    }

    // Radix-2 FFT
    this.radix2FFT(real, imag, -1);

    const magnitudes = new Float32Array(halfSize);
    for (let i = 0; i < halfSize; i++) {
      const mag = Math.sqrt(real[i] * real[i] + imag[i] * imag[i]) / fftSize;
      magnitudes[i] = 20 * Math.log10(Math.max(mag, 1e-10));
    }

    return magnitudes;
  }

  radix2FFT(real, imag, direction) {
    const n = real.length;
    if (n <= 1) return;

    // Bit reversal
    let j = 0;
    for (let i = 0; i < n; i++) {
      if (i < j) {
        [real[i], real[j]] = [real[j], real[i]];
        [imag[i], imag[j]] = [imag[j], imag[i]];
      }
      let m = n >> 1;
      while (m >= 1 && j >= m) {
        j -= m;
        m >>= 1;
      }
      j += m;
    }

    // FFT butterflies
    for (let size = 2; size <= n; size <<= 1) {
      const halfSize = size >> 1;
      const angle = (direction * 2 * Math.PI) / size;
      const wReal = Math.cos(angle);
      const wImag = Math.sin(angle);

      for (let i = 0; i < n; i += size) {
        let wr = 1, wi = 0;
        for (let k = 0; k < halfSize; k++) {
          const tr = wr * real[i + k + halfSize] - wi * imag[i + k + halfSize];
          const ti = wr * imag[i + k + halfSize] + wi * real[i + k + halfSize];

          real[i + k + halfSize] = real[i + k] - tr;
          imag[i + k + halfSize] = imag[i + k] - ti;
          real[i + k] += tr;
          imag[i + k] += ti;

          const newWr = wr * wReal - wi * wImag;
          const newWi = wr * wImag + wi * wReal;
          wr = newWr;
          wi = newWi;
        }
      }
    }
  }

  updateWaterfall(fftData) {
    const downsampled = this.downsample(fftData, this.waterfallSize);
    this.waterfallHistory.push(Array.from(downsampled));

    if (this.waterfallHistory.length > this.maxWaterfallLines) {
      this.waterfallHistory.shift();
    }

    this.emit('waterfall', {
      data: [...this.waterfallHistory],
      width: this.waterfallSize,
      height: this.waterfallHistory.length,
    });
  }

  downsample(data, targetSize) {
    const ratio = data.length / targetSize;
    const result = new Float32Array(targetSize);
    for (let i = 0; i < targetSize; i++) {
      let sum = 0;
      const start = Math.floor(i * ratio);
      const end = Math.floor((i + 1) * ratio);
      for (let j = start; j < end && j < data.length; j++) {
        sum += data[j];
      }
      result[i] = sum / Math.max(1, end - start);
    }
    return result;
  }

  getSpectrumData() {
    const fft = this.computeFFT(this.audioBuffer.slice(0, this.fftSize));
    return {
      data: Array.from(fft),
      sampleRate: this.sampleRate,
      fftSize: this.fftSize,
    };
  }

  getWaterfallData() {
    return {
      data: this.waterfallHistory,
      width: this.waterfallSize,
      height: this.waterfallHistory.length,
    };
  }

  applyFilter(samples, lowFreq, highFreq, sampleRate) {
    const nyquist = sampleRate / 2;
    const lowNorm = lowFreq / nyquist;
    const highNorm = highFreq / nyquist;

    const result = new Float32Array(samples.length);
    const kernelSize = 65;
    const kernel = new Float32Array(kernelSize);
    const center = (kernelSize - 1) / 2;

    for (let i = 0; i < kernelSize; i++) {
      const n = i - center;
      if (n === 0) {
        kernel[i] = 2 * (highNorm - lowNorm);
      } else {
        kernel[i] = (Math.sin(2 * Math.PI * highNorm * n) - Math.sin(2 * Math.PI * lowNorm * n)) / (Math.PI * n);
      }
      // Hamming window
      kernel[i] *= 0.54 - 0.46 * Math.cos((2 * Math.PI * i) / (kernelSize - 1));
    }

    // Convolution
    for (let i = 0; i < samples.length; i++) {
      for (let k = 0; k < kernelSize; k++) {
        const idx = i - k + center;
        if (idx >= 0 && idx < samples.length) {
          result[i] += samples[idx] * kernel[k];
        }
      }
    }

    return result;
  }
}

module.exports = { AudioProcessor };
