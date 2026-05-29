import { useState, useEffect, useCallback } from 'react';
import WaterfallPanel from './components/WaterfallPanel';
import VFOControlBar from './components/VFOControlBar';
import RadioConnectionPanel from './components/RadioConnectionPanel';
import DigitalModesPanel from './components/DigitalModesPanel';
import LogbookPanel from './components/LogbookPanel';
import TopMenuBar from './components/TopMenuBar';
import StatusBar from './components/StatusBar';

export default function App() {
  const [radioState, setRadioState] = useState({
    connected: false,
    frequency: 14250000,
    mode: 'USB',
    ptt: false,
    band: '20m',
    power: 100,
    volume: 50,
    rfGain: 80,
    squelch: 0,
    antenna: 1,
    preamp: false,
    attenuator: false,
    noiseBlanker: false,
    agc: 'medium',
    filter: { low: 300, high: 2700 },
  });

  const [activeTab, setActiveTab] = useState('sdr');
  const [showConnectionPanel, setShowConnectionPanel] = useState(false);
  const [digitalMode, setDigitalMode] = useState(null);

  useEffect(() => {
    if (window.hamradio) {
      const unsubSpectrum = window.hamradio.audio.onSpectrum((data) => {
        setWaterfallData(prev => ({ ...prev, spectrum: data }));
      });

      const unsubWaterfall = window.hamradio.audio.onWaterfall((data) => {
        setWaterfallData(prev => ({ ...prev, waterfall: data }));
      });

      return () => { unsubSpectrum?.(); unsubWaterfall?.(); };
    }
  }, []);

  const [waterfallData, setWaterfallData] = useState({ spectrum: null, waterfall: null });

  const handleRadioConnect = useCallback(async (config) => {
    if (!window.hamradio) return;
    const state = await window.hamradio.radio.connect(config);
    setRadioState(prev => ({ ...prev, ...state, connected: true }));
    setShowConnectionPanel(false);
  }, []);

  const handleRadioDisconnect = useCallback(async () => {
    if (!window.hamradio) return;
    await window.hamradio.radio.disconnect();
    setRadioState(prev => ({ ...prev, connected: false }));
  }, []);

  const handleSetFrequency = useCallback(async (freqHz) => {
    if (!window.hamradio) return;
    const state = await window.hamradio.radio.setFrequency(freqHz);
    setRadioState(prev => ({ ...prev, ...state }));
  }, []);

  const handleSetMode = useCallback(async (mode) => {
    if (!window.hamradio) return;
    const state = await window.hamradio.radio.setMode(mode);
    setRadioState(prev => ({ ...prev, ...state }));
  }, []);

  const handlePTT = useCallback(async (enable) => {
    if (!window.hamradio) return;
    const state = await window.hamradio.radio.setPTT(enable);
    setRadioState(prev => ({ ...prev, ...state }));
  }, []);

  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100vh' }}>
      <TopMenuBar
        radioState={radioState}
        onOpenConnection={() => setShowConnectionPanel(true)}
        onDisconnect={handleRadioDisconnect}
        activeTab={activeTab}
        onTabChange={setActiveTab}
        digitalMode={digitalMode}
      />

      <VFOControlBar
        radioState={radioState}
        onSetFrequency={handleSetFrequency}
        onSetMode={handleSetMode}
        onPTT={handlePTT}
      />

      <div style={{ flex: 1, display: 'flex', overflow: 'hidden' }}>
        {activeTab === 'sdr' && (
          <WaterfallPanel
            radioState={radioState}
            waterfallData={waterfallData}
          />
        )}

        {activeTab === 'digital' && (
          <DigitalModesPanel
            radioState={radioState}
            digitalMode={digitalMode}
            onModeChange={setDigitalMode}
          />
        )}

        {activeTab === 'logbook' && (
          <LogbookPanel radioState={radioState} />
        )}
      </div>

      <StatusBar radioState={radioState} />

      {showConnectionPanel && (
        <RadioConnectionPanel
          onConnect={handleRadioConnect}
          onClose={() => setShowConnectionPanel(false)}
        />
      )}
    </div>
  );
}
