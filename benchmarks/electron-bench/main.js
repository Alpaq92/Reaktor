/* The rotating boxes of samples/bench/bench.c, drawn by Electron.
 *
 * The window is the content area, not the frame: 800x600 of canvas, the same
 * picture the other four arms draw. The renderer does the drawing and hands
 * its counters back over IPC; this process prints them and exits, so the
 * stdout block matches what bench.exe prints. */
'use strict'

const { app, BrowserWindow, ipcMain } = require('electron')
const path = require('path')

const argv = process.argv.slice(1)

function flag(name, fallback) {
    const i = argv.indexOf(name)
    return i >= 0 && i + 1 < argv.length ? argv[i + 1] : fallback
}

const seconds = flag('--bench-seconds', '5')
const fps = flag('--fps', '0')
const boxes = flag('--boxes', '64')

/* Reaktor's default renderer is the CPU one. Chromium's is not, and this is
 * the only switch that brings the two arms onto the same rasterizer. */
if (argv.indexOf('--software') >= 0) app.disableHardwareAcceleration()

app.whenReady().then(() => {
    const win = new BrowserWindow({
        width: 800,
        height: 600,
        useContentSize: true,
        resizable: false,
        backgroundColor: '#f7f7f7',
        autoHideMenuBar: true,
        webPreferences: {
            preload: path.join(__dirname, 'preload.js'),
            /* Off, or Chromium quietly stops painting an unfocused window and
             * the run measures a sleeping process. */
            backgroundThrottling: false
        }
    })

    win.setMenuBarVisibility(false)
    win.loadFile(path.join(__dirname, 'index.html'),
                 { query: { seconds: seconds, fps: fps, boxes: boxes } })
})

ipcMain.on('bench-result', (event, result) => {
    for (const key of Object.keys(result))
        process.stdout.write(key.padEnd(14) + result[key] + '\n')
    app.exit(0)
})

app.on('window-all-closed', () => app.exit(0))
