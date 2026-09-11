# Performance

Nuklear builds the frame, SDL's **software rasterizer** draws it, and libcss
supplies every color from a stylesheet read at runtime. There is no GPU path
by default on any platform — see
[the renderer](DOCUMENTATION.md#the-renderer) for why.

**At rest it draws nothing.** SDL waits for events, and a frame is built only
when something actually changed. Two consequences explain every number below:
**CPU is a count of frames**, not a rate, and **memory is flat** once the font
atlas and the icons are baked.

The four-platform figures were taken on the build that shipped a single
executable, at 1.00× scale in a 960×680 window. The tree now builds three, and
the current Windows sizes are `showcase.exe` 2,526,208, `notepad.exe`
2,365,952 and `simple.exe` 2,338,304 — the runtime is the same library in each,
so everything below still describes what one of them costs to run.

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
sizes, the 256×256 one stored as PNG. `tools/mkicon.c` builds it from the SVG.

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
WARP and the same frame costs **78 ms**.

## What it needs to run

**No GPU** — a machine that can run a browser can run this. Budget about
**10 MB of RAM** and a few hundred triangles a frame. Two things a deployment
does need: the asset files, found by walking up to the `.reaktor-root` marker
(the web build packages them into its `.data` bundle), and a served web build,
since a `file://` page cannot fetch the `.wasm`.

## Where the numbers came from

- **Windows** — 11 Pro (26200), x64, MSVC Release, in a VirtualBox VM with no
  Direct3D 11 adapter.
- **Linux** — Debian 13, x86_64, GCC Release, X11, also without a GPU.
- **macOS** — 11.7 Big Sur, Intel iMac, Apple Clang 12, 1024×768 non-Retina,
  built with `-DCMAKE_OSX_SYSROOT=…/MacOSX11.3.sdk`.
- **GhostBSD** — 26.1-R15.0p2, x86_64, Clang 19.1.7, X11, VMware, no 3D.
- **All four** — 1.00× scale, 960×680 window.

The **Diagnostics** page reports the renderer, the frame breakdown and the
resident set live, which is where to reproduce any of this.
