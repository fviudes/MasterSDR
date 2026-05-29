const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('hamradio', {
  serial: {
    getPorts: () => ipcRenderer.invoke('get-serial-ports'),
  },
  radio: {
    connect: (config) => ipcRenderer.invoke('radio-connect', config),
    disconnect: () => ipcRenderer.invoke('radio-disconnect'),
    getState: () => ipcRenderer.invoke('radio-get-state'),
    setFrequency: (freqHz) => ipcRenderer.invoke('radio-set-frequency', freqHz),
    setMode: (mode) => ipcRenderer.invoke('radio-set-mode', mode),
    setPTT: (enable) => ipcRenderer.invoke('radio-ptt', enable),
    onState: (callback) => {
      ipcRenderer.on('radio-state', (event, data) => callback(data));
      return () => ipcRenderer.removeAllListeners('radio-state');
    },
    onSpectrumData: (callback) => {
      ipcRenderer.on('radio-spectrum-data', (event, data) => callback(data));
      return () => ipcRenderer.removeAllListeners('radio-spectrum-data');
    },
    onAudioData: (callback) => {
      ipcRenderer.on('radio-audio-data', (event, data) => callback(data));
      return () => ipcRenderer.removeAllListeners('radio-audio-data');
    },
  },
  audio: {
    start: () => ipcRenderer.invoke('audio-start'),
    stop: () => ipcRenderer.invoke('audio-stop'),
    getSpectrum: () => ipcRenderer.invoke('audio-get-spectrum'),
    getWaterfall: () => ipcRenderer.invoke('audio-get-waterfall'),
    onSpectrum: (callback) => {
      ipcRenderer.on('audio-spectrum', (event, data) => callback(data));
      return () => ipcRenderer.removeAllListeners('audio-spectrum');
    },
    onWaterfall: (callback) => {
      ipcRenderer.on('audio-waterfall', (event, data) => callback(data));
      return () => ipcRenderer.removeAllListeners('audio-waterfall');
    },
  },
  digital: {
    startFT8: () => ipcRenderer.invoke('digital-start-ft8'),
    startFT4: () => ipcRenderer.invoke('digital-start-ft4'),
    stop: () => ipcRenderer.invoke('digital-stop'),
    sendCQ: (data) => ipcRenderer.invoke('digital-send-cq', data),
    respond: (target, message) => ipcRenderer.invoke('digital-respond', { targetCallsign: target, message }),
    getBandActivity: () => ipcRenderer.invoke('digital-get-band-activity'),
    onFT8Decode: (callback) => {
      ipcRenderer.on('ft8-decode', (event, data) => callback(data));
      return () => ipcRenderer.removeAllListeners('ft8-decode');
    },
    onFT4Decode: (callback) => {
      ipcRenderer.on('ft4-decode', (event, data) => callback(data));
      return () => ipcRenderer.removeAllListeners('ft4-decode');
    },
  },
  logbook: {
    syncConfig: (config) => ipcRenderer.invoke('logbook-sync-config', config),
    uploadQSO: (qso) => ipcRenderer.invoke('logbook-upload-qso', qso),
    getQSOs: (params) => ipcRenderer.invoke('logbook-get-qsos', params),
    exportADIF: () => ipcRenderer.invoke('logbook-export-adif'),
    importADIF: () => ipcRenderer.invoke('logbook-import-adif'),
  },
  dialog: {
    saveFile: (options) => ipcRenderer.invoke('save-file-dialog', options),
  },
});
