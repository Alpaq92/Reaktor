# Performance

**Every pixel here is drawn by the CPU.** Nuklear turns the widgets into a
list of shapes, SDL's software rasterizer fills them in, and no graphics card
is asked for on any platform — [the renderer](DOCUMENTATION.md#the-renderer)
explains why not. Which should be the expensive way to do this, and isn't,
because of the other half of the design:

**Nothing is drawn until something changes.** SDL sleeps on the event queue,
and a frame is built only when the picture would actually differ. So the cost
of running this is not a rate you pay per second — it is a price per frame,
multiplied by how often the user makes one happen. A window nobody is touching
costs nothing at all.

That is the sentence to keep in mind for everything below. **CPU is a count of
frames, not a load**, and **memory is a floor, not a curve** — once the font
atlas and the icons are baked, nothing else grows.

The four-platform figures were taken on the build that shipped a single
executable, at 1.00× scale in a 960×680 window. The tree now builds four, and
the current Windows sizes are `showcase.exe` 2,531,328, `notepad.exe`
2,370,560, `simple.exe` 2,341,888 and `bench.exe` 2,322,944 — the runtime is the
same library in each, so everything below still describes what one of them
costs to run.

## Against the others

![Two bar charts of drawing 64 rotating boxes at 60 fps in an 800x600 window.
Memory: Reaktor 10.0 MB, the same binary on D3D11 60.7 MB and on OpenGL
90.2 MB, Flutter 92.2 MB, Electron 158.3 MB, Kotlin Multiplatform 669.8 MB.
CPU as a percent of one core: Reaktor 16.4%, D3D11 14.4%, OpenGL 14.1%,
Flutter 16.6%, Electron 39.0%, Kotlin Multiplatform 56.1%](../assets/rest/benchmark.svg)

The workload is not original. It is
[ShaftUI/Shaft](https://github.com/ShaftUI/Shaft)'s benchmark — 64 boxes
rotating at 60 fps in an 800×600 window — and the comparison started as a
reading of the five figures in its README. Those are gone from the chart now.
They were taken on an M1 Max with no denominator named for the CPU column, and
mixing them with a run on this desk was comparing two machines and calling it a
comparison of frameworks. **Shaft's own figures are excluded, and Shaft is not
plotted at all, because it does not build on Windows** — see below. The
picture it defined is what survived, and every bar above is a run of it here.

Four frameworks drawing that picture on one machine, measured by one
procedure, with Reaktor entered three times because `--renderer` changes what
it is: `benchmarks/` holds the other three as projects of their own — plus a
fourth for Shaft, which is the one that did not run — and
`benchmarks/run.ps1` runs them and samples each from outside over its whole
process tree, because Electron is five processes and a JVM's idea of its own
memory is not the system's.

**The two panels agree, which they did not use to.** Reaktor draws this in a
ninth of Flutter's memory and a sixty-seventh of Compose's, and it is also the
cheapest arm on CPU that is not one of its own GPU backends.

| Arm | Memory | CPU, one core | CPU, all 12 threads |
| --- | --- | --- | --- |
| **Reaktor**, software | **10.0 MB** | **16.4%** | **1.4%** |
| **Reaktor**, `direct3d11` | 60.7 MB | 14.4% | 1.2% |
| **Reaktor**, `opengl` | 90.2 MB | 14.1% | 1.2% |
| Flutter | 92.2 MB | 16.6% | 1.4% |
| Electron | 158.3 MB | 39.0% | 3.3% |
| Kotlin Multiplatform | 669.8 MB | 56.1% | 4.7% |

**That top row used to read 67.3%, and the paragraph under it used to explain
why a CPU rasterizer among GPU ones should be the most expensive thing in the
table.** It was not paying for rasterizing. It was paying for being asked the
wrong way: `nk_convert` flattens every shape to triangles, so a panel
background arrived as two textured, alpha-blended triangles covering the
window. Measured three ways, the same coverage costs 16.38 ms as textured
blended geometry, 6.95 ms untextured, and 0.12 ms as a fill. The renderer now
recognises an axis-aligned quad of one color and issues the fill, and skips the
blend when the color is opaque, because source-over with an alpha of one is the
source. A frame of this benchmark went from 11.48 ms to 2.12 ms and an empty
window from 8.06 ms to 0.54 ms, drawing the same pixels to within one least
significant bit — `--renderer direct3d11` is unchanged to the bit. The lesson
is the one at the top of this file: the cost was in the asking, not the
drawing, and only measuring the present found it.

The first three rows are one executable. `--renderer` is the whole difference,
and what it now buys is about two points of CPU for six to nine times the
memory — which is no longer much of a trade, and is the reason the default
stays where it is. On a GPU driver Reaktor costs what the GPU frameworks cost:
**`opengl` lands on 90.2 MB against Flutter's 92.2**, and the CPU figures of
all four GPU-drawing arms sit inside two and a half points of each other. Those
two have almost nothing in common above the driver — Nuklear through SDL
against Skia through the Flutter engine — which is the point: at that size the
number is the GL context and its buffers, not the framework on top of it.

Both CPU columns are given because the question has to be asked: at 12 threads
the two readings differ by a factor of twelve, and a comparison that does not
say which one it means is not saying anything.

### How it was measured

```bash
./benchmarks/run.ps1 -Seconds 6 -Fps 60 -Runs 10
```

`--fps 60` holds every arm to a real 60 fps and sleeps out the rest of each
frame, so a process is charged for the drawing and not the wait. Each figure is
the median of ten six-second runs. Three full passes moved the CPU medians by
up to three points and the memory by less than a megabyte, so read the CPU
column to the point and the memory column to the tenth. Startup is inside the measurement, so
the run length matters — at three seconds instead of six, Electron reads 45.5%
and Compose 60.7%, while Reaktor does not move.

Memory is the peak of summed private bytes across the process tree, which for
the JVM is with no heap flags: 670 MB is what Compose costs out of the box, not
what it can be squeezed to.

What a single Reaktor frame costs is a separate question, and `--fps 60` is the
wrong run to ask it in: with vsync on, the block lands inside the present and
the frame reads as the whole 16.6 ms period. `--no-vsync` is the run that
answers it, and here the frame goes from **11.42 ms on the software rasterizer
to 0.41 on `direct3d11` and 0.27 on `opengl`** — in every case almost all of it
the present.

Those are the drivers this SDL build actually has. `direct3d12` and `vulkan`
say *not available* and fall back to D3D11, which is worth knowing before
reading a figure attributed to them; `opengles2` does start, at 0.26 ms and
143.7 MB, the memory being ANGLE's. The Diagnostics page prints the backend SDL
settled on next to the one that was asked for, which is the only way to tell a
fallback from a measurement.

The two renderers then disagree about whether that is the whole story.
`bench` extrapolates its software frame to 68.2% of a core at 60 fps and the
sampler charges the process 68.6%, which is as close as two different
instruments get. For `direct3d11` `bench` says 2.5% and the sampler says 16.5%.
Neither is wrong: the drawing really is that cheap, and the difference is
everything the process is charged for that no frame counter can see — the
driver's threads, the presentation, the wakeups that pace the rate. It is the
reason the bars come from outside.

### What these replaced

The previous bars had five frameworks, but only Reaktor's were ours — the other
four came from [ShaftUI/Shaft](https://github.com/ShaftUI/Shaft)'s README, taken
on an M1 Max, with no denominator named for the CPU figure. Reaktor's own
numbers barely moved when they were measured properly: **9.6 MB against 10.0**,
and 6.1% of the machine against **5.6%**. Everything around them did.

**The denominator was the whole trouble.** That panel was labeled percent of
twelve cores, and on twelve cores Flutter measures **1.4%** here where their
README shows 11.4%. Eightfold, in the direction that says their figure was per
core — so the old chart was plotting one per-machine number against four
per-core ones, in the same panel, which flattered the per-machine one by about
twelve. On one denominator the memory ordering survives, and the CPU ordering
went through two upheavals rather than one: first the software rasterizer came
out the most expensive arm in the table, which is what a CPU rasterizer among
GPU ones should be, and then it turned out not to be rasterizing that was
expensive — see the fills above — and it came back to 16.4%.

Compose's 235.1 MB becoming 669.8 is a second kind of difference and not the
same kind of finding. The JVM sizes its heap from the machine's RAM, so that
bar says as much about this machine as it does about Compose.

**Shaft itself is absent, and not for want of trying.** Its Skia bundle ships
Windows binaries and Shaft links D3D12 for Windows, but SwiftSDL3 0.1.6 never
compiles its `joystick/virtual/`, `joystick/hidapi/` or `joystick/windows/`
directories there, so `SDL_joystick.c` has nothing to link against. Everything
else builds — Skia, and the benchmark's own Swift, 25 of 26 steps — and then
the executable does not link:

```
lld-link: error: undefined symbol: SDL_SetJoystickVirtualButtonInner
```

That is the whole of it. Two earlier walls turned out to be this machine rather
than Shaft, and both are gone: the build needs a developer shell, and it needs
git told not to attempt symlinks. `benchmarks/README.md` has the commands. What
remains cannot be fixed from this side — a package cannot add sources to
another package's target, and dependencies here are read as they ship — so
`benchmarks/shaft-bench` stays in the tree, building up to the link, waiting on
a SwiftSDL3 that lists its Windows sources correctly.

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

Everything links statically — SDL, libcss, plutosvg, plutovg, with Nuklear and
Onlay compiled in — so nothing but the C runtime loads from outside the binary.
The three emsdk builds agree to 211 bytes end to end, so the Closure and
`wasm-opt` differences between host platforms turn out not to matter (Windows
and Linux both on emscripten 6.0.9). The Linux bundle is 1,221,689 of `.wasm`,
86,001 of packaged assets, 80,045 of `.js` and 1,451 of `.html`. GhostBSD makes
a fourth and widens the spread to 380 bytes, but on emscripten 6.0.3 rather
than 6.0.9 — so that gap is a toolchain version as much as a host.

The Windows binary also carries the icon as a linked resource: 125 KB, nine
sizes, the 256×256 one stored as PNG. `tools/mkicon.c` builds it from the SVG,
by hand when needed.

## Memory

Idle on the Login tab.

| | Windows | Linux | macOS | GhostBSD |
| --- | --- | --- | --- | --- |
| Committed / private / footprint | **9.0 MB** | **4.3 MB** | **~30 MB** | **5.1 MB** |
| Resident / working set | **29–34 MB** | **11.4 MB** | **33–36 MB** | **11.9 MB** |

Two different questions, not two estimates of one: committed is what the
process asked the system to back, resident is what is in RAM now including
shared library pages that are not this process's to give back.

**Linux started at 123.6 MB, and almost none of it was ours.** SDL's X11
driver sets up OpenGL whatever renderer you ask it for, and on a machine with
no GPU that OpenGL is Mesa's software one, which drags in LLVM: 53 MB of
libLLVM, 10 of Mesa, 30 of heap. Compiling SDL's GL drivers out took it to the
4.3 MB above; `-DREAKTOR_SDL_GL=ON` puts them back.

**GhostBSD lands beside Linux on both rows**, 0.5 MB apart on resident and 0.8
on the other, which is what the same rasterizer on the same X11 with the same
absent GPU should do. Of its 5.1 MB, 2.5 MB is a single SysV shared segment of
2,611,200 bytes — 960×680×4, SDL's framebuffer handed to the X server through
MIT-SHM — leaving 2.6 MB that is genuinely this process's. That one mapping is
the buffer the macOS paragraph below is about, shared here rather than copied.
The column is swap-backed resident, not Linux's private-clean-plus-dirty:
`reaktor_process_memory` in `core/base/util.c` branches on `_WIN32` and
`__linux__` only, so on FreeBSD the app reports zero for its own memory and
these came from `ps` and `procstat -v`, as macOS came from `vmmap`.

**macOS is ~7× Linux, and the gap is outside this code.** `vmmap` splits the
~30 MB into ~15 MB of Cocoa floor and ~12 MB of heap, most of that three
~2.5 MB bitmaps — SDL's render target, its staging copy, the font atlas. X11
shares one buffer between app and server; macOS keeps a private copy at every
step of the compositor, which is why it does not sit near Linux. The floor
without a renderer rewrite is 26–28 MB, and a Retina display pushes committed
toward 50 MB.

Windows spreads about 2 MB between identical runs, so a single reading against
a single reading means nothing; ten launches per arm resolve about 1 MB. When
that is not enough, `tools/vmwalk.c` attributes another process's private bytes
to named buckets and prints what it could not account for rather than
pretending the buckets are exhaustive. Build it with
`cmake --build build --target vmwalk`.

## CPU

**At rest: 0% of one core** on Windows, Linux and GhostBSD, every tab. Not
"low" — nothing. macOS settles around **0.3–0.5%**: CoreAnimation ticks the
window periodically and each tick wakes `SDL_WaitEvent` for a no-op iteration.
The cost is the wake, not a frame.

Forced to redraw at a fixed rate, 960×680:

| Page | Windows | Linux | macOS | GhostBSD |
| --- | --- | --- | --- | --- |
| Login | 6.8–6.9 ms/frame | 6.2–6.7 ms/frame | 17.5–20.5 ms/frame | 6.2–8.3 ms/frame |
| Buttons (the busiest) | 11.5–12.2 ms/frame | 11.7–13.0 ms/frame | 29.6–30.2 ms/frame | 13.4–15.3 ms/frame |

The same rasterizer on all four, so Windows and Linux agreeing is expected
rather than lucky. macOS pays an extra pass: SDL's Metal renderer uploads the
software bitmap to a texture and presents that, the same private copy at every
step that dominates the memory table. Cost is linear in frames — a pointer
swept across a row of buttons draws one per hover crossing.

GhostBSD's frame is present, not drawing: 6.7 ms of the Login median's 7.1 and
11.1 ms of the Buttons median's 14.8 go to the present alone. Its X server
drives VMware's SVGA II with no 3D, so a frame is a software blit into the
virtual framebuffer and then out to the host. The other three columns' splits
were not recorded; these ranges are p10–p90 of 31 samples.

The accessibility tree adds **4.2 µs** to a drawn frame (82 nodes), below the
noise above. And the figure that made the software default worth it: on the
GPU-less VM, `direct3d11` falls back to WARP and the frame goes from 4.7 ms to
**20.3 ms**. A machine with a real GPU inverts that — see
**Against the others**.

## What it needs to run

**No GPU** — a machine that can run a browser can run this. Budget about
**10 MB of RAM** and a few hundred triangles a frame. Two things a deployment
does need: the asset files, found by walking up to the `.reaktor-root` marker
(the web build packages them into its `.data` bundle), and a served web build,
since a `file://` page cannot fetch the `.wasm`.

## Where the numbers came from

- **Windows** — 11 Pro (26200), x64, MSVC Release, in a VirtualBox VM with no
  Direct3D 11 adapter. Everything below **Size** was taken here.
- **Windows, with a GPU** — 11 Pro (26200), x64, MSVC Release, Ryzen 5 4600H
  (6 cores / 12 threads), GeForce RTX 2060, 1920×1080 at 120 Hz, no display
  scaling. All five bars of **Against the others** were taken here, in the
  800×600 window every arm asks for. The other three frameworks were built
  here too: Electron 44.3.0 on Node 22, Flutter 3.38.0, and Compose
  Multiplatform 1.12.0 on Kotlin 2.4.20 and a Temurin JDK 21.
- **Linux** — Debian 13, x86_64, GCC Release, X11, also without a GPU.
- **macOS** — 11.7 Big Sur, Intel iMac, Apple Clang 12, 1024×768 non-Retina,
  built with `-DCMAKE_OSX_SYSROOT=…/MacOSX11.3.sdk`.
- **GhostBSD** — 26.1-R15.0p2, x86_64, Clang 19.1.7, X11, VMware, no 3D.
- **The four platform columns** — 1.00× scale, 960×680 window.

The **Diagnostics** page reports the renderer, the frame breakdown and the
resident set live, which is where to reproduce any of this.
