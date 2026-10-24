# Performance

**By default, every pixel is drawn by the CPU.** Nuklear turns the widgets into
shapes and SDL's software rasterizer fills them in; no GPU is required
anywhere, for the reasons in [the renderer](DOCUMENTATION.md#the-renderer).
`--renderer` hands the drawing to the GPU instead, through Direct3D, OpenGL or
Metal; [Against the others](#against-the-others) measures both.

**Nothing is drawn until something changes.** The app sleeps on the event
queue and builds a frame only when the picture would differ. So **CPU is a
count of frames, not a load**, and **memory is a floor, not a curve**: once the
atlas and icons are baked, nothing grows.

On Windows today, `showcase.exe` is 3,232,256 bytes, `notepad.exe` 2,280,960,
`simple.exe` 2,160,640 and `bench.exe` 2,150,912. The showcase links all three
optional modules and the others none; each carries the default look —
Aileron, the mark, the sheets and seven icons, 73,123 bytes — compiled in. The
four-platform figures below come from an older build that shipped one
executable, at 1.00× scale in a 960×680 window; the runtime they measure is
the one all four programs share.

## Against the others

![Two bar charts of drawing 64 rotating boxes at 60 fps in an 800x600 window.
Memory: Reaktor 8.9 MB, the same binary on D3D12 120.5 MB and on OpenGL
69.5 MB, Flutter 132.7 MB, Electron 156.3 MB, Kotlin
Multiplatform 668.7 MB. Steady-state CPU as a percent of one core: Reaktor
7.1%, D3D12 4.1%, OpenGL 5.0%, Flutter 15.3%, Electron 19.6%,
Kotlin Multiplatform 29.4%](benchmark.svg)

The workload is [ShaftUI/Shaft](https://github.com/ShaftUI/Shaft)'s: 64 boxes
rotating at 60 fps in an 800×600 window, every arm on the same machine: a
Lenovo laptop with a 6-core Ryzen 5 4600H, a GeForce RTX 2060 and a 1920×1080
120 Hz display, on Windows 11 Pro. Each arm is its own app, built with its
framework's toolchain; one script launches it and samples its whole process
tree from outside. [`benchmarks/`](../benchmarks/BENCHMARKS.md) has the
projects and the runner. Reaktor appears once per `--renderer`. Shaft is not
plotted: it does not link on Windows.

| Arm | Memory | CPU, with startup | CPU, steady state |
| --- | --- | --- | --- |
| **Reaktor**, `software` | 8.9 MB | 8.8% | 7.1% |
| **Reaktor**, `direct3d11` | 58.0 MB | 12.1% | 10.2% |
| **Reaktor**, `direct3d12` | 120.5 MB | 11.4% | 4.1% |
| **Reaktor**, `opengl` | 69.5 MB | 12.1% | 5.0% |
| Flutter | 132.7 MB | 38.8% | 15.3% |
| Electron | 156.3 MB | 36.5% | 19.6% |
| Kotlin Multiplatform | 668.7 MB | 51.5% | 29.4% |

- **About a fifteenth of Flutter's memory and less than half its CPU**, with
  startup or without, and about a seventy-fifth of Kotlin Multiplatform's
  memory.
- **Every Reaktor renderer takes less CPU than every other framework.** On a
  GPU renderer its memory is mostly the graphics driver's: 58–121 MB, below
  Flutter's 133.
- **Startup counts.** A run that draws for only 0.2 s takes 0.16 s of CPU on
  `software` and 0.41–0.47 s on a GPU renderer (medians of five runs). Once running, `direct3d12` and
  `opengl` take two to three points less than `software`.
- **A GPU renderer's CPU depends on what else uses the GPU.** With another
  program keeping it busy, `direct3d11` and `opengl` missed 60 fps and took
  16–28% of a core. These runs had the GeForce RTX 2060 idle.

`software` used to read 67.3% here, because every rectangle reached SDL as
two blended triangles. Drawing solid rectangles as plain fills cut a frame
from 11.48 ms to 2.12 ms, with the same pixels.

### How it was measured

```bash
./benchmarks/run.ps1 -Seconds 6 -Fps 60 -Runs 10 -PinClock
./benchmarks/run.ps1 -Fps 60 -Runs 10 -Steady -PinClock
```

CPU is a percent of one core: with startup over the whole 6-second run, steady
from 3 s to 8 s of a 9-second run. Every arm is held to 60 fps and sleeps out
the rest of each frame; Kotlin Multiplatform held 59. Each figure is the
median of ten runs, the steady-state ones taken in turn across arms; passes
differ by up to three points of CPU. Memory is peak private bytes, and CPU the
sum, across each app's process tree. `-PinClock` runs on a copy of the power
plan that holds the processor at full speed, and restores the plan after.
Every row was measured on 2026-10-24 with the GPU idle, the other frameworks in
one pass and Reaktor's again later that day: Flutter
3.47.8, Electron 44.3.0, and Kotlin 2.4.20 with Compose 1.12.0 on JDK 21. The
Flutter arm took 91 MB and 19% of a core with startup on the Flutter release
used before; 3.47.8 takes more of both. The sampler counts what an app's own timers miss: `bench` times
0.07 ms of work per `direct3d11` frame, where the sampler charged over 10%.
`vulkan` falls back to `direct3d11` in this SDL build.

## Size

| Artifact | Bytes |
| --- | --- |
| Windows | 2,473,984 |
| Linux, stripped | 3,967,504 |
| macOS, stripped | 3,242,248 |
| GhostBSD, stripped | 2,793,248 |
| Web bundle (Windows emsdk) | 1,389,356 |
| Web bundle (Linux emsdk) | 1,389,186 |
| Web bundle (macOS emsdk) | 1,389,145 |
| Web bundle (GhostBSD emsdk) | 1,389,525 |

The older single-executable build, on each platform. Everything links
statically — SDL, libcss, plutosvg, plutovg, Nuklear, Onlay, and mojibake and
kb_text_shape with Text — so only the C runtime loads from outside. The web
bundles agree within 211 bytes on Windows, Linux and macOS (emscripten 6.0.9),
and 380 with GhostBSD's emscripten 6.0.3; the Linux one is 1,221,689 bytes of
`.wasm`, 86,001 of assets, 80,045 of `.js` and 1,451 of `.html`. Today's
build also links FreeType, which bakes the glyph atlas, and reads the
stylesheets with its own engine in place of libcss. The Windows
binary's icon is a 125 KB resource of nine sizes, built from the SVG by
`tools/mkicon.c`.

### Modules

`showcase`, each module off in turn; native on the Windows machine below, web
on Windows emsdk 6.0.9. The web rows are from the build before the CSS engine
replaced libcss.

| `showcase` | Default | Module off | Saved |
| --- | --- | --- | --- |
| `.exe`, accessibility bridges | 3,232,256 | 3,216,896 | 15,360 (0.48%) |
| `.exe`, Locale | 3,232,256 | 3,216,896 | 15,360 (0.48%) |
| `.exe`, Text | 3,232,256 | 2,607,104 | 625,152 (19.3%) |
| `.wasm` + `.js`, accessibility bridges | 2,308,781 | 2,305,843 | 2,938 (0.13%) |
| `.wasm` + `.js`, Locale | 2,308,781 | 2,295,269 | 13,512 (0.59%) |
| `.wasm` + `.js`, Text | 2,308,781 | 1,824,708 | 484,073 (21.0%) |
| `.data`, Locale | 1,910,136 | 143,867 | 1,766,269 (92.5%) |
| `.data`, Text | 1,910,136 | 151,448 | 1,758,688 (92.1%) |

**The bridges are small**; turned off, they drop libraries more than bytes.
Pixels and `--a11y-dump` stay byte-identical, and only a screen reader
notices: UI Automation finds 99 elements on the Buttons page, none without
them.

**Text is mostly tables and a font.** kb_text_shape is 95 KB of code and
391 KB of tables, and mojibake 19 KB of code and 180 KB of line-breaking and
bidirectional data; the `.data` it saves is M PLUS 1p, 1,758,688 bytes.
Locale is 15 KB of the executable and 7,581 bytes of catalogs; compiled in
(`-DREAKTOR_LOCALE_EMBED=ON`), `showcase.exe` is 3,239,936 bytes.

**Text costs memory once it draws.** Medians of private bytes over ten
launches: Diagnostics is 13.0 MB with every module, 12.7 MB without Locale and
12.6 MB without Text — kb_text_shape's tables are not `const`, so they are
committed untouched. Translations is 15.4 MB against 12.9 MB without Text: M PLUS 1p
held open, what kb_text_shape parses from it, the glyph texture and the
caches.

## Memory

Idle on the Login page.

| | Windows | Linux | macOS | GhostBSD |
| --- | --- | --- | --- | --- |
| Committed / private / footprint | **12.7 MB** | **4.3 MB** | **~30 MB** | **5.1 MB** |
| Resident / working set | **29.6 MB** | **11.4 MB** | **33–36 MB** | **11.9 MB** |

Committed is what the process asked the system to back; resident is what is
in RAM, shared library pages included. Windows is today's `showcase`, with
every module; the others are the older single-executable build. Idle on
Windows, `notepad` is 9.1 MB and `simple`, which links no module, 7.2 MB. The
showcase's figure includes a second copy of the glyph atlas: its splash is a
window of its own, and the first such window keeps its atlas for the next.

- **Windows is mostly the system's.** Split before the CSS engine replaced
  libcss, 1.7 MB of the showcase's 12.6 MB was Reaktor's own allocations, the largest the glyph atlas (548 KB) and its
  table (281 KB) for eight sizes in two weights. Most of the rest is the heaps
  of the Windows components it loads: UI Automation, the shell, the input
  method.

- **Linux started at 123.6 MB, almost none of it Reaktor's.** SDL's X11 driver
  set up OpenGL whatever the renderer, and with no GPU that loads Mesa's
  software OpenGL and LLVM: 53 MB of libLLVM, 10 of Mesa, 30 of heap.
  Compiling SDL's GL drivers out brought it to 4.3 MB; `-DREAKTOR_SDL_GL=ON`
  puts them back.
- **GhostBSD lands beside Linux.** Of its 5.1 MB, 2.5 MB is SDL's framebuffer
  (960×680×4), shared with the X server. Its figures come from `ps` and
  `procstat -v`, macOS's from `vmmap`; `reaktor_process_memory` reads only
  Windows and Linux.
- **macOS is ~7× Linux, outside this code.** `vmmap` splits the ~30 MB into
  ~15 MB of Cocoa floor and ~12 MB of heap, mostly three ~2.5 MB bitmaps: SDL's
  render target, its staging copy and the atlas. Without a renderer rewrite
  the floor is 26–28 MB, and Retina pushes committed toward 50 MB.

Windows varies by about half a megabyte between identical runs, so compare ten
launches.
`tools/vmwalk.c` (`cmake --build build --target vmwalk`) splits another
process's private bytes into named buckets.

## CPU

**At rest: 0% of a core** on Windows, Linux and GhostBSD, on every page; macOS
settles around **0.3–0.5%**, as CoreAnimation wakes the loop now and then for
an iteration that draws nothing.

Redrawing continuously at 960×680, p10–p90 of 31 samples:

| Page | Windows | Linux | macOS | GhostBSD |
| --- | --- | --- | --- | --- |
| Login | 6.8–6.9 ms/frame | 6.2–6.7 ms/frame | 17.5–20.5 ms/frame | 6.2–8.3 ms/frame |
| Buttons (the busiest) | 11.5–12.2 ms/frame | 11.7–13.0 ms/frame | 29.6–30.2 ms/frame | 13.4–15.3 ms/frame |

The same rasterizer runs on all four. macOS also uploads the bitmap to a
Metal texture to present it. On GhostBSD most of a frame is the present, a
software blit into VMware's framebuffer: 6.7 ms of Login's 7.1 ms, 11.1 ms of
Buttons' 14.8.

Today's Windows build, at its default 1100×680, redrawing continuously with
vsync off, build, render and present, p10–p90 of 31 frames after 9 to warm up,
over three launches: Login takes **1.3–1.8 ms** a frame and Buttons
**2.7–3.4 ms**; `direct3d11` takes 0.4–0.8 ms and 0.8–1.3 ms. In software the
present is most of it: 1.08 ms of Login's frame.
Without a GPU, `direct3d11` falls back to WARP, which rasterizes on threads of
its own: drawing `bench` flat out, the process spends **1.78 ms** of CPU a
frame on WARP against 0.75 ms on `software` (medians of five runs), which
made software the default.

The accessibility tree adds **3.3 µs** to a drawn frame (82 nodes). With a
GPU, `direct3d12` and `opengl` take less CPU; see **Against the others**.

## What it needs to run

**No GPU**, and **7–13 MB of RAM** on Windows. The default look is compiled
in, so a program needs no files beside it until it uses other Ionicons, Simple.css, the
catalogs or the Japanese font, found under a `.reaktor-root` marker beside it
or up to two folders above. The web build packs those into its `.data` and needs an HTTP server,
since a `file://` page cannot fetch the `.wasm`.

## Where the numbers came from

- **Windows** — 11 Pro (26200), x64, MSVC Release, Ryzen 5 4600H (6 cores,
  12 threads), GeForce RTX 2060, 1920×1080 at 120 Hz, no scaling. Sizes and
  **Modules** with MSVC 14.44 (Visual Studio 2022 Build Tools) and emscripten
  6.0.9; **Against the others** in the 800×600 window every arm asks for, with
  Electron 44.3.0 on Node 22, Flutter 3.38.0, and Compose Multiplatform 1.12.0
  on Kotlin 2.4.20 and Temurin JDK 21.
- **Linux** — Debian 13, x86_64, GCC Release, X11, no GPU.
- **macOS** — 11.7 Big Sur, Intel iMac, Apple Clang 12, 1024×768 non-Retina,
  built with `-DCMAKE_OSX_SYSROOT=…/MacOSX11.3.sdk`.
- **GhostBSD** — 26.1-R15.0p2, x86_64, Clang 19.1.7, X11, VMware, no 3D.

The **Diagnostics** page shows the renderer, the frame breakdown and the
resident set live; reproduce any of this there.
