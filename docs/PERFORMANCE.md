# Performance

**Every pixel is drawn by the CPU.** Nuklear turns the widgets into shapes, SDL's
software rasterizer fills them in, and no graphics card is required on any
platform — [the renderer](DOCUMENTATION.md#the-renderer) explains why.

**Nothing is drawn until something changes.** SDL sleeps on the event queue and
builds a frame only when the picture would differ, so an untouched window costs
nothing. Two consequences explain every number below: **CPU is a count of
frames, not a load**, and **memory is a floor, not a curve** — once the font
atlas and icons are baked, nothing grows.

The four-platform figures were taken on an older build that shipped a single
executable, at 1.00× scale in a 960×680 window. The runtime is the same library
in all four of today's executables, so they still describe what one costs. On
Windows today: `showcase.exe` 3,338,240 bytes, `notepad.exe` 2,360,320,
`simple.exe` 2,331,136, `bench.exe` 2,312,704. The showcase links all three
optional modules; the other three link none.

## Against the others

![Two bar charts of drawing 64 rotating boxes at 60 fps in an 800x600 window.
Memory: Reaktor 10.0 MB, the same binary on D3D11 60.7 MB and on OpenGL
90.2 MB, Flutter 92.2 MB, Electron 158.3 MB, Kotlin Multiplatform 669.8 MB.
CPU as a percent of one core: Reaktor 16.4%, D3D11 14.4%, OpenGL 14.1%,
Flutter 16.6%, Electron 39.0%, Kotlin Multiplatform 56.1%](../assets/rest/benchmark.svg)

The workload is [ShaftUI/Shaft](https://github.com/ShaftUI/Shaft)'s: 64 boxes
rotating at 60 fps in an 800×600 window. Every bar is a run of it on one
machine, by one procedure — [`benchmarks/`](../benchmarks/BENCHMARKS.md) has the
projects and the runner. Reaktor appears three times because `--renderer`
changes what it is. Shaft itself isn't plotted: it doesn't link on Windows.

| Arm | Memory | CPU, one core |
| --- | --- | --- |
| **Reaktor**, `software` | **10.0 MB** | **16.4%** |
| **Reaktor**, `direct3d11` | 60.7 MB | 14.4% |
| **Reaktor**, `opengl` | 90.2 MB | 14.1% |
| Flutter | 92.2 MB | 16.6% |
| Electron | 158.3 MB | 39.0% |
| Kotlin Multiplatform | 669.8 MB | 56.1% |

- **Reaktor draws this in a ninth of Flutter's memory** and a sixty-seventh of
  Compose's, and is the cheapest arm on CPU apart from its own GPU backends.
- **The GPU backends buy about two points of CPU for six to nine times the
  memory.** That's why the default stays software. On a GPU driver Reaktor
  costs what the GPU frameworks cost: `opengl` is 90.2 MB against Flutter's
  92.2, and `direct3d11`, `opengl` and Flutter fall within 2.5 points on CPU.
  Nuklear-through-SDL and Skia-through-Flutter share almost nothing above the
  driver, so at that size the number is the GL context and its buffers, not the
  framework.

### Why the top row used to read 67.3%

It wasn't rasterizing. `nk_convert` flattens every shape into triangles, so a
panel background arrived as two textured, alpha-blended triangles covering the
window. The same coverage costs **16.38 ms** as textured blended geometry,
**6.95 ms** untextured, and **0.12 ms** as a fill.

The renderer now recognizes an axis-aligned quad of one color and issues a
fill, and skips blending when the color is opaque. A frame of this benchmark
went from **11.48 ms to 2.12 ms**, and an empty window from 8.06 ms to 0.54 ms.
The pixels match to within one least significant bit, and `--renderer
direct3d11` output is unchanged to the bit. Only measuring the present found it.

### How it was measured

```bash
./benchmarks/run.ps1 -Seconds 6 -Fps 60 -Runs 10
```

Every arm is held to 60 fps and sleeps out the rest of each frame, so it is
charged for drawing, not waiting. Each figure is the median of ten 6-second
runs; across three passes CPU moved by up to three points and memory by under a
megabyte. Memory is the peak of summed private bytes across the process tree,
and the JVM runs with no heap flags.

The bars come from that outside sampler rather than the apps' own frame
counters, which miss driver threads and presentation: for `direct3d11`, `bench`
extrapolated 2.5% of a core where the sampler charged 16.5%. `direct3d12` and
`vulkan` are not available in this SDL build and fall back to D3D11.

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

Everything links statically — SDL, libcss, plutosvg and plutovg, with Nuklear
and Onlay compiled in, and mojibake and kb_text_shape wherever the Text module
is — so nothing but the C runtime loads from outside the binary.

The Windows, Linux and macOS web bundles agree to within 211 bytes, so host
differences in Closure and `wasm-opt` don't matter (Windows and Linux both on
emscripten 6.0.9). The Linux bundle is 1,221,689 bytes of `.wasm`, 86,001 of
packaged assets, 80,045 of `.js` and 1,451 of `.html`. GhostBSD widens the
spread to 380 bytes, but on emscripten 6.0.3, so that gap is as much toolchain
as host.

The Windows binary carries its icon as a linked resource: 125 KB, nine sizes,
the 256×256 one stored as PNG. `tools/mkicon.c` builds it from the SVG.

### Modules

Each module off in turn, on the same machine and toolchain; the web figures
are Windows emsdk 6.0.9. Only the showcase links them, so only the showcase is
measured.

| `showcase` | Default | Module off | Saved |
| --- | --- | --- | --- |
| `.exe`, accessibility bridges | 3,338,240 | 3,324,928 | 13,312 (0.40%) |
| `.exe`, Locale | 3,338,240 | 3,322,880 | 15,360 (0.46%) |
| `.exe`, Text | 3,338,240 | 2,551,296 | 786,944 (23.6%) |
| `.wasm` + `.js`, accessibility bridges | 1,851,832 | 1,848,990 | 2,842 (0.15%) |
| `.wasm` + `.js`, Locale | 1,851,832 | 1,837,332 | 14,500 (0.78%) |
| `.wasm` + `.js`, Text | 1,851,832 | 1,361,532 | 490,300 (26.5%) |
| `.data`, Locale | 1,867,379 | 101,758 | 1,765,621 (94.6%) |
| `.data`, Text | 1,867,379 | 108,691 | 1,758,688 (94.2%) |

**The bridges are small**: switching them off drops their libraries, not bytes.
Pixels and `--a11y-dump` stay byte-identical; only a screen reader notices, as
Windows UI Automation finds 92 elements on the Buttons page and none without
them.

**Text is mostly tables and a font.** Of its 787 KB, kb_text_shape's object file
is 113 KB of code and 443 KB of tables, and mojibake adds its line-breaking and
bidirectional data. The `.data` it saves is M PLUS 1p, 1,758,688 bytes. Locale
is 15 KB of code and 6,933 bytes of catalogs; compiled in
(`-DREAKTOR_LOCALE_EMBED=ON`), `showcase.exe` is 3,344,896 bytes.

**Text costs memory once it draws.** Medians of private bytes over ten launches
each: on the Diagnostics page, 8.0 MB with every module, 7.6 MB with Text off
and 8.2 MB with Locale off, Text's 0.4 MB being kb_text_shape's tables, which
are not `const` and so are committed without being touched. On the Translations
page, 10.8 MB against 7.7 MB without Text: M PLUS 1p held open to rasterize
from, what kb_text_shape parses out of it, the glyph texture and the caches.

## Memory

Idle on the Login tab.

| | Windows | Linux | macOS | GhostBSD |
| --- | --- | --- | --- | --- |
| Committed / private / footprint | **9.0 MB** | **4.3 MB** | **~30 MB** | **5.1 MB** |
| Resident / working set | **29–34 MB** | **11.4 MB** | **33–36 MB** | **11.9 MB** |

The rows answer different questions: committed is what the process asked the
system to back; resident is what's in RAM right now, including shared library
pages that aren't the process's to give back.

**Linux started at 123.6 MB, almost none of it ours.** SDL's X11 driver sets up
OpenGL whatever renderer you ask for, and on a machine with no GPU that means
Mesa's software OpenGL, which loads LLVM: 53 MB of libLLVM, 10 of Mesa, 30 of
heap. Compiling SDL's GL drivers out brought it to 4.3 MB;
`-DREAKTOR_SDL_GL=ON` puts them back.

**GhostBSD lands beside Linux**, within 0.5 MB on resident and 0.8 MB on the
other row — as expected for the same rasterizer on the same X11 with no GPU. Of
its 5.1 MB, 2.5 MB is one SysV shared segment of 2,611,200 bytes (960×680×4):
SDL's framebuffer, handed to the X server over MIT-SHM. That leaves 2.6 MB that
is genuinely the process's own. That column is swap-backed resident, not Linux's
private clean-plus-dirty: `reaktor_process_memory` in `core/base/util.c` handles
only `_WIN32` and `__linux__`, so on FreeBSD these came from `ps` and
`procstat -v`, as macOS came from `vmmap`.

**macOS is ~7× Linux, and the gap is outside this code.** `vmmap` splits the
~30 MB into ~15 MB of Cocoa floor and ~12 MB of heap, most of that three ~2.5 MB
bitmaps: SDL's render target, its staging copy, and the font atlas. X11 shares
one buffer between app and server; macOS keeps a private copy at each step of
the compositor. Without a renderer rewrite the floor is 26–28 MB, and a Retina
display pushes committed toward 50 MB.

Windows varies by about 2 MB between identical runs, so compare ten launches,
not one; that resolves about 1 MB. For finer attribution, `tools/vmwalk.c`
splits another process's private bytes into named buckets and reports what it
couldn't account for. Build it with `cmake --build build --target vmwalk`.

## CPU

**At rest: 0% of a core** on Windows, Linux and GhostBSD, on every tab. macOS
settles around **0.3–0.5%**: CoreAnimation ticks the window now and then, and
each tick wakes `SDL_WaitEvent` for an iteration that draws nothing.

Forced to redraw continuously, at 960×680:

| Page | Windows | Linux | macOS | GhostBSD |
| --- | --- | --- | --- | --- |
| Login | 6.8–6.9 ms/frame | 6.2–6.7 ms/frame | 17.5–20.5 ms/frame | 6.2–8.3 ms/frame |
| Buttons (the busiest) | 11.5–12.2 ms/frame | 11.7–13.0 ms/frame | 29.6–30.2 ms/frame | 13.4–15.3 ms/frame |

It's the same rasterizer on all four, so Windows and Linux agreeing is
expected. macOS pays an extra pass: SDL's Metal renderer uploads the software
bitmap to a texture and presents that — the same private copy that dominates
its memory. Cost scales with frames: sweeping the pointer across a row of
buttons draws one per hover crossing.

On GhostBSD the frame is mostly present: 6.7 ms of Login's 7.1 ms median, and
11.1 ms of Buttons' 14.8. Its X server drives VMware's SVGA II with no 3D, so a
frame is a software blit into the virtual framebuffer, then out to the host. The
other platforms' splits weren't recorded; the ranges are p10–p90 of 31 samples.

The accessibility tree adds **4.2 µs** to a drawn frame (82 nodes) — below the
noise above. And the number that made software the default: on the GPU-less VM,
`direct3d11` falls back to WARP and a frame goes from 4.7 ms to **20.3 ms**
(measured before the fill fix). With a real GPU that reverses — see **Against
the others**.

## What it needs to run

**No GPU** — anything that can run a browser can run this. Budget about **10 MB
of RAM** and a few hundred triangles a frame. A deployment needs two things: the
asset files, found by walking up to the `.reaktor-root` marker (the web build
packs them into its `.data` bundle), and, for the web build, an HTTP server,
since a `file://` page can't fetch the `.wasm`.

## Where the numbers came from

- **Windows** — 11 Pro (26200), x64, MSVC Release, in a VirtualBox VM with no
  Direct3D 11 adapter. Everything from **Size** down was taken here.
- **Windows, with a GPU** — 11 Pro (26200), x64, MSVC Release, Ryzen 5 4600H
  (6 cores / 12 threads), GeForce RTX 2060, 1920×1080 at 120 Hz, no display
  scaling. Everything in **Against the others** was taken here, in the 800×600
  window every arm asks for, with Electron 44.3.0 on Node 22, Flutter 3.38.0,
  and Compose Multiplatform 1.12.0 on Kotlin 2.4.20 and a Temurin JDK 21.
- **Linux** — Debian 13, x86_64, GCC Release, X11, no GPU.
- **macOS** — 11.7 Big Sur, Intel iMac, Apple Clang 12, 1024×768 non-Retina,
  built with `-DCMAKE_OSX_SYSROOT=…/MacOSX11.3.sdk`.
- **GhostBSD** — 26.1-R15.0p2, x86_64, Clang 19.1.7, X11, VMware, no 3D.
- **The four platform columns** — 1.00× scale, 960×680 window.

The **Diagnostics** page shows the renderer, the frame breakdown and the
resident set live — that's where to reproduce any of this.
