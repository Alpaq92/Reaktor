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

Unless a figure says otherwise, it was measured on 2026-11-10 on master for
0.11.0, on the Windows machine in
[Where the numbers came from](#where-the-numbers-came-from). There,
`showcase.exe` is 3,385,344 bytes, `notepad.exe` 2,376,192, `simple.exe`
2,233,344 and `bench.exe` 2,217,984, linked with
[link-time optimization](#size). The showcase links all nine optional
modules, `notepad` three (settings, popup windows, printing) and the other two
none; each carries the default look compiled in: Aileron, the mark, the sheets
and seven icons, 73,123 bytes.

## Against the others

![Two bar charts of drawing 64 rotating boxes at 60 fps in an 800x600 window.
Memory: Reaktor 8.8 MB, the same program on D3D12 120.5 MB and on OpenGL
68.7 MB, Flutter 132.7 MB, Electron 155.2 MB, Kotlin
Multiplatform 668.7 MB. Steady-state CPU as a percent of one core: Reaktor
6.0%, D3D12 5.2%, OpenGL 5.9%, Flutter 15.3%, Electron 20.7%,
Kotlin Multiplatform 29.4%](benchmark.svg)

The workload is [ShaftUI/Shaft](https://github.com/ShaftUI/Shaft)'s: 64 boxes
rotating at 60 fps in an 800×600 window. Each arm is its own app, built with
its framework's toolchain; one script launches it and samples its whole
process tree from outside. [`benchmarks/`](../benchmarks/BENCHMARKS.md) has
the projects and the runner. Reaktor appears once per `--renderer`. Shaft is
not plotted: it does not link on Windows.

| Arm | Memory | CPU, with startup | CPU, steady state | Measured |
| --- | --- | --- | --- | --- |
| **Reaktor**, `software` | 8.8 MB | 7.0% | 6.0% | 2026-11-11 |
| **Reaktor**, `direct3d11` | 57.6 MB | 14.4% | 11.7% | 2026-11-11 |
| **Reaktor**, `direct3d12` | 120.5 MB | 10.6% | 5.2% | 2026-11-11 |
| **Reaktor**, `opengl` | 68.7 MB | 11.1% | 5.9% | 2026-11-11 |
| Electron 44.3.0 | 155.2 MB | 36.1% | 20.7% | 2026-11-09 |
| Flutter 3.47.8 | 132.7 MB | 38.8% | 15.3% | 2026-10-24 |
| Kotlin Multiplatform | 668.7 MB | 51.5% | 29.4% | 2026-10-24 |

- **About a fifteenth of Flutter's memory and less than half its CPU**, with
  startup or without, and about a seventy-fifth of Kotlin Multiplatform's
  memory.
- **Every Reaktor renderer takes less CPU than every other framework.** On a
  GPU renderer its memory is mostly the graphics driver's: 58–121 MB, below
  Flutter's 133.
- **Once running, `direct3d12` takes 0.8 points less than `software` and
  `opengl` about the same; with startup, they take 3.6–4.1 more.**
  `direct3d11` takes the most of the four either way.
- **`software`'s memory lands on one of two levels.** Its runs read
  8.1–8.3 MB or 9.1–9.2 MB, 16 of the 40 the lower, so the median falls
  between. Master before #139 does the same: twelve passes of the two taken
  in turn gave each a median of 9.1 MB. The passes of 2026-11-10 read 9.1,
  those of 2026-11-09 8.4.
- **About 2.2 MB of every arm is the antivirus.** Bitdefender puts two
  components into every process on this machine. Walked with VirtualQueryEx
  and a heap ledger on `bench` on 2026-11-04, they come to two private heaps
  of 776 KB each, 74 code pages patched with hooks (303 KB), 56 more copied
  unchanged (229 KB), 53 KB of trampolines and about 180 KB of pages beside.
  Subtract that to compare with a machine without it. Of the rest, 1.9 MB is
  the window's framebuffer, 1.75 MB system DLL data made private, and 1.1 MB
  Reaktor's, SDL's, Nuklear's and FreeType's own allocations, the glyph atlas
  (537 KB) the largest.
- **A GPU renderer's CPU depends on what else uses the GPU.** With another
  program keeping it busy, `direct3d11` and `opengl` missed 60 fps and took
  16–28% of a core (2026-10-24). These runs had the GeForce RTX 2060 idle.

### How it was measured

```powershell
./benchmarks/run.ps1 -Targets <arm> -Seconds 6 -Fps 60 -Runs 1 -PinClock
./benchmarks/run.ps1 -Targets <arm> -Fps 60 -Runs 1 -Steady -PinClock
```

Forty passes of Reaktor's arms, twenty in each of two sessions, each running
every arm once in each mode, the order of the arms rotated and reversed from
pass to pass so none always runs first. Memory is
peak private bytes and CPU the sum across each app's process tree, as a
percent of one core: with startup over the whole 6-second run, steady from
3 s to 8 s of a 9-second run. Every arm held 60 fps; the fewest frames a
run drew were 356 of 361. Each figure is the median of the forty runs.
Between runs of one arm, memory spread by up to 2.5 MB (`direct3d12`
7.3 MB) and steady CPU by several points: the middle half of `software`'s
runs read 5.6–6.7%, `direct3d11`'s 9.3–13.5%, `direct3d12`'s 4.2–6.1% and
`opengl`'s 4.9–6.5%. A whole session moves too: from the same binaries,
`direct3d11` read 9.7% steady in the first and 12.8% in the second, and
the passes of 2026-11-10 read 9.1% for `direct3d11` and 5.0% for
`opengl`. `-PinClock` runs
on a copy of the power plan that holds the processor at full speed, and
restores the plan after.

Reaktor's `software`, `direct3d11` and `direct3d12` arms are the default
Release build; `opengl` is a build with
`-DREAKTOR_RENDERERS=direct3d11,direct3d12,opengl`. `vulkan` falls back to
`direct3d11` in this SDL build. Electron keeps its figures of 2026-11-09,
twenty passes of the same script taken with Reaktor's arms then; the middle
half of its steady runs read 19.0–22.1% and its memory spread by 5.5 MB.
Flutter and Kotlin Multiplatform keep theirs of 2026-10-24, from the same
machine and script with ten runs each: Flutter 3.47.8, and Kotlin 2.4.20 with
Compose 1.12.0 on JDK 21, which held 59 fps.

Run the script from PowerShell: started through Git Bash, the same `bench`
reads about 3 MB less private memory (2026-11-07).

## Interaction

Driving the showcase with input, 0.11.0 draws its frames in 42–43% less
time each than 0.10.0, 22% less on the wheel, most of it saved in
rendering; the process spends 13–31% less CPU on hover, slider drag and
typing, and 15% less on the wheel. A harness build of the
showcase feeds it synthetic SDL events every 8 ms after two seconds of
warm-up, on `software` at 1100×680, vsync off, light theme, with the clock
pinned as above:

- **Hover**: the pointer sweeps four rows of the Buttons page, 3 px a step.
- **Slider drag**: on Inputs, a sweep over the fields, the Float slider
  dragged back and forth, then down and up the sidebar.
- **Typing**: a word typed into a field and erased, eight times, a key every
  48 ms.
- **Wheel**: 60 notches down and 60 up, twice, one every 32 ms.

Twelve pairs per workload, each pair one run of 0.10.0 and one of 0.11.0,
which runs first alternating. Medians of the twelve:

| Workload | Frames drawn | A drawn frame, 0.10.0 | 0.11.0 | Process CPU, 0.10.0 | 0.11.0 |
| --- | --- | --- | --- | --- | --- |
| Hover | 40, 38 | 3.38 ms | **1.93 ms** | 8.9% | **7.7%** |
| Slider drag | 344, 333 | 3.18 ms | **1.83 ms** | 17.2% | **12.8%** |
| Typing | 128 | 3.36 ms | **1.96 ms** | 14.2% | **9.7%** |
| Wheel | 208 | 3.40 ms | **2.65 ms** | 15.4% | **13.2%** |

Where two counts of frames drawn are given, the first is 0.10.0's: on hover
and slider drag 0.11.0 draws a few fewer for the same input, as master did
before #139. A drawn frame is build, render and present as the runtime
times them; CPU is the whole process over the script. Every pair drew the
frame faster on 0.11.0. Rendering is 52–57% shorter on hover, slider drag
and typing, 25% on the wheel, whose frames redraw most of the window; build
is 24–39% shorter. Present is no shorter. Process CPU was lower on 0.11.0
in every slider and typing pair and ten of twelve hover and wheel pairs.

## Memory

Idle, ten seconds after launch with the pointer outside the window; medians of
ten launches on 2026-11-10, 0.11.0 taken in turn with 0.10.0. Master before
#137 was measured the same way earlier that day:

| | Private bytes | Working set | 0.10.0, private bytes | Before #137 |
| --- | --- | --- | --- | --- |
| `showcase` | **12.7 MB** | 31.7 MB | 12.9 MB | 15.8 MB |
| `notepad` | **9.0 MB** | 25.3 MB | 9.1 MB | 10.6 MB |
| `simple` | **6.6 MB** | 19.8 MB | 6.9 MB | 7.6 MB |

Private bytes are what the process asked the system to back; the working set
is what is in RAM, shared pages of system DLLs included. Launches spread by up
to 1.0 MB of private bytes. The showcase opens
on its Login page; its figure includes a second copy of the glyph atlas,
because its splash is a window of its own and the first such window keeps its
atlas for the next.

- **Windows is mostly the system's.** Reaktor, SDL, Nuklear and FreeType
  allocate 1.1 MB in `bench`; most of the showcase's private bytes are the
  heaps of the Windows components it loads: UI Automation, the shell, the
  input method.
- **Bitdefender is in every Windows figure here**, about 2.2 MB of each;
  [the chart's notes](#against-the-others) have the split.

`tools/vmwalk.c` (`cmake --build build --target vmwalk`) splits another
process's private bytes into named buckets.

### On Linux

**Linux once started at 123.6 MB, almost none of it Reaktor's.** SDL's X11
driver set up OpenGL whatever the renderer, and with no GPU that loads Mesa's
software OpenGL and LLVM: 53 MB of libLLVM, 10 of Mesa, 30 of heap.
Compiling SDL's GL drivers out brought that build to 4.3 MB;
`-DREAKTOR_RENDERERS=opengl` puts them back.

**Three apps share 21% at most.** `tools/ci/pss.sh` runs `showcase`,
`notepad` and `simple` together, idle, on a GitHub `ubuntu-24.04` runner;
medians of ten launches. Each carries its own copy of libreaktor and SDL,
which a shared library would map once.

| | PSS | Clean pages of its binary |
| --- | --- | --- |
| `showcase` | 9,180 KB | 2,796 KB |
| `notepad` | 5,957 KB | 2,208 KB |
| `simple` | 5,251 KB | 2,164 KB |

Of the 20,433 KB together, one copy instead of three would save 4,366 KB, or
21.3%. That counts each app's own code as library code, so it is an upper
bound. Counted page by page, it is 20.2%: `tools/ci/pss-pages.sh` links the
three with maps and sorts each resident page of a binary by the object it came
from, and sharing would save 4,120 KB of the 20,423. That still takes the
largest app's library pages as all three's, and leaves out what the dynamic
linker costs each app.

On a Linux Mint 22.3 desktop (Cinnamon on X11, llvmpipe, a KVM guest with 4
cores of the Ryzen 5 4600H) it falls under 20%: `showcase` grows to
14,224 KB, while the library pages barely move. Counting every clean page of a
binary, sharing saves 4,602 KB of 24,157 KB, or 19.0%; page by page,
4,176 KB of 24,200 KB, or 17.3%.

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

## CPU

**At rest: 0% of a core** on Windows, Linux and GhostBSD, on every page; macOS
settles around 0.3–0.5%, as CoreAnimation wakes the loop now and then for an
iteration that draws nothing.

**Without a GPU, `software` beats Direct3D.** `direct3d11` then falls back to
WARP, which rasterizes on threads of its own: drawing `bench` flat out, the
process spent 1.78 ms of CPU a frame on WARP against 0.75 ms on `software`
(medians of five runs, 2026-10-24), which made software the default.

**The accessibility tree adds 3.3 µs to a drawn frame** with 82 nodes
(2026-10-23).

## Size

Everything links statically (SDL, plutosvg, plutovg, Nuklear, Onlay,
FreeType, and mojibake and kb_text_shape with Text), so only the C runtime
loads from outside. The Windows binary's icon is a 125 KB resource of nine
sizes, built from the SVG by `tools/mkicon.c`.

Nuklear is compiled with `-Os` (`/Os` with MSVC): the stripped shared
libreaktor on Linux x86-64 goes from 1,880,624 bytes at `-O3` to 1,737,264,
and Nuklear's own code from 306,649 to 168,042. `bench --no-vsync --video
dummy` on the Linux Mint desktop above, median of seven runs, CPU time a frame:

| Build | `-O3` | `-Os`, three rounds |
| --- | --- | --- |
| shared | 563 µs | 574, 563, 528 µs |
| static | 527 µs | 548, 523, 511 µs |

The rounds spread wider than the change. Every showcase tab in both themes,
`notepad` and `simple` draw and dump byte for byte as they did at `-O3`.

**With MSVC, a Release build is link-time optimized** (`REAKTOR_LTO`),
Reaktor and SDL compiled as one program. `showcase.exe` grows from
3,234,304 bytes to 3,385,344 (4.7%). Without it, SDL's software rasterizer
ran faster or slower as the code linked before it changed size; with it,
on 2026-11-10 against 0.10.0, `bench --no-vsync` renders a frame 8.9%
faster (every one of twelve runs) and the showcase's pages, drawn every
frame, 4–8% faster. Pixels and `--a11y-dump` are unchanged. Its static
library holds objects only the same compiler can link, so
`tools/libreaktor-pkg.sh` and the Windows and macOS releases build one with
it off.

**With Apple Clang it is on as well**; with GCC and Clang on Linux it stays
off. On GitHub runners on 2026-11-10, a CI job (since removed) built each
twice and ran `bench --renderer software --no-vsync` ten times each way, in turn; shots and dumps were the same both
ways:

| Compiler | Render, 64 boxes | 512 boxes | `showcase` |
| --- | --- | --- | --- |
| GCC 12.2, Linux x86-64 | 0.41 → 0.38 ms (10 of 10 faster) | 0.65 → 0.61 ms (10 of 10) | +84,936 bytes (2.0%) |
| Clang 14.0, Linux x86-64 | 0.22 → 0.22 ms (2 of 10) | 0.35 → 0.34 ms (9 of 10) | +342,264 bytes (8.6%) |
| Apple Clang 17.0, macOS arm64 | 0.59 → 0.56 ms (9 of 10) | 0.865 → 0.795 ms (9 of 10) | +225,120 bytes (6.8%) |

With it, GCC also warns 69 times more, in kb_text_shape and FreeType, and
Clang on Linux gains nothing for its size. Apple Clang was measured on arm64
only.

SDL's GPU API is left out unless `REAKTOR_SHADERS` is on. Built in, it costs
the software renderer too: SDL then loads the Vulkan loader, every installed
Vulkan driver and LLVM, and the Diagnostics page's "window and renderer" step
goes from +0.2 MB private to +34.4 MB. On the same desktop, the shared build,
`bench --no-vsync`, five runs each, both on the software backend:

| `REAKTOR_SHADERS` | A frame on the display | Private |
| --- | --- | --- |
| `OFF` | 2.08–2.35 ms | 5.0 MB |
| `ON` | 13.59–16.31 ms | 63.1 MB |

The stripped `libreaktor-sdl3.so` is 2,659,344 bytes without it and 2,950,704
with it. With `--video dummy`, where nothing reaches a display, the frame costs
563–622 µs of CPU without it and 588–740 µs with it.

### Modules

`showcase.exe` on 2026-11-10, each module off in turn:

| | Default | Module off | Saved |
| --- | --- | --- | --- |
| Accessibility bridges | 3,385,344 | 3,366,400 | 18,944 (0.56%) |
| Locale | 3,385,344 | 3,361,280 | 24,064 (0.71%) |
| Text | 3,385,344 | 2,764,288 | 621,056 (18.3%) |

**The bridges are small**; turned off, they drop libraries more than bytes.
Pixels and `--a11y-dump` stay byte-identical, and only a screen reader
notices: UI Automation finds 99 elements on the Buttons page, none without
them.

**Text is mostly tables and a font.** kb_text_shape is 95 KB of code and
391 KB of tables, and mojibake 19 KB of code and 180 KB of line-breaking and
bidirectional data; on the web, the `.data` it adds is M PLUS 1p, 1,758,688
bytes. Locale is 24 KB of the executable and 7,581 bytes of catalogs;
compiled in (`-DREAKTOR_LOCALE_EMBED=ON`), `showcase.exe` is 3,393,024 bytes.

## What it needs to run

**No GPU**, and **7–13 MB of RAM** on Windows. The default look is compiled
in, so a program needs no files beside it until it uses other Ionicons,
Simple.css, the catalogs or the Japanese font, found under a `.reaktor-root`
marker beside it or up to two folders above. The web build packs those into
its `.data` and needs an HTTP server, since a `file://` page cannot fetch the
`.wasm`.

## Where the numbers came from

- **Windows**: a Lenovo laptop with an AMD Ryzen 5 4600H (6 cores, 12
  threads), a GeForce RTX 2060 and a 1920×1080 120 Hz display at 100%
  scaling, on Windows 11 Pro (build 26300), x64. MSVC 14.44 (Visual Studio
  2022 Build Tools) Release builds, link-time optimized; **Against the others** in the 800×600
  window every arm asks for.
- **Linux**: the Linux Mint 22.3 desktop above, and for **Three apps share**
  a GitHub `ubuntu-24.04` runner, x86_64, in a Debian 12 container, GCC
  Release, Xvfb, no GPU.

The **Diagnostics** page shows the renderer, the frame breakdown and the
resident set live; reproduce any of this there.
