# Performance

**Every pixel is drawn by the CPU.** Nuklear turns the widgets into shapes and
SDL's software rasterizer fills them in; no GPU is required anywhere, for the
reasons in [the renderer](DOCUMENTATION.md#the-renderer).

**Nothing is drawn until something changes.** The app sleeps on the event
queue and builds a frame only when the picture would differ. So **CPU is a
count of frames, not a load**, and **memory is a floor, not a curve**: once the
atlas and icons are baked, nothing grows.

On Windows today, `showcase.exe` is 3,473,408 bytes, `notepad.exe` 2,491,392,
`simple.exe` 2,456,064 and `bench.exe` 2,447,360. The showcase links all three
optional modules and the others none; each carries the default look —
Aileron, the mark, the sheets and seven icons, 67,829 bytes — compiled in. The
four-platform figures below come from an older build that shipped one
executable, at 1.00× scale in a 960×680 window; the runtime they measure is
the one all four programs share.

## Against the others

![Two bar charts of drawing 64 rotating boxes at 60 fps in an 800x600 window.
Memory: Reaktor 10.0 MB, the same binary on D3D11 60.7 MB and on OpenGL
90.2 MB, Flutter 92.2 MB, Electron 158.3 MB, Kotlin Multiplatform 669.8 MB.
CPU as a percent of one core: Reaktor 16.4%, D3D11 14.4%, OpenGL 14.1%,
Flutter 16.6%, Electron 39.0%, Kotlin Multiplatform 56.1%](../assets/rest/benchmark.svg)

The workload is [ShaftUI/Shaft](https://github.com/ShaftUI/Shaft)'s: 64 boxes
rotating at 60 fps in an 800×600 window, run on one machine by one procedure;
[`benchmarks/`](../benchmarks/BENCHMARKS.md) has the projects and the runner.
Reaktor appears three times because `--renderer` changes what it is. Shaft is
not plotted: it does not link on Windows.

| Arm | Memory | CPU, one core |
| --- | --- | --- |
| **Reaktor**, `software` | **10.0 MB** | **16.4%** |
| **Reaktor**, `direct3d11` | 60.7 MB | 14.4% |
| **Reaktor**, `opengl` | 90.2 MB | 14.1% |
| Flutter | 92.2 MB | 16.6% |
| Electron | 158.3 MB | 39.0% |
| Kotlin Multiplatform | 669.8 MB | 56.1% |

- **A ninth of Flutter's memory**, a sixty-seventh of Compose's, and the
  cheapest arm on CPU after Reaktor's own GPU backends.
- **The GPU backends buy about two points of CPU for six to nine times the
  memory**, which is why the default stays software. On a GPU driver Reaktor
  costs what the GPU frameworks do: `opengl` is 90.2 MB against Flutter's
  92.2, and the three GPU arms fall within 2.5 points on CPU. At that size the
  number is the GL context and its buffers, not the framework.

**The top row used to read 67.3%**, and not from rasterizing. `nk_convert`
turns every shape into triangles, so a panel background reached SDL as two
textured, blended triangles over the window: **16.38 ms**, against **6.95 ms**
untextured and **0.12 ms** as a fill. The renderer now fills an axis-aligned
quad of one color, unblended when opaque. A frame of this benchmark went from
**11.48 ms to 2.12 ms** and an empty window from 8.06 to 0.54 ms, with pixels
within one least significant bit. Only measuring the present found it.

### How it was measured

```bash
./benchmarks/run.ps1 -Seconds 6 -Fps 60 -Runs 10
```

Every arm is held to 60 fps and sleeps out each frame, so it is charged for
drawing, not waiting. Each figure is the median of ten 6-second runs; across
three passes CPU moved by up to three points and memory by under a megabyte.
Memory is peak summed private bytes across the process tree; the JVM has no
heap flags. The sampler, not the apps' own counters, sets the bars: those miss
driver threads and presentation — for `direct3d11`, `bench` extrapolated 2.5%
of a core where the sampler charged 16.5%. `direct3d12` and `vulkan` fall back
to D3D11 in this SDL build.

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
`.wasm`, 86,001 of assets, 80,045 of `.js` and 1,451 of `.html`. The Windows
binary's icon is a 125 KB resource of nine sizes, built from the SVG by
`tools/mkicon.c`.

### Modules

`showcase`, each module off in turn; native on the Windows machine below, web
on Windows emsdk 6.0.9.

| `showcase` | Default | Module off | Saved |
| --- | --- | --- | --- |
| `.exe`, accessibility bridges | 3,480,576 | 3,465,216 | 15,360 (0.44%) |
| `.exe`, Locale | 3,480,576 | 3,466,240 | 14,336 (0.41%) |
| `.exe`, Text | 3,480,576 | 2,692,608 | 787,968 (22.6%) |
| `.wasm` + `.js`, accessibility bridges | 1,968,979 | 1,966,141 | 2,838 (0.14%) |
| `.wasm` + `.js`, Locale | 1,968,979 | 1,953,842 | 15,137 (0.77%) |
| `.wasm` + `.js`, Text | 1,968,979 | 1,464,437 | 504,542 (25.6%) |
| `.data`, Locale | 1,810,099 | 44,478 | 1,765,621 (97.5%) |
| `.data`, Text | 1,810,099 | 51,411 | 1,758,688 (97.2%) |

**The bridges are small**; turned off, they drop libraries more than bytes.
Pixels and `--a11y-dump` stay byte-identical, and only a screen reader
notices: UI Automation finds 99 elements on the Buttons page, none without
them.

**Text is mostly tables and a font.** kb_text_shape is 113 KB of code and
443 KB of tables, and mojibake adds its line-breaking and bidirectional data;
the `.data` it saves is M PLUS 1p, 1,758,688 bytes. Locale is 14 KB of code
and 6,933 bytes of catalogs; compiled in (`-DREAKTOR_LOCALE_EMBED=ON`),
`showcase.exe` is 3,487,232 bytes.

**Text costs memory once it draws.** Medians of private bytes over ten
launches: Diagnostics is 8.0 MB with every module, 7.6 MB without Text and
8.2 MB without Locale — Text's 0.4 MB is kb_text_shape's tables, which are not
`const` and so are committed untouched. Translations is 10.8 MB against 7.7 MB
without Text: M PLUS 1p held open, what kb_text_shape parses from it, the
glyph texture and the caches.

## Memory

Idle on the Login page.

| | Windows | Linux | macOS | GhostBSD |
| --- | --- | --- | --- | --- |
| Committed / private / footprint | **9.0 MB** | **4.3 MB** | **~30 MB** | **5.1 MB** |
| Resident / working set | **29–34 MB** | **11.4 MB** | **33–36 MB** | **11.9 MB** |

Committed is what the process asked the system to back; resident is what is
in RAM, shared library pages included.

- **Linux started at 123.6 MB, almost none of it Reaktor's.** SDL's X11 driver
  set up OpenGL whatever the renderer, and with no GPU that loads Mesa's
  software OpenGL and LLVM: 53 MB of libLLVM, 10 of Mesa, 30 of heap.
  Compiling SDL's GL drivers out brought it to 4.3 MB; `-DREAKTOR_SDL_GL=ON`
  puts them back.
- **GhostBSD lands beside Linux.** Of its 5.1 MB, 2.5 MB is SDL's framebuffer
  (960×680×4) in one SysV segment shared with the X server. Its figures are
  swap-backed resident from `ps` and `procstat -v`, macOS's from `vmmap`;
  `reaktor_process_memory` reads only Windows and Linux.
- **macOS is ~7× Linux, outside this code.** `vmmap` splits the ~30 MB into
  ~15 MB of Cocoa floor and ~12 MB of heap, mostly three ~2.5 MB bitmaps: SDL's
  render target, its staging copy and the atlas. Without a renderer rewrite
  the floor is 26–28 MB, and Retina pushes committed toward 50 MB.

Windows varies by about 2 MB between identical runs, so compare ten launches.
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

The same rasterizer runs on all four. macOS pays an extra pass, uploading the
software bitmap to a Metal texture to present it. On GhostBSD most of a frame
is the present — 6.7 ms of Login's 7.1 ms median, 11.1 ms of Buttons' 14.8 —
a software blit into VMware's SVGA II framebuffer.

The accessibility tree adds **4.2 µs** to a drawn frame (82 nodes). And the
number that made software the default: on the GPU-less VM, `direct3d11` falls
back to WARP and a frame goes from 4.7 ms to **20.3 ms** (before the fill
fix). With a real GPU that reverses — see **Against the others**.

## What it needs to run

**No GPU**, and about **10 MB of RAM**. The default look is compiled in, so a
program needs no files beside it until it uses other Ionicons, simple.css, the
catalogs or the Japanese font, found by walking up to the `.reaktor-root`
marker. The web build packs those into its `.data` and needs an HTTP server,
since a `file://` page cannot fetch the `.wasm`.

## Where the numbers came from

- **Windows** — 11 Pro (26200), x64, MSVC Release, in a VirtualBox VM with no
  Direct3D 11 adapter. Everything from **Size** down, and today's sizes, with
  MSVC 14.44 (Visual Studio 2022 Build Tools) and emscripten 6.0.9 for those
  and **Modules**.
- **Windows, with a GPU** — 11 Pro (26200), x64, MSVC Release, Ryzen 5 4600H
  (6 cores, 12 threads), GeForce RTX 2060, 1920×1080 at 120 Hz, no scaling.
  Everything in **Against the others**, in the 800×600 window every arm asks
  for, with Electron 44.3.0 on Node 22, Flutter 3.38.0, and Compose
  Multiplatform 1.12.0 on Kotlin 2.4.20 and Temurin JDK 21.
- **Linux** — Debian 13, x86_64, GCC Release, X11, no GPU.
- **macOS** — 11.7 Big Sur, Intel iMac, Apple Clang 12, 1024×768 non-Retina,
  built with `-DCMAKE_OSX_SYSROOT=…/MacOSX11.3.sdk`.
- **GhostBSD** — 26.1-R15.0p2, x86_64, Clang 19.1.7, X11, VMware, no 3D.

The **Diagnostics** page shows the renderer, the frame breakdown and the
resident set live; reproduce any of this there.
