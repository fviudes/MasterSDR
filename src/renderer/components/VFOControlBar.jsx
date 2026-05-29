import { useState, useEffect, useCallback } from 'react';
import { Minus, Plus, ChevronUp, ChevronDown, Mic, MicOff } from 'lucide-react';

const BANDS = [
  { name: '160m', min: 1800000, max: 2000000 },
  { name: '80m', min: 3500000, max: 4000000 },
  { name: '40m', min: 7000000, max: 7300000 },
  { name: '30m', min: 10100000, max: 10150000 },
  { name: '20m', min: 14000000, max: 14350000 },
  { name: '17m', min: 18068000, max: 18168000 },
  { name: '15m', min: 21000000, max: 21450000 },
  { name: '12m', min: 24890000, max: 24990000 },
  { name: '10m', min: 28000000, max: 29700000 },
  { name: '6m', min: 50000000, max: 54000000 },
];

const MODES = ['LSB', 'USB', 'CW', 'CWR', 'AM', 'FM', 'RTTY', 'FT8', 'FT4', 'DIGU'];

function formatFrequency(hz) {
  const mhz = (hz / 1e6).toFixed(6);
  const parts = mhz.split('.');
  return (
    <span style={{ fontFamily: 'monospace', fontSize: 22, fontWeight: 700 }}>
      <span style={{ color: 'var(--vfo-a)' }}>{parts[0]}</span>
      <span style={{ color: 'var(--text-muted)' }}>.</span>
      <span style={{ color: 'var(--vfo-a)' }}>{parts[1]?.substring(0, 3)}</span>
      <span style={{ color: 'var(--text-muted)', fontSize: 16 }}>{parts[1]?.substring(3, 6)}</span>
    </span>
  );
}

export default function VFOControlBar({ radioState, onSetFrequency, onSetMode, onPTT }) {
  const [step, setStep] = useState(100);
  const [inputFreq, setInputFreq] = useState('');

  useEffect(() => {
    setInputFreq((radioState.frequency / 1e6).toFixed(6));
  }, [radioState.frequency]);

  const tuneStep = useCallback((delta) => {
    const newFreq = radioState.frequency + delta;
    onSetFrequency(Math.max(100000, Math.min(600000000, newFreq)));
  }, [radioState.frequency, onSetFrequency]);

  const handleBandChange = (bandName) => {
    const band = BANDS.find(b => b.name === bandName);
    if (band) onSetFrequency(band.min);
  };

  const handleFreqInput = (e) => {
    if (e.key === 'Enter') {
      const mhz = parseFloat(e.target.value);
      if (mhz > 0.1 && mhz < 600) {
        onSetFrequency(Math.round(mhz * 1e6));
      }
    }
  };

  return (
    <div style={{
      background: 'var(--bg-panel)', borderBottom: '1px solid var(--border)',
      padding: '8px 16px', display: 'flex', alignItems: 'center', gap: 16,
      userSelect: 'none', flexWrap: 'wrap',
    }}>
      {/* Frequency display and tuning */}
      <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
        <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center' }}>
          <button onClick={() => tuneStep(step)} style={tuneBtnStyle}>
            <ChevronUp size={16} />
          </button>
          <div style={{ fontSize: 10, color: 'var(--text-muted)', margin: '2px 0' }}>
            {step >= 1000 ? `${step / 1000}K` : `${step}Hz`}
          </div>
          <button onClick={() => tuneStep(-step)} style={tuneBtnStyle}>
            <ChevronDown size={16} />
          </button>
        </div>

        <div style={{ display: 'flex', flexDirection: 'column' }}>
          <div style={{ display: 'flex', alignItems: 'baseline', gap: 4 }}>
            <span style={{ fontSize: 10, color: 'var(--vfo-a)', fontWeight: 600 }}>VFO A</span>
            <div style={{ cursor: 'pointer' }}>
              {formatFrequency(radioState.frequency)}
            </div>
          </div>
          <input
            type="text"
            value={inputFreq}
            onChange={(e) => setInputFreq(e.target.value)}
            onKeyDown={handleFreqInput}
            onBlur={() => setInputFreq((radioState.frequency / 1e6).toFixed(6))}
            style={{
              background: 'var(--bg-control)', border: '1px solid var(--border)',
              color: 'var(--text-primary)', padding: '2px 8px', fontSize: 12,
              borderRadius: 3, width: 140, fontFamily: 'monospace', marginTop: 4,
            }}
            placeholder="MHz"
          />
        </div>

        {/* Step buttons */}
        <div style={{ display: 'flex', gap: 2 }}>
          {[10, 100, 1000, 10000, 100000].map(s => (
            <button
              key={s}
              onClick={() => setStep(s)}
              style={{
                padding: '3px 8px', fontSize: 10, border: '1px solid var(--border)',
                background: step === s ? 'var(--accent-dim)' : 'transparent',
                color: step === s ? '#fff' : 'var(--text-muted)',
                borderRadius: 3, cursor: 'pointer',
              }}
            >
              {s >= 1000 ? `${s / 1000}K` : `${s}Hz`}
            </button>
          ))}
        </div>
      </div>

      <div style={{ width: 1, height: 40, background: 'var(--border)' }} />

      {/* Mode selector */}
      <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
        <span style={{ fontSize: 10, color: 'var(--text-muted)' }}>MODE</span>
        <select
          value={radioState.mode}
          onChange={(e) => onSetMode(e.target.value)}
          style={{
            background: 'var(--bg-control)', border: '1px solid var(--border)',
            color: 'var(--text-primary)', padding: '4px 8px', fontSize: 12,
            borderRadius: 3, cursor: 'pointer', fontWeight: 600,
          }}
        >
          {MODES.map(m => <option key={m} value={m}>{m}</option>)}
        </select>
      </div>

      <div style={{ width: 1, height: 40, background: 'var(--border)' }} />

      {/* Band selector */}
      <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
        <span style={{ fontSize: 10, color: 'var(--text-muted)' }}>BAND</span>
        <div style={{ display: 'flex', gap: 1, flexWrap: 'wrap' }}>
          {BANDS.map(b => (
            <button
              key={b.name}
              onClick={() => handleBandChange(b.name)}
              style={{
                padding: '3px 7px', fontSize: 10, border: '1px solid var(--border)',
                background: radioState.frequency >= b.min && radioState.frequency <= b.max ? 'var(--accent-dim)' : 'transparent',
                color: radioState.frequency >= b.min && radioState.frequency <= b.max ? '#fff' : 'var(--text-muted)',
                borderRadius: 3, cursor: 'pointer',
              }}
            >
              {b.name}
            </button>
          ))}
        </div>
      </div>

      <div style={{ width: 1, height: 40, background: 'var(--border)' }} />

      {/* PTT Button */}
      <button
        onMouseDown={() => onPTT(true)}
        onMouseUp={() => onPTT(false)}
        onMouseLeave={() => onPTT(false)}
        style={{
          padding: '8px 24px', fontSize: 13, fontWeight: 700, letterSpacing: 1,
          border: 'none', borderRadius: 6,
          background: radioState.ptt ? 'var(--danger)' : 'var(--bg-control)',
          color: radioState.ptt ? '#fff' : 'var(--text-muted)',
          cursor: 'pointer', border: radioState.ptt ? 'none' : '1px solid var(--border)',
          transition: 'all 0.1s',
        }}
      >
        {radioState.ptt ? 'TX' : 'PTT'}
      </button>

      {/* Power */}
      <div style={{ display: 'flex', alignItems: 'center', gap: 8, marginLeft: 'auto' }}>
        <span style={{ fontSize: 10, color: 'var(--text-muted)' }}>PWR</span>
        <input
          type="range"
          min="0"
          max="100"
          value={radioState.power}
          style={{ width: 60, accentColor: 'var(--accent)' }}
        />
        <span style={{ fontSize: 11, color: 'var(--text-primary)', minWidth: 32 }}>{radioState.power}W</span>
      </div>
    </div>
  );
}

const tuneBtnStyle = {
  padding: 4, border: '1px solid var(--border)',
  background: 'var(--bg-control)', color: 'var(--text-muted)',
  borderRadius: 4, cursor: 'pointer', display: 'flex',
  alignItems: 'center', justifyContent: 'center',
};
