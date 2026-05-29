import { useState, useEffect } from 'react';
import { X, Wifi, Loader2 } from 'lucide-react';

const RADIO_TYPES = [
  { id: 'hermes_lite_2', label: 'Hermes Lite 2', icon: 'sdr' },
  { id: 'anan', label: 'ANAN SDR', icon: 'sdr' },
  { id: 'flexradio', label: 'FlexRadio', icon: 'flex' },
  { id: 'icom', label: 'Icom (CI-V)', icon: 'icom' },
  { id: 'icom_ip', label: 'Icom (IP/LAN)', icon: 'icom' },
  { id: 'yaesu', label: 'Yaesu (CAT)', icon: 'yaesu' },
  { id: 'kenwood', label: 'Kenwood (CAT)', icon: 'kenwood' },
];

const ICOM_MODELS = ['IC-7300', 'IC-7610', 'IC-9700', 'IC-705', 'IC-7100', 'IC-7851'];
const YAESU_MODELS = ['FT-991A', 'FT-710', 'FT-DX10', 'FT-DX101', 'FT-891', 'FT-857'];
const KENWOOD_MODELS = ['TS-590', 'TS-890', 'TS-990', 'TS-480'];

export default function RadioConnectionPanel({ onConnect, onClose }) {
  const [radioType, setRadioType] = useState('hermes_lite_2');
  const [ports, setPorts] = useState([]);
  const [config, setConfig] = useState({
    type: 'hermes_lite_2',
    ip: '192.168.10.3',
    port: '',
    baudRate: 19200,
    model: 'IC-7300',
  });
  const [connecting, setConnecting] = useState(false);
  const [error, setError] = useState('');

  useEffect(() => {
    if (window.hamradio) {
      window.hamradio.serial.getPorts().then(setPorts).catch(() => {});
    }
  }, []);

  const handleConnect = async () => {
    setConnecting(true);
    setError('');
    try {
      await onConnect(config);
    } catch (err) {
      setError(err.message || 'Erro ao conectar');
    } finally {
      setConnecting(false);
    }
  };

  const needsIP = ['hermes_lite_2', 'anan', 'flexradio', 'icom_ip'].includes(radioType);
  const needsSerial = ['icom', 'yaesu', 'kenwood'].includes(radioType);
  const needsModel = ['icom', 'icom_ip', 'yaesu', 'kenwood'].includes(radioType);
  const isIcomIp = radioType === 'icom_ip';

  const models = (radioType === 'icom' || radioType === 'icom_ip') ? ICOM_MODELS :
    radioType === 'yaesu' ? YAESU_MODELS :
    radioType === 'kenwood' ? KENWOOD_MODELS : [];

  return (
    <div style={{
      position: 'fixed', inset: 0, background: 'rgba(0,0,0,0.7)',
      display: 'flex', alignItems: 'center', justifyContent: 'center',
      zIndex: 100,
    }} onClick={onClose}>
      <div
        onClick={(e) => e.stopPropagation()}
        style={{
          background: 'var(--bg-panel)', border: '1px solid var(--border)',
          borderRadius: 12, padding: 24, width: 480, maxHeight: '80vh',
          overflow: 'auto',
        }}
      >
        <div style={{
          display: 'flex', justifyContent: 'space-between', alignItems: 'center',
          marginBottom: 20,
        }}>
          <h2 style={{ fontSize: 16, color: '#fff', margin: 0 }}>Conectar Rádio</h2>
          <button onClick={onClose} style={{
            background: 'none', border: 'none', color: 'var(--text-muted)',
            cursor: 'pointer', padding: 4,
          }}>
            <X size={20} />
          </button>
        </div>

        {/* Radio type selection */}
        <div style={{ marginBottom: 20 }}>
          <label style={{ fontSize: 11, color: 'var(--text-muted)', marginBottom: 6, display: 'block' }}>
            TIPO DE RÁDIO
          </label>
          <div style={{ display: 'grid', gridTemplateColumns: 'repeat(3, 1fr)', gap: 6 }}>
            {RADIO_TYPES.map(r => (
              <button
                key={r.id}
                onClick={() => {
                  setRadioType(r.id);
                  setConfig(c => ({ ...c, type: r.id }));
                  setError('');
                }}
                style={{
                  padding: '10px 8px', fontSize: 11, border: '1px solid var(--border)',
                  background: radioType === r.id ? 'var(--accent-dim)' : 'var(--bg-control)',
                  color: radioType === r.id ? '#fff' : 'var(--text-muted)',
                  borderRadius: 6, cursor: 'pointer', textAlign: 'center',
                  fontWeight: radioType === r.id ? 600 : 400,
                }}
              >
                {r.label}
              </button>
            ))}
          </div>
        </div>

        {/* IP Address for SDR radios */}
        {needsIP && (
          <div style={{ marginBottom: 16 }}>
            <label style={{ fontSize: 11, color: 'var(--text-muted)', marginBottom: 4, display: 'block' }}>
              ENDEREÇO IP
            </label>
            <input
              type="text"
              value={config.ip}
              onChange={(e) => setConfig(c => ({ ...c, ip: e.target.value }))}
              placeholder="192.168.1.100"
              style={{
                width: '100%', padding: '8px 12px', background: 'var(--bg-control)',
                border: '1px solid var(--border)', color: 'var(--text-primary)',
                borderRadius: 6, fontSize: 13, fontFamily: 'monospace',
              }}
            />
          </div>
        )}

        {/* Icom IP UDP ports (fixed) and model */}
        {isIcomIp && (
          <>
            <div style={{ marginBottom: 12 }}>
              <label style={{ fontSize: 11, color: 'var(--text-muted)', marginBottom: 4, display: 'block' }}>
                PORTAS UDP (FIXAS)
              </label>
              <div style={{ display: 'grid', gridTemplateColumns: 'repeat(3, 1fr)', gap: 6 }}>
                <div style={{
                  padding: '8px', background: 'var(--bg-control)', border: '1px solid var(--border)',
                  borderRadius: 6, textAlign: 'center',
                }}>
                  <div style={{ fontSize: 10, color: 'var(--text-muted)' }}>Control</div>
                  <div style={{ fontSize: 13, color: 'var(--text-primary)', fontFamily: 'monospace' }}>50001</div>
                </div>
                <div style={{
                  padding: '8px', background: 'var(--bg-control)', border: '1px solid var(--border)',
                  borderRadius: 6, textAlign: 'center',
                }}>
                  <div style={{ fontSize: 10, color: 'var(--text-muted)' }}>Serial</div>
                  <div style={{ fontSize: 13, color: 'var(--text-primary)', fontFamily: 'monospace' }}>50002</div>
                </div>
                <div style={{
                  padding: '8px', background: 'var(--bg-control)', border: '1px solid var(--border)',
                  borderRadius: 6, textAlign: 'center',
                }}>
                  <div style={{ fontSize: 10, color: 'var(--text-muted)' }}>Audio</div>
                  <div style={{ fontSize: 13, color: 'var(--text-primary)', fontFamily: 'monospace' }}>50003</div>
                </div>
              </div>
            </div>

            <div style={{ marginBottom: 12 }}>
              <label style={{ fontSize: 11, color: 'var(--text-muted)', marginBottom: 4, display: 'block' }}>
                MODELO
              </label>
              <select
                value={config.model}
                onChange={(e) => setConfig(c => ({ ...c, model: e.target.value }))}
                style={{
                  width: '100%', padding: '8px 12px', background: 'var(--bg-control)',
                  border: '1px solid var(--border)', color: 'var(--text-primary)',
                  borderRadius: 6, fontSize: 13,
                }}
              >
                {models.map(m => <option key={m} value={m}>{m}</option>)}
              </select>
            </div>
          </>
        )}

        {/* Serial port for traditional radios */}
        {needsSerial && (
          <>
            <div style={{ marginBottom: 12 }}>
              <label style={{ fontSize: 11, color: 'var(--text-muted)', marginBottom: 4, display: 'block' }}>
                PORTA SERIAL
              </label>
              <select
                value={config.port}
                onChange={(e) => setConfig(c => ({ ...c, port: e.target.value }))}
                style={{
                  width: '100%', padding: '8px 12px', background: 'var(--bg-control)',
                  border: '1px solid var(--border)', color: 'var(--text-primary)',
                  borderRadius: 6, fontSize: 13,
                }}
              >
                <option value="">Selecionar porta...</option>
                {ports.map(p => (
                  <option key={p.path} value={p.path}>
                    {p.path} {p.manufacturer ? `(${p.manufacturer})` : ''}
                  </option>
                ))}
              </select>
            </div>

            <div style={{ marginBottom: 12 }}>
              <label style={{ fontSize: 11, color: 'var(--text-muted)', marginBottom: 4, display: 'block' }}>
                BAUD RATE
              </label>
              <select
                value={config.baudRate}
                onChange={(e) => setConfig(c => ({ ...c, baudRate: parseInt(e.target.value) }))}
                style={{
                  width: '100%', padding: '8px 12px', background: 'var(--bg-control)',
                  border: '1px solid var(--border)', color: 'var(--text-primary)',
                  borderRadius: 6, fontSize: 13,
                }}
              >
                {[4800, 9600, 19200, 38400, 57600, 115200].map(b => (
                  <option key={b} value={b}>{b} bps</option>
                ))}
              </select>
            </div>

            <div style={{ marginBottom: 12 }}>
              <label style={{ fontSize: 11, color: 'var(--text-muted)', marginBottom: 4, display: 'block' }}>
                MODELO
              </label>
              <select
                value={config.model}
                onChange={(e) => setConfig(c => ({ ...c, model: e.target.value }))}
                style={{
                  width: '100%', padding: '8px 12px', background: 'var(--bg-control)',
                  border: '1px solid var(--border)', color: 'var(--text-primary)',
                  borderRadius: 6, fontSize: 13,
                }}
              >
                {models.map(m => <option key={m} value={m}>{m}</option>)}
              </select>
            </div>
          </>
        )}

        {error && (
          <div style={{
            padding: '8px 12px', background: 'rgba(224,85,85,0.1)',
            border: '1px solid rgba(224,85,85,0.3)', borderRadius: 6,
            color: 'var(--danger)', fontSize: 12, marginBottom: 12,
          }}>
            {error}
          </div>
        )}

        <button
          onClick={handleConnect}
          disabled={connecting}
          style={{
            width: '100%', padding: '10px', fontSize: 14, fontWeight: 600,
            border: 'none', borderRadius: 8,
            background: connecting ? 'var(--bg-control)' : 'var(--accent)',
            color: '#fff', cursor: connecting ? 'wait' : 'pointer',
            display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 8,
          }}
        >
          {connecting ? (
            <><Loader2 size={16} className="spin" /> Conectando...</>
          ) : (
            <><Wifi size={16} /> Conectar</>
          )}
        </button>
      </div>
    </div>
  );
}
