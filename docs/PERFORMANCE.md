# Performance

Every number below was measured, not estimated. Where a figure is an
expectation rather than a measurement it says so.

**Measured on:** Windows 11 Pro (26200), x64, MSVC Release, Direct3D 11, one
display at 1.00x scale, 960x680 window. That is the only platform these
figures come from — see [System requirements](#system-requirements) for what
is and is not known about the others.

Reproduce any of it from the **Diagnostics** tab, which reports the renderer,
the frame breakdown, the memory and the startup cost of each step.

## Size

| Artefact | Bytes |
| --- | --- |
| `curie.exe` | 2,686,464 |
| `curie.wasm` | 1,194,540 |
| `curie.js` | 76,222 |
| `curie.data` | 36,215 |
| `curie.html` | 1,019 |
| **Web bundle, total** | **1,307,996** |

The binary links SDL, libcss, yutil, plutosvg and plutovg statically and
compiles Nuklear in, so nothing but the C runtime is loaded from outside it.
Trimming SDL to the subsystems this app uses — no audio, joystick, haptic,
HID, sensor, camera, power, GPU, tray or offscreen — and dropping the render
backends it can never select took it from 3.39 MB to its present size.

The web bundle was 3.55 MB before two changes: `-Oz` with `--closure 1`, and
packaging only the asset files the sources actually name rather than the
directories they live in. `curie.data` fell from 1,868,661 bytes to 36,215 on
its own.

## Memory

Steady state after startup, on the Login tab:

| Counter | Value |
| --- | --- |
| Private bytes | 8.4 MB |
| Working set | 23–30 MB |

**Private bytes vary by about 1.8 MB between runs** — 8.8, 9.7 and 10.6 MB
across three consecutive launches of the same binary — because the Direct3D 11
driver's own heap does. Any optimisation worth less than roughly a megabyte
cannot be distinguished from that noise, which is the honest ceiling on
further tuning. Working set is much larger than private bytes because it
counts shared driver and system pages that are not this process's to give
back.

What the application itself allocates:

| Allocation | Size |
| --- | --- |
| Font atlas (1024x128 RGBA32) | 0.5 MB |
| Icon cache | 19 KB over 4 rasters; ~80 KB after the Display tab |
| Nuklear command buffer | 4 KB, growing to ~16 KB on the densest page |
| Application state | ~30 KB |

That is under 0.6 MB of a roughly 9 MB process. The atlas is the only item
with meaningful room left in it — see the ALPHA8 note in
[DEVELOPMENT.md](DEVELOPMENT.md#fonts-and-the-atlas).

### What startup costs

Resident set sampled at each step, and the difference each one made:

| Step | Delta | Running total |
| --- | --- | --- |
| Before `main` — CRT, loader, the image itself | — | 7.4 MB |
| `SDL_Init(SDL_INIT_VIDEO)` | +3.7 MB | 11.1 MB |
| Window and renderer | +10.4 MB | 21.5 MB |
| Window icon — the first touch of plutosvg | +0.2 MB | 21.7 MB |
| Nuklear context | +0.0 MB | 21.8 MB |
| Font atlas | +1.5 MB | 23.2 MB |
| Stylesheets | +0.4 MB | 23.6 MB |

The window-and-renderer step is the graphics driver, and most of it is shared
pages rather than this process's own: disabling the render backends the app
never selects moved private bytes by 0.9 MB while barely moving working set at
all.

### Growth over time

Memory is flat in use, and is checked that way rather than assumed. Eighty
scheme switches followed by six sweeps through all seven tabs:

```
start                  11.20 MB private
after 20 theme loads   10.44
after 40                9.55
after 60                9.52
after 80                9.52
after 6 tab sweeps      9.52
```

This is worth re-running after any change to the style or icon paths. Before
the rule store was rebuilt per load, the same test climbed from 7.91 to
15.50 MB and had not levelled off.

## CPU

| Situation | Cost |
| --- | --- |
| At rest, any tab | **0.00%** of one core over 8 s |
| Pointer sweeping across the Login page | ~2.8% of one core, median |

At rest the figure is not "low", it is nothing: SDL is told to wait for
events, and `SDL_AppIterate` returns immediately unless something has actually
changed. A frame is built when a widget changes state or the pointer crosses a
region registered as hot in the previous frame.

Where a frame's time goes, on a typical drawn frame:

| Phase | Time |
| --- | --- |
| Build (running the UI code) | 1.3–1.9 ms |
| Render (Nuklear commands to geometry) | 0.4–0.9 ms |
| Present | 1.0–13 ms |

Present is the vsync wait and dominates, which is the intended shape. While a
mouse button is held, the callback rate is set to the display's refresh rather
than uncapped: presenting faster than the display accepts fills the swapchain
and then `SDL_RenderPresent` blocks on it — measured at 29 ms a frame against
0.11 ms of building, which held a drag at 35 fps.

Parsing both stylesheets takes 1.5–4.0 ms, and happens at startup and on each
scheme change.

## GPU

Modest by construction. The application holds one atlas texture plus one
texture per rasterised icon, uploads nothing per frame, and submits a few
hundred triangles — Nuklear's geometry for the visible widgets. Anti-aliasing
is Nuklear's own, done on the CPU during the render phase, and
`CURIE_AA=0` turns it off.

There is no shader, no framebuffer of its own and no compute: everything goes
through `SDL_Renderer`, so the graphics API is whichever one SDL picked.

## System requirements

**Windows** is the only platform in this table with measured numbers. The rest
follow from the build configuration and SDL's own requirements, and are marked
as expectations until somebody runs them.

| Platform | Renderer | Status |
| --- | --- | --- |
| Windows 10/11, x64 | Direct3D 11, or OpenGL if D3D is unavailable | **Measured.** Everything above. |
| macOS | Metal | *Expected.* Builds with CMake; no measurements. |
| Linux, x64/ARM | OpenGL or OpenGL ES, X11 or Wayland | *Expected.* Same. |
| FreeBSD and the other BSDs | OpenGL, X11 or Wayland | *Expected.* SDL supports them; not built here. |
| Browser | WebGL 2 (required — the build sets both the minimum and maximum) | **Measured** for size only; not profiled. |

**Filling that table in is planned work.** The macOS, Linux and BSD rows stay
marked as expectations until somebody builds on each and runs the same
measurements the Windows row came from — the Diagnostics tab reports
everything needed, so it is a matter of access to the machines, not of
tooling. The browser row needs a profiling pass rather than just a size
check.

In every case the requirement is modest: this is a 2.7 MB binary that draws a
few hundred triangles and idles at zero. On the desktop it needs about 10 MB
of RAM and whatever the platform's own graphics driver takes on top. A machine
that can run a browser can run it.

Two things a deployment does need:

- **The asset files.** A native build reads tiny.css and the Ionicons SVGs
  from disk at runtime, resolved relative to the `.curie-root` marker, so
  those files have to ship beside the binary or `CURIE_ROOT` has to point at
  them. The web build is the exception: Emscripten packages them into
  `curie.data`.
- **A served web build.** A `file://` page cannot fetch the `.wasm`.
