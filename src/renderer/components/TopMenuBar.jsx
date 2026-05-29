import { Radio, Wifi, WifiOff, Settings, RadioTower } from 'lucide-react';

export default function TopMenuBar({
  radioState, onOpenConnection, onDisconnect,
  activeTab, onTabChange, digitalMode,
}) {
  const tabs = [
    { id: 'sdr', label: 'SDR', icon: Radio },
    { id: 'digital', label: 'Digital', icon: RadioTower },
    { id: 'logbook', label: 'Logbook', icon: 'logbook' },
  ];

  return (
    <div style={{
      display: 'flex', alignItems: 'center', justifyContent: 'space-between',
      background: 'var(--bg-panel)', borderBottom: '1px solid var(--border)',
      padding: '0 12px', height: 40, userSelect: 'none',
    }}>
      <div style={{ display: 'flex', alignItems: 'center', gap: 16 }}>
        <span style={{ fontWeight: 700, fontSize: 14, color: 'var(--accent)', letterSpacing: 1 }}>
          HAMRADIO SDR
        </span>

        <div style={{ display: 'flex', gap: 2 }}>
          {tabs.map(tab => {
            const Icon = tab.icon === 'logbook' ? null : tab.icon;
            return (
              <button
                key={tab.id}
                onClick={() => onTabChange(tab.id)}
                style={{
                  padding: '4px 14px',
                  fontSize: 12,
                  fontWeight: activeTab === tab.id ? 600 : 400,
                  border: 'none',
                  background: activeTab === tab.id ? 'var(--accent-dim)' : 'transparent',
                  color: activeTab === tab.id ? '#fff' : 'var(--text-muted)',
                  borderRadius: 4,
                  cursor: 'pointer',
                  display: 'flex', alignItems: 'center', gap: 5,
                }}
              >
                {Icon && <Icon size={14} />}
                {tab.label}
              </button>
            );
          })}
        </div>
      </div>

      <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
        {radioState.connected ? (
          <>
            <span style={{ fontSize: 11, color: 'var(--success)' }}>Conectado</span>
            <button
              onClick={onDisconnect}
              style={{
                padding: '4px 10px', fontSize: 11, border: '1px solid var(--danger)',
                background: 'transparent', color: 'var(--danger)', borderRadius: 4,
                cursor: 'pointer', display: 'flex', alignItems: 'center', gap: 4,
              }}
            >
              <WifiOff size={12} /> Desconectar
            </button>
          </>
        ) : (
          <button
            onClick={onOpenConnection}
            style={{
              padding: '4px 10px', fontSize: 11, border: '1px solid var(--accent)',
              background: 'transparent', color: 'var(--accent)', borderRadius: 4,
              cursor: 'pointer', display: 'flex', alignItems: 'center', gap: 4,
            }}
          >
            <Wifi size={12} /> Conectar Rádio
          </button>
        )}

        {digitalMode && (
          <span style={{
            padding: '2px 8px', fontSize: 10, fontWeight: 600,
            background: 'var(--accent-dim)', color: '#fff', borderRadius: 3,
          }}>
            {digitalMode}
          </span>
        )}

        <button style={{
          padding: 4, border: 'none', background: 'transparent', color: 'var(--text-muted)',
          cursor: 'pointer', display: 'flex',
        }}>
          <Settings size={16} />
        </button>
      </div>
    </div>
  );
}
