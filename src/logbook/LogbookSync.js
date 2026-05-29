const axios = require('axios');
const fs = require('fs');

class LogbookSync {
  constructor() {
    this.config = {
      apiUrl: 'https://localhost:3000/api',
      apiKey: '',
      syncInterval: 300000, // 5 minutes
    };
    this.localLogbook = [];
    this.syncTimer = null;
  }

  async configure(config) {
    this.config = { ...this.config, ...config };

    if (this.config.apiKey) {
      try {
        await axios.get(`${this.config.apiUrl}/health`);
        this.startAutoSync();
        return { success: true };
      } catch {
        return { success: false, error: 'Não foi possível conectar ao servidor' };
      }
    }
    return { success: true };
  }

  startAutoSync() {
    if (this.syncTimer) clearInterval(this.syncTimer);
    this.syncTimer = setInterval(() => {
      this.syncPendingQSOs();
    }, this.config.syncInterval);
  }

  async uploadQSO(qso) {
    try {
      const response = await axios.post(
        `${this.config.apiUrl}/logbook`,
        qso,
        { headers: { 'X-API-Key': this.config.apiKey } }
      );
      return { success: true, qso: response.data.qso };
    } catch (error) {
      this.localLogbook.push({ ...qso, _syncPending: true, _timestamp: Date.now() });
      return { success: false, error: error.message, queued: true };
    }
  }

  async syncPendingQSOs() {
    const pending = this.localLogbook.filter(q => q._syncPending);
    if (pending.length === 0) return { synced: 0 };

    let synced = 0;
    for (const qso of pending) {
      try {
        await axios.post(
          `${this.config.apiUrl}/logbook`,
          { ...qso, _syncPending: undefined, _timestamp: undefined },
          { headers: { 'X-API-Key': this.config.apiKey } }
        );
        qso._syncPending = false;
        synced++;
      } catch {
        break;
      }
    }

    this.localLogbook = this.localLogbook.filter(q => !q._syncPending || q === pending[pending.length - 1]);
    return { synced };
  }

  async getQSOs(params = {}) {
    try {
      const response = await axios.get(`${this.config.apiUrl}/logbook`, {
        params,
        headers: { 'X-API-Key': this.config.apiKey },
      });
      return { success: true, ...response.data };
    } catch (error) {
      return { success: false, error: error.message, qsos: this.localLogbook };
    }
  }

  async exportADIF() {
    try {
      const response = await axios.get(`${this.config.apiUrl}/logbook/export`, {
        headers: { 'X-API-Key': this.config.apiKey },
        responseType: 'text',
      });
      return { success: true, adif: response.data };
    } catch {
      return { success: false, adif: this.generateLocalADIF() };
    }
  }

  async importADIF(filePath) {
    try {
      const content = fs.readFileSync(filePath, 'utf-8');
      const formData = new FormData();
      formData.append('file', new Blob([content]), 'import.adif');

      const response = await axios.post(`${this.config.apiUrl}/logbook/import`, formData, {
        headers: {
          'X-API-Key': this.config.apiKey,
          'Content-Type': 'multipart/form-data',
        },
      });
      return { success: true, imported: response.data.imported };
    } catch (error) {
      return { success: false, error: error.message, imported: 0 };
    }
  }

  generateLocalADIF() {
    let adif = 'ADIF Export from HamRadio Desktop\n<adif_ver:5>3.1.4\n<programid:17>HamRadioDesktop\n<eoh>\n';
    for (const qso of this.localLogbook) {
      adif += `<call:${qso.callsign.length}>${qso.callsign} `;
      adif += `<band:${(qso.band || '').length}>${qso.band || ''} `;
      adif += `<mode:${(qso.mode || '').length}>${qso.mode || ''} `;
      adif += `<qso_date:8>${qso.qso_date || ''} `;
      adif += `<time_on:4>${(qso.time_on || '').replace(/:/g, '')} `;
      if (qso.rst_sent) adif += `<rst_sent:${qso.rst_sent.length}>${qso.rst_sent} `;
      if (qso.rst_rcvd) adif += `<rst_rcvd:${qso.rst_rcvd.length}>${qso.rst_rcvd} `;
      adif += '<eor>\n';
    }
    return adif;
  }
}

module.exports = { LogbookSync };
