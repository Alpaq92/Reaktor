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
Memory: Reaktor 9.6 MB, Shaft 46.4 MB, Flutter 50.2 MB, Electron 189.7 MB,
Kotlin Multiplatform 235.1 MB. CPU: Reaktor 6.1%, Shaft 10.8%, Flutter 11.4%,
Electron 23.9%, Kotlin Multiplatform 18.4%](../assets/rest/benchmark.svg)

Reaktor's bars are `samples/bench`; the other four are
[ShaftUI/Shaft](https://github.com/ShaftUI/Shaft)'s, read off their README.

**9.6 MB and 6.1% of the machine**, animating without stopping — which is the
case this design is worst at, since nothing here is free when the picture never
holds still.

### How the CPU bar was measured

`--fps 60` holds the run to a real 60 fps and sleeps out the rest of each frame,
so the process is charged for the drawing and not the wait. 6.1% is of all 12
threads, the median of ten runs — the same measurement `bench` prints as 72.9%
of one core. An outside sampler agreed to within a point, and exactly on memory.

```bash
./build/bench --bench-seconds 6 --fps 60 --renderer software
```

The renderer is the other half of it. `--renderer direct3d11` takes the frame
from **12.3 ms to 0.35 ms** and the CPU to **0.7%**, and private memory the
other way, from **9.6 MB to 61.0 MB**. Software is the default, and the bars
above.

> ⚠️ **These bars are not measured on equal terms yet.** Shaft's four come
> from an M1 Max and their README names no denominator, so read the comparison
> as indicative until all five are drawn on one machine.

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

The same rasterizer on all four, so Windows and Linux agreeing is the expected
result rather than a coincidence; macOS runs the same code but pays another
pass for it, because SDL's Metal renderer uploads the software bitmap to a
texture and presents that — the same private-copy-at-every-step pattern that
dominates the memory table above. Cost is linear in frames: a pointer swept
across a row of buttons draws one per hover crossing, typing draws one per
keystroke.

GhostBSD's frame is present, not drawing: 6.7 ms of the Login median's 7.1 and
11.1 ms of the Buttons median's 14.8 go to the present alone, leaving 0.4 ms
and 3.4 ms of build and render. Its X server drives VMware's SVGA II with no
3D, so a frame is a software blit into the virtual framebuffer and then out to
the host again. The other three columns' splits were not recorded, so the table
compares whole frames and nothing finer; its ranges are p10–p90 of 31 samples.

The accessibility tree adds **4.2 µs** to a drawn frame (82 nodes), below the
noise in the measurements above. For contrast, the one figure that made the
software default worth it: on Windows with no GPU, `direct3d11` falls back to
WARP and the same frame costs **78 ms**. `--renderer` reproduces that on
demand — `bench --no-vsync --renderer direct3d11` takes the frame from 4.7 ms
to 20.3 ms on the same machine.

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
  scaling. The **Against the others** bars were taken here, in the
  800×600 window `samples/bench` asks for.
- **Linux** — Debian 13, x86_64, GCC Release, X11, also without a GPU.
- **macOS** — 11.7 Big Sur, Intel iMac, Apple Clang 12, 1024×768 non-Retina,
  built with `-DCMAKE_OSX_SYSROOT=…/MacOSX11.3.sdk`.
- **GhostBSD** — 26.1-R15.0p2, x86_64, Clang 19.1.7, X11, VMware, no 3D.
- **The four platform columns** — 1.00× scale, 960×680 window.

The **Diagnostics** page reports the renderer, the frame breakdown and the
resident set live, which is where to reproduce any of this.
