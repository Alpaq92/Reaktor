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

On Windows today, `showcase.exe` is 3,215,360 bytes, `notepad.exe` 2,274,304,
`simple.exe` 2,140,672 and `bench.exe` 2,130,944. The showcase links all nine
optional modules, `notepad` three (settings, popup windows, printing) and the
other two none; each carries the default look —
Aileron, the mark, the sheets and seven icons, 73,123 bytes — compiled in. The
four-platform figures below come from an older build that shipped one
executable, at 1.00× scale in a 960×680 window; the runtime they measure is
the one all four programs share.

## Against the others

![Two bar charts of drawing 64 rotating boxes at 60 fps in an 800x600 window.
Memory: Reaktor 8.9 MB, the same program on D3D12 120.5 MB and on OpenGL
69.2 MB, Flutter 132.7 MB, Electron 156.3 MB, Kotlin
Multiplatform 668.7 MB. Steady-state CPU as a percent of one core: Reaktor
6.3%, D3D12 4.1%, OpenGL 5.5%, Flutter 15.3%, Electron 19.6%,
Kotlin Multiplatform 29.4%](benchmark.svg)

The workload is [ShaftUI/Shaft](https://github.com/ShaftUI/Shaft)'s: 64 boxes
rotating at 60 fps in an 800×600 window, every arm on the same machine: a
Lenovo laptop with a 6-core Ryzen 5 4600H, a GeForce RTX 2060 and a 1920×1080
120 Hz display, on Windows 11 Pro. Each arm is its own app, built with its
framework's toolchain; one script launches it and samples its whole process
tree from outside. [`benchmarks/`](../benchmarks/BENCHMARKS.md) has the
projects and the runner. Reaktor appears once per `--renderer`; its arms are
0.10.0, measured on 2026-11-07, the others from the pass of 2026-10-24. Shaft
is not plotted: it does not link on Windows.

| Arm | Memory | CPU, with startup | CPU, steady state |
| --- | --- | --- | --- |
| **Reaktor**, `software` | 8.9 MB | 7.9% | 6.3% |
| **Reaktor**, `direct3d11` | 57.7 MB | 13.5% | 9.1% |
| **Reaktor**, `direct3d12` | 120.5 MB | 11.1% | 4.1% |
| **Reaktor**, `opengl` | 69.2 MB | 11.2% | 5.5% |
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
  `software` and 0.41–0.47 s on a GPU renderer (medians of five runs, on
  2026-10-24). Once running, `direct3d12` takes two points less than
  `software`, `opengl` under one.
- **About 2.2 MB of Reaktor's 8.9 is the antivirus.** Bitdefender puts two
  components into every process on this machine, so every arm carries them.
  Walked with VirtualQueryEx and a heap ledger on `bench` on 2026-11-04, they
  come to two private heaps of 776 KB each (one the hook module's, one holding
  a 642 KB table of the other's), 74 code pages patched with hooks (303 KB) and 56
  more copied unchanged (229 KB), 53 KB of trampolines and about 180 KB of
  pages beside. Subtract that to compare with a machine without it. The rest
  of the 9.3 MB walked is 1.9 MB of window framebuffer, 1.75 MB of system DLL
  data made private, about 0.9 MB of page tables and other commit a walk
  cannot see, and 1.1 MB allocated by Reaktor, SDL, Nuklear and FreeType —
  the glyph atlas, 537 KB, and its table, 143 KB, the largest.
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
median of ten runs, the steady-state ones taken in turn across arms, and
Reaktor's the mean of two such medians; passes differ by up to three points of
CPU. Memory is peak private bytes, and CPU the
sum, across each app's process tree. `-PinClock` runs on a copy of the power
plan that holds the processor at full speed, and restores the plan after.
The other frameworks were measured in one pass on 2026-10-24 and Reaktor's rows
on 2026-11-07, all with the GPU idle: Flutter
3.47.8, Electron 44.3.0, and Kotlin 2.4.20 with Compose 1.12.0 on JDK 21. The
Flutter arm took 91 MB and 19% of a core with startup on the Flutter release
used before; 3.47.8 takes more of both. The sampler counts what an app's own timers miss: `bench` times
0.07 ms of work per `direct3d11` frame on 2026-10-22, where the sampler charges
9% once running.
`vulkan` falls back to `direct3d11` in this SDL build. Run the script from
PowerShell: started through Git Bash, the same `bench` reads 5.9–6.2 MB of
private bytes against 8.2–8.7 MB, three runs each.

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

Nuklear is compiled with `-Os` (`/O1` with MSVC): the stripped shared
libreaktor on Linux x86-64 goes from 1,880,624 bytes at `-O3` to 1,737,264,
and Nuklear's own code from 306,649 to 168,042. `bench --no-vsync --video
dummy` on the Linux desktop below, median of seven runs, CPU time a frame:

| Build | `-O3` | `-Os`, three rounds |
| --- | --- | --- |
| shared | 563 µs | 574, 563, 528 µs |
| static | 527 µs | 548, 523, 511 µs |

The rounds spread wider than the change. Every showcase tab in both themes,
`notepad` and `simple` draw and dump byte for byte as they did at `-O3`.

SDL's GPU API is left out unless `REAKTOR_SHADERS` is on. Built in, it costs
the software renderer too: SDL then loads the Vulkan loader, every installed
Vulkan driver and LLVM, and the Diagnostics page's "window and renderer" step
goes from +0.2 MB private to +34.4 MB. On the Linux desktop below, the shared
build, `bench --no-vsync`, five runs each, both on the software backend:

| `REAKTOR_SHADERS` | A frame on the display | Private |
| --- | --- | --- |
| `OFF` | 2.08–2.35 ms | 5.0 MB |
| `ON` | 13.59–16.31 ms | 63.1 MB |

The stripped `libreaktor-sdl3.so` is 2,659,344 bytes without it and 2,950,704
with it. With `--video dummy`, where nothing reaches a display, the frame costs
563–622 µs of CPU without it and 588–740 µs with it.

### Modules

`showcase`, each module off in turn; native on the Windows machine below, web
on Windows emsdk 6.0.9. The web rows are from the build before the CSS engine
replaced libcss.

| `showcase` | Default | Module off | Saved |
| --- | --- | --- | --- |
| `.exe`, accessibility bridges | 3,215,360 | 3,200,000 | 15,360 (0.48%) |
| `.exe`, Locale | 3,215,360 | 3,200,000 | 15,360 (0.48%) |
| `.exe`, Text | 3,215,360 | 2,591,232 | 624,128 (19.4%) |
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
(`-DREAKTOR_LOCALE_EMBED=ON`), `showcase.exe` is 3,222,528 bytes.

**Text costs memory once it draws.** Medians of private bytes over ten
launches: Diagnostics is 12.6 MB with every module, 12.6 MB without Locale and
12.1 MB without Text — kb_text_shape's tables are not `const`, so they are
committed untouched. Translations, in Japanese, is 12.3 MB against 11.8 MB
without Text: M PLUS 1p held open, what kb_text_shape parses from it, the
glyph texture and the caches.

## Memory

Idle on the Login page.

| | Windows | Linux | macOS | GhostBSD |
| --- | --- | --- | --- | --- |
| Committed / private / footprint | **12.3 MB** | **4.3 MB** | **~30 MB** | **5.1 MB** |
| Resident / working set | **29.3 MB** | **11.4 MB** | **33–36 MB** | **11.9 MB** |

Committed is what the process asked the system to back; resident is what is
in RAM, shared library pages included. Windows is today's `showcase`, with
every module, medians of ten launches; the others are the older
single-executable build. Idle on Windows, `notepad` is 8.9 MB and `simple`,
which links no module, 7.4 MB. The
showcase's figure includes a second copy of the glyph atlas: its splash is a
window of its own, and the first such window keeps its atlas for the next.

- **Windows is mostly the system's.** Split before the CSS engine replaced
  libcss, 1.7 MB of the showcase's 12.6 MB was Reaktor's own allocations, the largest the glyph atlas (548 KB) and its
  table (281 KB) for eight sizes in two weights. Most of the rest is the heaps
  of the Windows components it loads: UI Automation, the shell, the input
  method.
- **Bitdefender is in every Windows figure here**, about 2.2 MB of each;
  [the chart's note](#against-the-others) has the split, measured on `bench`.
  In the showcase its two heaps are the 776 KB pair among the heap segments.

- **Linux started at 123.6 MB, almost none of it Reaktor's.** SDL's X11 driver
  set up OpenGL whatever the renderer, and with no GPU that loads Mesa's
  software OpenGL and LLVM: 53 MB of libLLVM, 10 of Mesa, 30 of heap.
  Compiling SDL's GL drivers out brought it to 4.3 MB; `-DREAKTOR_RENDERERS=opengl`
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

**Three apps share 21% at most.** `tools/ci/pss.sh` runs `showcase`,
`notepad` and `simple` together, idle; medians of ten launches. Each carries
its own copy of libreaktor and SDL, which a shared library would map once.

| | PSS | Clean pages of its binary |
| --- | --- | --- |
| `showcase` | 9,180 KB | 2,796 KB |
| `notepad` | 5,957 KB | 2,208 KB |
| `simple` | 5,251 KB | 2,164 KB |

Of the 20,433 KB together, one copy instead of three would save 4,366 KB, or
21.3%. That counts each app's own code as library code, so it is an upper
bound.

Counted page by page, it is 20.2%. `tools/ci/pss-pages.sh` links the three
with maps and sorts each resident page of a binary by the object it came from:
2,386 KB of `showcase`'s is libreaktor and SDL, 2,090 KB of `notepad`'s and
2,018 KB of `simple`'s. Sharing would save 4,120 KB of the 20,423. That is still
an upper bound: it takes the largest app's library pages as all three's, and
leaves out what the dynamic linker costs each app.

On a Linux desktop it falls under 20%: Linux Mint 22.3, Cinnamon on X11,
llvmpipe (no GPU passthrough), a KVM guest on an AMD Ryzen 5 4600H with 4
cores. `showcase` grows to 14,224 KB there, while the library pages barely
move. Counting every clean page of a binary, sharing saves 4,602 KB of
24,157 KB, or 19.0%; counted page by page, 4,176 KB of 24,200 KB, or 17.3%.

**Shared, it saves 19%.** The same three on that desktop, five launches each,
static and then linked to `libreaktor.so` (`-DREAKTOR_SHARED=ON`):

| | Static PSS | Shared PSS |
| --- | --- | --- |
| `showcase` | 8,528 KB | 7,323 KB |
| `notepad` | 5,480 KB | 4,280 KB |
| `simple` | 4,473 KB | 3,379 KB |
| Together | 18,544 KB | 14,941 KB |

3,603 KB less, 19.4%, near the estimate above. One app on its own pays for
the split instead: its resident set is 0.6–0.9 MB larger shared (`simple`
8.2 MB against 7.3, `showcase` 18.7 against 18.0), the library's relocations
and a second SDL image that no other app is there to share.

### The tray on Linux

The same Linux Mint desktop, `showcase` idle on the Login page, five to eight
launches each, before and after the tray moved from SDL's to libdbus:

| | SDL's tray | Over libdbus | No tray |
| --- | --- | --- | --- |
| Shared libraries mapped | 110 | 57 | 57 |
| GTK, GDK, ATK, dbusmenu-gtk3, AppIndicator | mapped | none | none |
| `reaktor_tray_open` | 79–133 ms | 7–23 ms | |
| First frame, Diagnostics "startup" | 762–963 ms | 794–950 ms | 740–861 ms |
| Private dirty | 68.9–72.0 MB | 68.1–76.8 MB | 65.0–73.0 MB |

`simple` maps 57 too. The first frame and private memory vary more between
launches than the tray moves them; the open itself is the measured saving.

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
Metal texture to present it. On GhostBSD the timers put 6.7 ms of Login's
7.1 ms and 11.1 ms of Buttons' 14.8 in the present, but those timers counted
SDL's rasterizing as present too, so that is the raster and the blit into
VMware's framebuffer together.

The Windows build of 2026-10-24, at its default 1100×680, redrawing
continuously with vsync off, build, render and present, p10–p90 of 31 frames after 9 to warm up,
over three launches: Login takes **1.3–1.8 ms** a frame and Buttons
**2.7–3.4 ms**; `direct3d11` takes 0.4–0.8 ms and 0.8–1.3 ms. Rasterizing is
most of a software frame. SDL does it when its queue is flushed, which then
happened inside the present; flushing first, on 0.9.0 over three launches, it
takes 1.07–1.23 ms of Login's 1.34–1.55 ms and 1.60–1.73 ms of Buttons'
2.45–2.68 ms, and the copy to the window 14–22 µs and 18–65 µs.
Without a GPU, `direct3d11` falls back to WARP, which rasterizes on threads of
its own: drawing `bench` flat out, the process spends **1.78 ms** of CPU a
frame on WARP against 0.75 ms on `software` (medians of five runs), which
made software the default.

The accessibility tree adds **3.3 µs** to a drawn frame (82 nodes). Once
running, `direct3d12` and `opengl` take less CPU, though more with startup; see
**Against the others**.

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
- **Sharing** — GitHub `ubuntu-24.04` runner, x86_64, Debian 12 container,
  GCC Release, Xvfb, no GPU.

The **Diagnostics** page shows the renderer, the frame breakdown and the
resident set live; reproduce any of this there.
