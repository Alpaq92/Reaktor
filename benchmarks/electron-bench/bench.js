/* The picture, in canvas 2D. Every constant here is read off
 * samples/bench/bench.c, including the integer divisions - the grid step and
 * the radius are truncated there, so they are truncated here too, or the boxes
 * land a pixel off and the two arms are not drawing the same thing.
 *
 * The grid comes off the real drawing area rather than a hardcoded 800x600,
 * the way bench.c takes win_w and win_h, and the size is printed so a window
 * that came out the wrong size says so instead of quietly drawing smaller. */
'use strict'

const params = new URLSearchParams(location.search)
const SECONDS = parseFloat(params.get('seconds') || '5')
const FPS = parseFloat(params.get('fps') || '0')
const BOXES = parseInt(params.get('boxes') || '64', 10)

const canvas = document.getElementById('c')
const ctx = canvas.getContext('2d', { alpha: false })

const W = window.innerWidth
const H = window.innerHeight

canvas.width = W
canvas.height = H

const STEP_X = Math.trunc(W / 8)
const HALF_X = Math.trunc(W / 16)
const STEP_Y = Math.trunc(H / 8)
const HALF_Y = Math.trunc(H / 16)
const R = Math.trunc(H / 24)

const fills = []
for (let i = 0; i < BOXES; i++)
    fills.push('rgb(' + (60 + (i * 3) % 190) + ',140,220)')

let t0 = 0
let due = 0
let frames = 0
let work_ms = 0

function draw(now) {
    if (!t0) { t0 = now; due = now }

    const t = (now - t0) / 1000
    const began = performance.now()

    ctx.fillStyle = '#f7f7f7'
    ctx.fillRect(0, 0, W, H)

    for (let i = 0; i < BOXES; i++) {
        const a = t * (1 + 0.05 * i)
        const cx = (i % 8) * STEP_X + HALF_X
        const cy = Math.trunc(i / 8) * STEP_Y + HALF_Y

        ctx.beginPath()
        for (let k = 0; k < 4; k++) {
            const ang = a + k * Math.PI / 2
            const x = cx + R * Math.cos(ang)
            const y = cy + R * Math.sin(ang)

            if (k === 0) ctx.moveTo(x, y)
            else         ctx.lineTo(x, y)
        }
        ctx.closePath()
        ctx.fillStyle = fills[i]
        ctx.fill()
    }

    work_ms += performance.now() - began
    frames++

    if (now - t0 >= SECONDS * 1000) { report(now - t0); return }
    schedule()
}

/* Held to a rate, the wait is a timer and the frame still lands on a vsync.
 * Free-running, requestAnimationFrame is the vsync and the rate is the
 * display's - Chromium has no way to be asked for more. */
function schedule() {
    if (FPS <= 0) { requestAnimationFrame(draw); return }

    const delay = (due += 1000 / FPS) - performance.now()

    if (delay > 0) window.setTimeout(function () { requestAnimationFrame(draw) }, delay)
    else { due = performance.now(); requestAnimationFrame(draw) }
}

/* ms_per_frame is the drawing this script is charged for and nothing else:
 * Chromium rasterizes and presents on other threads in other processes, and
 * none of that is visible from here. The comparable number for this arm is
 * the one the sampler in run.ps1 takes from outside. */
function report(elapsed_ms) {
    window.bench.report({
        size: W + 'x' + H,
        frames: String(frames),
        fps: (frames / (elapsed_ms / 1000)).toFixed(1),
        ms_per_frame: (work_ms / frames).toFixed(2),
        cpu_at_60fps: (100 * (work_ms / frames) / (1000 / 60)).toFixed(1)
    })
}

requestAnimationFrame(draw)
