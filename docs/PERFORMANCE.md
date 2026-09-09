# Performance

## How the app works

Nuklear builds the frame, SDL's **software rasteriser** draws it, and libcss
supplies every colour from a stylesheet read at runtime. There is no GPU path
by default on any platform — see the renderer section of
[DEVELOPMENT.md](DEVELOPMENT.md) for why.

**At rest it draws nothing.** SDL is told to wait for events, and a frame is
built only when something actually changed. So there are two consequences that
explain every number below: **CPU is a count of frames**, not a rate, and
**memory is flat** once the font atlas and the icons are baked.

## Size

| Artefact | Bytes |
| --- | --- |
| `reaktor.exe` (Windows) | 2,406,400 |
| `reaktor` (Linux, stripped) | 3,967,504 |
| `reaktor` (macOS, stripped) | 3,242,248 |
| Web bundle, total | 1,389,145 |

Everything links statically — SDL, libcss, plutosvg, plutovg, with Nuklear
compiled in — so nothing but the C runtime loads from outside the binary.

The web bundle above is the macOS emsdk build; **the Windows emsdk build has
not been measured** and its four files (`.html`, `.js`, `.wasm`, `.data`) may
land a few kilobytes either side of the number above — the Closure Compiler
and `wasm-opt` versions differ between platforms. Worth a line here once
someone runs `build-wasm.ps1`.

## Memory

Idle on the Login tab.

| | Windows | Linux | macOS |
| --- | --- | --- | --- |
| Committed / private / footprint | median **9.0 MB** (8.0–9.9) | **4.3 MB** | **~30 MB** (29.9–31.4) |
| Resident / working set | **29–34 MB** | **11.4 MB** | **33–36 MB** |

Two different questions, not two estimates of one: committed is what the
process asked the system to back, resident is what is in RAM now including
shared library pages that are not this process's to give back.

Linux was **123.6 MB** until SDL's OpenGL drivers were compiled out. SDL's X11
driver initialises GL whatever renderer you ask for, and on a machine without a
GPU that GL is Mesa's llvmpipe — 53 MB of libLLVM, 10 of libgallium, 30 of
heap, none of it Reaktor's. `-DREAKTOR_SDL_GL=ON` puts it back.

Windows spreads about 2 MB between identical runs, so a single reading against
a single reading means nothing; ten launches per arm resolve about 1 MB.

### macOS is Cocoa plus SDL, and that is where the number comes from

The macOS committed figure is ~7× Linux, and every megabyte of the gap is
outside `src/`. `vmmap` on an idle Login tab breaks the ~30 MB footprint down
to about **~15 MB of macOS platform floor** and **~12 MB of Reaktor's own
heap**, with the rest small change:

| Region | Dirty | What it is | Reducible? |
| --- | --- | --- | --- |
| IOKit shared segments | 8.0 MB | WindowServer↔app IPC, GPU device state | No — one-time cost per NSWindow |
| CG backing stores | 3.1 MB | AppKit's offscreen buffers for window frame, shadow, cursor | No — Cocoa compositor |
| IOSurface | 2.5 MB | One 960×680×4 framebuffer for the compositor | No — proportional to window size |
| Framework `__DATA_DIRTY` | ~1.4 MB | AppKit, CoreFoundation, LaunchServices init | No |
| MALLOC_LARGE | ~8 MB | Three ~2.5 MB blocks: SDL software render bitmap, `SDL_Texture` staging bitmap, font atlas raster | Only by rewriting the SDL renderer path |
| MALLOC_SMALL + TINY | ~3.6 MB | Nuklear buffers, libcss parse tree, SVG icon cache, a11y arenas | ~0.5 MB with tighter caches, not worth the code |

Two things fall out of that table:

- **The prediction that macOS would sit close to the Linux figure was wrong.**
  On Linux, X11's shared-memory `XImage` collapses the "app draws, GPU shows"
  path into one buffer shared with the server; on macOS every step keeps a
  private copy — the SDL software bitmap, the `SDL_Texture` staging bitmap,
  the `IOSurface` handed to the compositor, and CoreGraphics's own offscreen
  backing behind it. That is the ~10 MB of extra framebuffer you see, and it
  is macOS's compositor pipeline rather than anything Reaktor asked for.
- **The reachable ceiling with no rewrite is about 26–28 MB**, from a
  periodic `malloc_zone_pressure_relief` on idle and a slightly tighter font
  atlas. A real cut — down toward ~22 MB — means collapsing the SDL software
  path's memory bitmap and staging texture into one buffer, which is an edit
  to `src/nk_sdl3_renderer.h` that would want retesting on Windows and Linux
  before shipping. It has not been done because the win is small and the
  vendored file exists to avoid this exact kind of maintenance.

macOS 11.7 on Intel, 1024×768 non-Retina, at the default 960×680 window. A
Retina display adds a ×4-pixel drawable at each of the framebuffer stops above
and pushes the committed figure toward 50 MB — which is still Cocoa's price
and not Reaktor's.

## CPU

**At rest: 0% of one core** on Windows and Linux, every tab. Not "low" —
nothing. macOS is not the same: the running average settles around
**~0.3–0.5%**, which is small but not zero — CoreAnimation ticks the window
periodically and each tick wakes SDL_WaitEvent for a no-op iteration. Under
the same `waitevent` policy the app draws nothing on those wakes; the cost
is the wake itself.

Forced to redraw at a fixed rate, 960x680:

| Page | Windows | Linux |
| --- | --- | --- |
| Login | 6.8–6.9 ms/frame | 6.2–6.7 ms/frame |
| Buttons (the busiest) | 11.5–12.2 ms/frame | 11.7–13.0 ms/frame |

macOS frame times are not measured here yet — the Diagnostics tab reports
them live and is where to reproduce.

The same rasteriser on both, so the agreement is the expected result rather
than a coincidence. Cost is linear in frames: a pointer swept across a row of
buttons draws one per hover crossing, typing draws one per keystroke.

For contrast, the one figure that made the software default worth it: on
Windows with no GPU, `direct3d11` falls back to WARP and the same frame costs
**78 ms**.

The accessibility tree adds **4.2 µs** to a drawn frame (82 nodes), which is
below the noise in the measurements above.

## macOS

Now measured. The size, memory and CPU tables above carry a macOS column;
memory in particular has its own section — the total is dominated by Cocoa's
compositor pipeline rather than by anything Reaktor holds, so it does not
sit near the Linux figure and the earlier prediction that it would was
wrong. What is left to measure is frame time on macOS, which the Diagnostics
tab reports live.

## What it needs to run

**No GPU.** The renderer is a software rasteriser on every platform, so there
is no driver, shader or framebuffer requirement — a machine that can run a
browser can run this. Budget about **10 MB of RAM** and a few hundred triangles
a frame.

Two things a deployment does need:

- **The asset files.** A native build reads the stylesheet and the icons from
  disk at runtime, found by walking up to the `.reaktor-root` marker, so they
  ship beside the binary or `REAKTOR_ROOT` points at them. The web build
  packages them into `reaktor.data`.
- **A served web build.** A `file://` page cannot fetch the `.wasm`.

## Where the numbers came from

Windows: 11 Pro (26200), x64, MSVC Release, in a VirtualBox VM with no
Direct3D 11 adapter. Linux: Debian 13, x86_64, GCC Release, X11, also without a
GPU. macOS: 11.7 Big Sur on Intel iMac, Apple Clang 12, 1024×768 non-Retina,
built through the `-DCMAKE_OSX_SYSROOT=…/MacOSX11.3.sdk` workaround the
default CommandLineTools SDK on that machine needs. All at 1.00x scale in a
960x680 window. The **Diagnostics** tab reports the renderer, the frame
breakdown and the resident set live, which is where to reproduce any of this.
