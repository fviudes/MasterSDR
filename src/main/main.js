const { app, BrowserWindow, ipcMain, dialog } = require('electron');
const path = require('path');
const { initSerialPorts, getSerialPorts } = require('./radio/SerialManager');
const { RadioController } = require('./radio/RadioController');
const { AudioProcessor } = require('./dsp/AudioProcessor');
const { DigitalModes } = require('./digital/DigitalModes');
const { LogbookSync } = require('./logbook/LogbookSync');

let mainWindow;
let radioController;
let audioProcessor;
let digitalModes;
let logbookSync;

function createWindow() {
  mainWindow = new BrowserWindow({
    width: 1400,
    height: 900,
    minWidth: 1024,
    minHeight: 700,
    title: 'HamRadio SDR Client',
    backgroundColor: '#0f172a',
    webPreferences: {
      preload: path.join(__dirname, 'preload.js'),
      contextIsolation: true,
      nodeIntegration: false,
    },
  });

  if (process.env.NODE_ENV === 'development') {
    mainWindow.loadURL('http://localhost:5173');
    mainWindow.webContents.openDevTools();
  } else {
    mainWindow.loadFile(path.join(__dirname, '../dist/index.html'));
  }

  mainWindow.on('closed', () => { mainWindow = null; });
}

app.whenReady().then(async () => {
  radioController = new RadioController();
  audioProcessor = new AudioProcessor();
  digitalModes = new DigitalModes(audioProcessor);
  logbookSync = new LogbookSync();

  radioController.on('spectrum-data', (data) => {
    mainWindow?.webContents.send('radio-spectrum-data', data);
  });

  radioController.on('audio-data', (data) => {
    mainWindow?.webContents.send('radio-audio-data', data);
  });

  radioController.on('state', (state) => {
    mainWindow?.webContents.send('radio-state', state);
  });

  createWindow();
  setupIPC();

  await initSerialPorts();
});

app.on('window-all-closed', () => {
  if (radioController) radioController.disconnect();
  if (audioProcessor) audioProcessor.stop();
  app.quit();
});

app.on('activate', () => {
  if (BrowserWindow.getAllWindows().length === 0) createWindow();
});

function setupIPC() {
  ipcMain.handle('get-serial-ports', async () => {
    return await getSerialPorts();
  });

  ipcMain.handle('radio-connect', async (event, config) => {
    return await radioController.connect(config);
  });

  ipcMain.handle('radio-disconnect', async () => {
    return await radioController.disconnect();
  });

  ipcMain.handle('radio-get-state', async () => {
    return radioController.getState();
  });

  ipcMain.handle('radio-set-frequency', async (event, freqHz) => {
    return await radioController.setFrequency(freqHz);
  });

  ipcMain.handle('radio-set-mode', async (event, mode) => {
    return await radioController.setMode(mode);
  });

  ipcMain.handle('radio-ptt', async (event, enable) => {
    return await radioController.setPTT(enable);
  });

  ipcMain.handle('audio-start', async () => {
    return await audioProcessor.start();
  });

  ipcMain.handle('audio-stop', async () => {
    return await audioProcessor.stop();
  });

  ipcMain.handle('audio-get-spectrum', async () => {
    return audioProcessor.getSpectrumData();
  });

  ipcMain.handle('audio-get-waterfall', async () => {
    return audioProcessor.getWaterfallData();
  });

  audioProcessor.on('spectrum', (data) => {
    mainWindow?.webContents.send('audio-spectrum', data);
  });

  audioProcessor.on('waterfall', (data) => {
    mainWindow?.webContents.send('audio-waterfall', data);
  });

  digitalModes.on('ft8-decode', (data) => {
    mainWindow?.webContents.send('ft8-decode', data);
  });

  digitalModes.on('ft4-decode', (data) => {
    mainWindow?.webContents.send('ft4-decode', data);
  });

  ipcMain.handle('digital-start-ft8', async () => {
    return await digitalModes.startFT8();
  });

  ipcMain.handle('digital-start-ft4', async () => {
    return await digitalModes.startFT4();
  });

  ipcMain.handle('digital-stop', async () => {
    return await digitalModes.stop();
  });

  ipcMain.handle('digital-send-cq', async (event, { message, frequency }) => {
    return await digitalModes.transmitCQ(message, frequency);
  });

  ipcMain.handle('digital-respond', async (event, { targetCallsign, message }) => {
    return await digitalModes.respond(targetCallsign, message);
  });

  ipcMain.handle('digital-get-band-activity', async () => {
    return digitalModes.getBandActivity();
  });

  ipcMain.handle('logbook-sync-config', async (event, config) => {
    return await logbookSync.configure(config);
  });

  ipcMain.handle('logbook-upload-qso', async (event, qso) => {
    return await logbookSync.uploadQSO(qso);
  });

  ipcMain.handle('logbook-get-qsos', async (event, params) => {
    return await logbookSync.getQSOs(params);
  });

  ipcMain.handle('logbook-export-adif', async () => {
    return await logbookSync.exportADIF();
  });

  ipcMain.handle('logbook-import-adif', async () => {
    const result = await dialog.showOpenDialog(mainWindow, {
      filters: [{ name: 'ADIF Files', extensions: ['adi', 'adif'] }],
      properties: ['openFile'],
    });
    if (!result.canceled && result.filePaths.length > 0) {
      return await logbookSync.importADIF(result.filePaths[0]);
    }
    return { imported: 0 };
  });

  ipcMain.handle('save-file-dialog', async (event, options) => {
    return await dialog.showSaveDialog(mainWindow, options);
  });
}
