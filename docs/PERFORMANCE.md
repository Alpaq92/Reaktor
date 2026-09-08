# Performance

Every number here was measured. Where a figure is an expectation it says so.

**Measured on:** Windows 11 Pro (26200), x64, MSVC Release, one display at
1.00x, 960x680 window — inside a **VirtualBox VM with no hardware Direct3D 11
adapter**, so SDL's `direct3d11` backend runs on **WARP, Microsoft's software
rasteriser**. That is not a footnote, and it is stated once here rather than
repeated below: texture memory is process memory on this machine rather than
VRAM, so it counts in every figure and would not on a GPU, and frame time is
CPU rasterisation, so treat present as an upper bound.

Reproduce any of it from the **Diagnostics** tab.

## Size

| Artefact | Bytes |
| --- | --- |
| `reaktor.exe` | 2,406,400 |
| `reaktor.wasm` | 1,205,355 |
| `reaktor.js` | 76,228 |
| `reaktor.data` | 47,263 |
| `reaktor.html` | 1,019 |
| **Web bundle, total** | **1,329,865** |

SDL, libcss, yutil, plutosvg and plutovg link statically and Nuklear compiles
in, so nothing but the C runtime loads from outside the binary. Trimming SDL to
the subsystems this app uses, and dropping the render backends it can never
select, took it from 3.39 MB.

The web bundle was 3.55 MB before `-Oz` with `--closure 1` and packaging only
the asset files the sources actually name — `reaktor.data` alone fell from
1,868,661 bytes.

## Memory

Idle on the Login tab, twenty launches of the same binary:

| Counter | Value |
| --- | --- |
| Private bytes (commit) | median 9.0 MB, range 8.0–9.9 |
| Working set | 29–34 MB |

Two different questions, not two estimates of one. **Private bytes is commit** —
the pages this process asked the system to back. **Working set is residency** —
what is in RAM now, including the shared pages of every DLL and driver mapped
in, which are not this process's to give back.

**The spread between identical runs is about 2 MB.** With a standard deviation
near 0.8 MB, ten launches per arm resolve a difference of roughly 1.0 MB and
five resolve 1.4 MB. A single reading against a single reading carries no
information — an earlier revision of this document made exactly that mistake,
crediting a build change with 0.9 MB on one run each way.

### What the app itself allocates

| Allocation | Size | Where it lives |
| --- | --- | --- |
| Font atlas (1024x128, 8-bit indexed) | 128 KB | A texture: VRAM on hardware, process memory here |
| Icon cache | 18 KB over 4 rasters; ~120 KB after every tab | Same |
| Nuklear command buffer | 4–8 KB | Heap |
| `struct App`, style cache, context | ~60 KB | Heap |

Everything this codebase can account for, image private pages included, comes
to roughly **0.45 MB of the 9.0**. The other 95% is the graphics driver, the C
runtime, the loader and twenty statically-imported DLLs. It is not a budget
with slack in it.

An audit looking for savings produced fourteen candidates and zero survivors.
The four reasons they died:

1. **Freeing after the peak does not lower commit.** Private bytes is a
   high-water quantity for small blocks: `free` returns a 27 KB block to a free
   list inside pages that stay committed. Only allocations above the NT heap's
   ~512 KB threshold are genuinely returned.
2. **`.text`, `.rdata`, `.rsrc` and untouched `.bss` cost zero private bytes.**
   They are clean and file-backed. The entire writable section of `reaktor.exe`
   is 53,616 bytes, which caps what any code-size change could move.
3. **GPU allocations are not process heap** — on hardware. Here they are, per
   the note at the top, which is why the atlas work paid at all.
4. **Kilobyte-scale buffer shrinks buy a rendering regression.** Three separate
   proposals made a silent-truncation failure reachable where it is not.

### What startup costs

| Step | Private | Resident |
| --- | --- | --- |
| Before `main` — CRT, loader, the image | 1.4 MB | 7.4 MB |
| `SDL_Init(SDL_INIT_VIDEO)` | +0.8 MB | +3.6 MB |
| Window and renderer | +1.2 MB | +10.4 MB |
| Window icon — first touch of plutosvg | +0.1 MB | +0.3 MB |
| Nuklear context | +0.0 MB | +0.0 MB |
| Font atlas | +0.5 MB | +0.4 MB |
| Stylesheets | +0.1 MB | +0.7 MB |
| **End of `SDL_AppInit`** | **4.2 MB** | **23.0 MB** |

The window and renderer step maps in 10.4 MB and commits 1.2 of it; the rest is
`d3d11.dll`, `dxgi.dll` and the user-mode driver.

### Where the private bytes are

`tools/vmwalk.c` walks another process's address space and buckets every
committed region — the only way to see the 95% the app cannot. Build it with
`cmake --build build --target vmwalk`, then `build/vmwalk.exe <pid>`. It is
read-only and out-of-process, so measuring does not move what is measured.

| Bucket | MB |
| --- | --- |
| private read/write (heap, arenas) | 6.5 |
| image pages, copy-on-write dirty | 1.25 |
| thread stacks (commit) | 0.30 |
| private executable (JIT/shader) | 0.04 |
| **attributed** | **8.14** |
| **`PrivateUsage` (ground truth)** | **9.02** |
| **unattributed** | **0.89** |

Three things that settles:

- **The unattributed 0.89 MB is stable to two decimals** across every run and
  window size. Not slop: it is what `PrivateUsage` counts and `VirtualQuery`
  does not expose — page tables and per-process structures.
- **1.25 MB is copy-on-write image pages** over about fifty DLLs, 8–105 KB
  each; `reaktor.exe` contributes 47 KB. Loader cost, and nothing here reduces it.
- **Twelve threads, none of them ours.** SDL, COM and the graphics stack create
  them. 0.30 MB of stack commit against 11.7 MB of reserve — the number to
  quote whenever someone proposes `/STACK`.

### What using the app costs, and why 97% of it is not ours

| | Private bytes |
| --- | --- |
| End of `SDL_AppInit` | 4.2 MB |
| First frame drawn, then idle | 8–10 MB |
| After sweeping every tab six times | **14–16 MB**, then flat |

**Roughly 8 MB of rise, of which about 254 KB is this application's — 3%.**
After a full sweep the app's own accounting reads: command buffer 8 KB, icon
cache 118 KB in 18 rasters, atlas 128 KB, all bounded by construction.
`vmwalk` puts the rest in the heap and the graphics stack's arenas.

It is a one-time ramp, not a leak, and four checks say so:

- **It plateaus.** Eighty theme switches and six further sweeps move it no
  further.
- **It is not new.** The commit before this work behaves the same or worse:
  10.8 MB idle, 17→21 MB after sweeping, and still climbing.
- **It is not the D3D11 path.** SDL's own software renderer shows the same
  shape — 9.9 → 16.0 MB.
- **It is not page complexity.** The two simplest tabs cost +6.1 MB, the two
  heaviest +1.5 MB. A warm-up, not a function of what is drawn.

The three extra threads are the Windows thread pool — all start at the same
`ntdll` address as nine that were already there — and cost 0.07 MB between them.

### Growth over time

Memory is flat in use, and checked rather than assumed: eighty scheme switches
followed by six sweeps of all seven tabs settle and stay there. Worth re-running
after any change to the style or icon paths — before the libcss rule store was
rebuilt per load, the same test climbed from 7.9 to 15.5 MB without levelling
off.

## CPU

**At rest: 0.00% of one core**, every tab, foreground or background, over
hundreds of sampled seconds. That is not "low", it is nothing: SDL waits for
events and `SDL_AppIterate` returns without drawing unless something changed.
(A callback rate that is numeric rather than `waitevent` but draws nothing
costs 1.4–2.0% — the loop itself is nearly free.)

**A drawn frame costs 7–12 ms of CPU on this machine** — 7 on the login card,
12 on the busiest page — now that `auto` takes SDL's software renderer when the
adapter is WARP. Left to Direct3D it was **78 ms**, and almost none of that was
Reaktor's: with no GPU, `direct3d11` runs on WARP and the frame is rasterised in
software by the display stack, inside this process, on Windows thread-pool
threads — the main thread was 3.6–4.7% of it and `ntdll`'s threads ~83%.
The renderer section of [DEVELOPMENT.md](DEVELOPMENT.md) is the account of
making the cheaper path draw the same page. Measured by forcing a redraw at
fixed rates:

| Forced rate | Software, Login | Software, Buttons | WARP (any page) |
| --- | --- | --- | --- |
| 30 fps | 20% of one core, 6.8 ms/frame | 37%, 12.2 ms/frame | 200%+, *saturated* |
| 60 fps | 41%, 6.9 ms/frame | 69%, 11.5 ms/frame | *saturated* |
| 5–10 fps | | | 38–85%, 75–85 ms/frame |

Linear in frames either way. (An earlier note here read "the same with
`REAKTOR_RENDERER=software`, 83 vs 85 ms, which rules the SDL renderer out"; that
was wrong — `REAKTOR_RENDERER` did not accept `software` at the time, so both arms
measured WARP twice.)

The consequence is still that **CPU is a count of frames**: a pointer swept
along a row of buttons draws a frame per hover crossing, typing is a frame a
keystroke, a drag is a frame a tick at the display's refresh. All intended, all
invisible on a GPU, all worth counting here. Three things were found by
counting, all fixed, all in [DEVELOPMENT.md](DEVELOPMENT.md), and re-measured
on the software default:

- A drag whose button-up never arrived left the app drawing at the display's
  refresh forever — **171% of a core** on WARP, reproducible by taking focus
  away mid-press. Now every scenario measured — a sweep, clicking every tab,
  press-in and release-outside — returns to **0.00%**.
- Pointer-driven frames are coalesced, and the gap adapts to what a frame
  measurably costs. Sustained waving over the Buttons page: 27% of four cores
  before the coalescing on WARP, 11–16% after, **3% on the software default**;
  over Login, 19% → 5–8% → **0.8%**. The table of gaps against CPU is in
  DEVELOPMENT.md.
- A button held still over something that cannot change under it drew sixty
  identical frames a second — 42% of four cores on WARP, 11–18% after
  coalescing, **3.9% on the software default**.

Read those ranges as ranges: three runs of the same measurement on this VM
spread by half again, so a single reading either way means nothing.

Where a drawn frame's *wall-clock* time goes on the main thread — which is what
Diagnostics has always shown, and why the WARP cost stayed invisible for so
long: build 1.8 ms, render 0.3 ms, present 0.5 ms, and none of it was where the
62–78 ms went. Diagnostics shows the process-wide figure beside them.

| Phase | Time |
| --- | --- |
| Build (running the UI code) | 1.3–1.9 ms |
| Render (Nuklear commands to geometry) | 0.4–0.9 ms |
| Present | 1.0–13 ms |

While a button is held the callback rate is the display's refresh rather than
uncapped: presenting faster than the display accepts fills the swapchain and
present blocks on it — 29 ms a frame against 0.11 ms of building, holding a
drag at 35 fps.

Parsing both stylesheets takes 1.5–4.0 ms, at startup and on each scheme change.

## GPU

Modest by construction: one atlas texture plus one per rasterised icon, nothing
uploaded per frame, a few hundred triangles. No shader, no framebuffer of its
own, no compute — everything goes through `SDL_Renderer`. Anti-aliasing is
Nuklear's, on the CPU, and `REAKTOR_AA=0` turns it off.

On hardware this should mean lower private bytes, a smaller present time, the
texture rows leaving the memory table — and the 78 ms of CPU per frame above
going to a few hundred microseconds, because a GPU presents and this machine
rasterises. An expectation, not a measurement.

## System requirements

Windows is the only row with measured numbers; the rest follow from the build
configuration and SDL's own requirements.

| Platform | Renderer | Status |
| --- | --- | --- |
| Windows 10/11, x64 | Direct3D 11, or OpenGL if unavailable | **Measured** |
| macOS | Metal | *Expected* |
| Linux, x64/ARM | OpenGL or OpenGL ES, X11 or Wayland | *Expected* |
| FreeBSD and the other BSDs | OpenGL, X11 or Wayland | *Expected* |
| Browser | WebGL 2, required by the build | **Measured** for size only |

Filling those in is a matter of access to the machines, not tooling.

The requirement is modest either way: a 2.7 MB binary that draws a few hundred
triangles and idles at zero, needing about 10 MB of RAM plus whatever the
platform's graphics driver takes. A machine that can run a browser can run it.

Two things a deployment needs:

- **The asset files.** A native build reads tiny.css and the Ionicons SVGs from
  disk at runtime, resolved against the `.reaktor-root` marker, so they ship
  beside the binary or `REAKTOR_ROOT` points at them. The web build packages them
  into `reaktor.data`.
- **A served web build.** A `file://` page cannot fetch the `.wasm`.
