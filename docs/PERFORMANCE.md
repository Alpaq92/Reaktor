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
| Web bundle, total | 1,389,145 |

Everything links statically — SDL, libcss, plutosvg, plutovg, with Nuklear
compiled in — so nothing but the C runtime loads from outside the binary.

## Memory

Idle on the Login tab.

| | Windows | Linux |
| --- | --- | --- |
| Committed / private | median **9.0 MB** (8.0–9.9) | **4.3 MB** |
| Resident / working set | **29–34 MB** | **11.4 MB** |

Two different questions, not two estimates of one: committed is what the
process asked the system to back, resident is what is in RAM now including
shared library pages that are not this process's to give back.

Linux was **123.6 MB** until SDL's OpenGL drivers were compiled out. SDL's X11
driver initialises GL whatever renderer you ask for, and on a machine without a
GPU that GL is Mesa's llvmpipe — 53 MB of libLLVM, 10 of libgallium, 30 of
heap, none of it Reaktor's. `-DREAKTOR_SDL_GL=ON` puts it back.

Windows spreads about 2 MB between identical runs, so a single reading against
a single reading means nothing; ten launches per arm resolve about 1 MB.

## CPU

**At rest: 0% of one core**, on both platforms, every tab. Not "low" — nothing.

Forced to redraw at a fixed rate, 960x680:

| Page | Windows | Linux |
| --- | --- | --- |
| Login | 6.8–6.9 ms/frame | 6.2–6.7 ms/frame |
| Buttons (the busiest) | 11.5–12.2 ms/frame | 11.7–13.0 ms/frame |

The same rasteriser on both, so the agreement is the expected result rather
than a coincidence. Cost is linear in frames: a pointer swept across a row of
buttons draws one per hover crossing, typing draws one per keystroke.

For contrast, the one figure that made the software default worth it: on
Windows with no GPU, `direct3d11` falls back to WARP and the same frame costs
**78 ms**.

The accessibility tree adds **4.2 µs** to a drawn frame (82 nodes), which is
below the noise in the measurements above.

## macOS

**Not measured.** The app has never been built or run there — the
NSAccessibility bridge is written and unexecuted, and no size, memory or frame
figure on this page has a macOS column because there is no machine here to
produce one. Expect it to sit close to the Linux numbers: the renderer is the
same software rasteriser, and Metal is never reached.

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
GPU. Both at 1.00x scale in a 960x680 window. The **Diagnostics** tab reports
the renderer, the frame breakdown and the resident set live, which is where to
reproduce any of this.
