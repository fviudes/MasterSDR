import { useRef, useEffect, useCallback } from 'react';

const WATERFALL_WIDTH = 1024;
const WATERFALL_HEIGHT = 512;
const SPECTRUM_HEIGHT = 150;

export default function WaterfallPanel({ radioState, waterfallData }) {
  const waterfallCanvasRef = useRef(null);
  const spectrumCanvasRef = useRef(null);
  const waterfallBuffer = useRef([]);

  useEffect(() => {
    if (!waterfallData.waterfall) return;

    waterfallBuffer.current = waterfallData.waterfall.data.slice(-256);

    drawWaterfall();
    drawSpectrum();
  }, [waterfallData]);

  const drawWaterfall = useCallback(() => {
    const canvas = waterfallCanvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const w = canvas.width;
    const h = canvas.height;
    const buffer = waterfallBuffer.current;

    // Scroll up
    const scrollAmount = 1;
    const imageData = ctx.getImageData(0, 0, w, h);
    ctx.putImageData(imageData, 0, -scrollAmount);

    // Draw new line at bottom
    if (buffer.length > 0) {
      const row = buffer[buffer.length - 1] || [];
      const imageRow = ctx.createImageData(w, 1);
      const data = imageRow.data;

      for (let x = 0; x < w; x++) {
        const idx = Math.floor((x / w) * row.length);
        const value = row[idx] || -120;

        // Color mapping: blue (-120dB) -> green (-90dB) -> yellow (-70dB) -> red (-40dB)
        const normalized = Math.max(0, Math.min(1, (value + 120) / 80));
        let r, g, b;

        if (normalized < 0.25) {
          const t = normalized / 0.25;
          r = 0; g = 0; b = Math.floor(60 + 195 * t);
        } else if (normalized < 0.5) {
          const t = (normalized - 0.25) / 0.25;
          r = 0; g = Math.floor(255 * t); b = Math.floor(255 * (1 - t));
        } else if (normalized < 0.75) {
          const t = (normalized - 0.5) / 0.25;
          r = Math.floor(255 * t); g = 255; b = 0;
        } else {
          const t = (normalized - 0.75) / 0.25;
          r = 255; g = Math.floor(255 * (1 - t)); b = 0;
        }

        data[x * 4] = r;
        data[x * 4 + 1] = g;
        data[x * 4 + 2] = b;
        data[x * 4 + 3] = 255;
      }

      ctx.putImageData(imageRow, 0, h - 1);
    }

    requestAnimationFrame(() => {});
  }, []);

  const drawSpectrum = useCallback(() => {
    const canvas = spectrumCanvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const w = canvas.width;
    const h = canvas.height;

    ctx.fillStyle = 'var(--bg-primary)';
    ctx.fillRect(0, 0, w, h);

    // Grid
    ctx.strokeStyle = 'var(--grid-color)';
    ctx.lineWidth = 0.5;
    for (let db = -120; db <= 0; db += 10) {
      const y = h - ((db + 120) / 120) * h;
      ctx.beginPath();
      ctx.moveTo(0, y);
      ctx.lineTo(w, y);
      ctx.stroke();

      ctx.fillStyle = 'var(--text-muted)';
      ctx.font = '9px monospace';
      ctx.fillText(`${db}`, 2, y - 2);
    }

    // Spectrum data
    const spectrum = waterfallData.spectrum?.data;
    if (!spectrum || spectrum.length === 0) return;

    ctx.beginPath();
    ctx.strokeStyle = 'var(--spectrum-color)';
    ctx.lineWidth = 1.5;

    for (let x = 0; x < w; x++) {
      const idx = Math.floor((x / w) * spectrum.length);
      const value = spectrum[idx] || -120;
      const y = h - ((value + 120) / 120) * h;

      if (x === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    }
    ctx.stroke();

    // Fill below
    ctx.lineTo(w, h);
    ctx.lineTo(0, h);
    ctx.closePath();
    const gradient = ctx.createLinearGradient(0, 0, 0, h);
    gradient.addColorStop(0, 'rgba(77, 166, 255, 0.3)');
    gradient.addColorStop(1, 'rgba(77, 166, 255, 0.02)');
    ctx.fillStyle = gradient;
    ctx.fill();
  }, [waterfallData]);

  const centerFreq = radioState.frequency;
  const spanHz = 200000; // 200 kHz span
  const startFreq = centerFreq - spanHz / 2;
  const endFreq = centerFreq + spanHz / 2;

  return (
    <div style={{ flex: 1, display: 'flex', flexDirection: 'column', position: 'relative' }}>
      {/* Frequency axis */}
      <div style={{
        display: 'flex', justifyContent: 'space-between',
        padding: '2px 10px', background: 'var(--bg-panel)', borderBottom: '1px solid var(--border)',
        fontSize: 10, color: 'var(--text-muted)', fontFamily: 'monospace',
      }}>
        <span>{`${(startFreq / 1e6).toFixed(4)} MHz`}</span>
        <span style={{ color: 'var(--accent)', fontWeight: 600 }}>SPAN: {(spanHz / 1000).toFixed(0)} kHz</span>
        <span>{`${(endFreq / 1e6).toFixed(4)} MHz`}</span>
      </div>

      {/* Spectrum display */}
      <canvas
        ref={spectrumCanvasRef}
        width={WATERFALL_WIDTH}
        height={SPECTRUM_HEIGHT}
        style={{ width: '100%', height: SPECTRUM_HEIGHT, background: 'var(--bg-primary)' }}
      />

      {/* Waterfall display */}
      <canvas
        ref={waterfallCanvasRef}
        width={WATERFALL_WIDTH}
        height={WATERFALL_HEIGHT}
        style={{ flex: 1, width: '100%', background: 'var(--bg-primary)' }}
      />

      {/* Center frequency marker */}
      <div style={{
        position: 'absolute', top: 28, left: '50%', transform: 'translateX(-50%)',
        display: 'flex', flexDirection: 'column', alignItems: 'center', zIndex: 10,
        pointerEvents: 'none',
      }}>
        <div style={{
          width: 2, height: SPECTRUM_HEIGHT + 10, background: 'var(--vfo-a)',
          opacity: 0.8,
        }} />
        <div style={{
          fontSize: 9, color: 'var(--vfo-a)', fontFamily: 'monospace',
          background: 'var(--bg-primary)', padding: '1px 4px', borderRadius: 2,
        }}>
          {(radioState.frequency / 1e6).toFixed(4)}
        </div>
      </div>

      {/* Filter overlay */}
      <div style={{
        position: 'absolute', top: 28, left: '50%', transform: 'translateX(-50%)',
        height: SPECTRUM_HEIGHT, width: 60,
        background: 'rgba(255, 107, 107, 0.08)',
        border: '1px solid rgba(255, 107, 107, 0.2)',
        borderRadius: 3, zIndex: 5, pointerEvents: 'none',
      }} />
    </div>
  );
}
