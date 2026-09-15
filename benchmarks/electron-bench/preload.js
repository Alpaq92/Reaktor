'use strict'

const { contextBridge, ipcRenderer } = require('electron')

contextBridge.exposeInMainWorld('bench', {
    report: function (result) { ipcRenderer.send('bench-result', result) }
})
