# Performance

Every number below was measured, not estimated. Where a figure is an
expectation rather than a measurement it says so.

**Measured on:** Windows 11 Pro (26200), x64, MSVC Release, one display at
1.00x scale, 960x680 window — inside a **VirtualBox VM with no hardware
Direct3D 11 adapter**, so SDL's `direct3d11` backend is running on **WARP,
Microsoft's software rasteriser** (`d3d10warp.dll` is loaded and is the
most-written module in the process; `auto`, `gpu` and `software` all land
within noise of each other).

That is not a footnote. It changes how two whole sections below should be
read:

- **Texture memory is process memory here.** The font atlas, the icon textures
  and the swapchain are WARP allocations in this address space, not VRAM. On a
  machine with a real GPU they would move off the private-bytes figure
  entirely, and the numbers would be lower.
- **Frame time is CPU rasterisation, not a GPU wait.** Treat the present figure
  as an upper bound.

Every figure here is from that machine. See
[System requirements](#system-requirements) for the other platforms, and take
"hardware GPU" as another unmeasured row.

Reproduce any of it from the **Diagnostics** tab, which reports the renderer,
the frame breakdown, the memory and the startup cost of each step.

## Size

| Artefact | Bytes |
| --- | --- |
| `curie.exe` | 2,686,976 |
| `curie.wasm` | 1,194,540 |
| `curie.js` | 76,222 |
| `curie.data` | 36,215 |
| `curie.html` | 1,019 |
| **Web bundle, total** | **1,307,996** |

The binary links SDL, libcss, yutil, plutosvg and plutovg statically and
compiles Nuklear in, so nothing but the C runtime is loaded from outside it.
Trimming SDL to the subsystems this app uses — no audio, joystick, haptic,
HID, sensor, camera, power, GPU, tray or offscreen — and dropping the render
backends it can never select took it from 3.39 MB to its present size.

The web bundle was 3.55 MB before two changes: `-Oz` with `--closure 1`, and
packaging only the asset files the sources actually name rather than the
directories they live in. `curie.data` fell from 1,868,661 bytes to 36,215 on
its own.

## Memory

Steady state after startup, on the Login tab, over ten launches of the same
binary:

| Counter | Value |
| --- | --- |
| Private bytes (commit) | median 9.1 MB, range 8.1–10.7 |
| Working set | 29–34 MB |

Read those as two different questions, not two estimates of one. **Private
bytes is commit** — the pages this process has asked the system to back, its
own. **Working set is residency** — what is in RAM right now, including the
shared, file-backed pages of every DLL and driver mapped into the process,
which are not this process's to give back. A change can move one and not the
other, and most changes here move neither.

**The spread between identical runs is about 2 MB**, from the Direct3D 11
driver's own heap and the NT heap's segment behaviour. That is the honest
ceiling on tuning: with a standard deviation near 0.8 MB, ten launches per arm
resolve a difference of roughly 1.0 MB, and five per arm resolve 1.4 MB.
**A single reading against a single reading carries no information at all.**
Anything smaller than a megabyte cannot be measured by this method, and this
document has been wrong that way before — an earlier revision credited a build
change with 0.9 MB of private bytes on one run against one run.

What the application itself allocates:

| Allocation | Size | Where it lives |
| --- | --- | --- |
| Font atlas (1024x128, 8-bit indexed) | 128 KB | A texture. On real hardware, VRAM; **here, WARP's own process memory** — so on this machine it does count. |
| Icon cache | 19 KB over 4 rasters; ~80 KB after the Display tab | Same |
| Nuklear command buffer | 4 KB, growing to ~16 KB on the densest page | Heap |
| Application state (`struct App`) | ~32 KB | Heap |
| Style cache, Nuklear context, misc | ~30 KB | Heap |

The heap rows come to well under 0.1 MB, and everything this codebase can
account for — image private pages included — to roughly **0.45 MB of the
9.1**. The other 95% is the graphics driver, the C runtime, the loader and
the twenty DLLs statically imported before `main` runs. **It is not a budget
with slack in it.**

An audit that spent six independent investigations looking for savings, with
every proposal attacked by three verifiers, produced **fourteen candidates and
zero survivors**. The four reasons they died are worth keeping:

1. **Freeing after the peak does not lower commit.** Private bytes is a
   high-water quantity for small blocks: `free` returns a 27 KB block to a
   free list inside pages that stay committed. Only allocations above the NT
   heap's ~512 KB threshold are genuinely returned.
2. **`.text`, `.rdata`, `.rsrc` and untouched `.bss` cost zero private bytes.**
   They are clean and file-backed. The entire writable section of `curie.exe`
   is 53,616 bytes, which caps what any code-size change could possibly move.
3. **GPU allocations are not process heap** — *on hardware*. The atlas, the
   icon textures and the swapchain are driver allocations. On the WARP machine
   these figures were taken on that reason does not apply: they land in the
   private-bytes number, which is why the atlas work below paid here and would
   pay nothing on a real GPU.
4. **Kilobyte-scale buffer shrinks buy a rendering regression.** Three separate
   proposals made a silent-truncation failure reachable where it currently is
   not.

### What startup costs

Resident set sampled at each step, and the difference each one made:

| Step | Delta | Running total |
| --- | --- | --- |
| Before `main` — CRT, loader, the image itself | — | 7.4 MB |
| `SDL_Init(SDL_INIT_VIDEO)` | +3.7 MB | 11.1 MB |
| Window and renderer | +10.4 MB | 21.5 MB |
| Window icon — the first touch of plutosvg | +0.2 MB | 21.7 MB |
| Nuklear context | +0.0 MB | 21.8 MB |
| Font atlas | +1.5 MB | 23.2 MB |
| Stylesheets | +0.4 MB | 23.6 MB |

**Every figure in that table is working set, not private bytes** — it is
sampled with `GetProcessMemoryInfo`'s `WorkingSetSize`. Read it as what each
step *maps in*, not what it costs us. The window-and-renderer step is almost
entirely the graphics driver's own pages: `d3d11.dll`, `dxgi.dll` and the
vendor's user-mode driver enter the working set roughly one-for-one and enter
private commit only through their own dirtied data. Disabling the render
backends the app never selects is a real saving in binary size (3.39 MB →
2.69 MB); it is not a 0.9 MB private-bytes saving, and an earlier revision of
this document claimed that it was.

### What moved the number, and by how much

Three changes, measured over ten launches each on the same protocol:

| State | Median private bytes |
| --- | --- |
| Before | 10.64 MB |
| Atlas back to 1024x128 (oversampling off, after the typeface change doubled it) | 9.55 MB |
| Atlas 8-bit indexed instead of RGBA32 | 9.05 MB |

**Read the total, not the steps.** Each individual step is around 0.5 MB, which
is below what ten launches per arm can resolve; the cumulative 1.6 MB is above
it. And all of it is a property of this machine — see the WARP note at the top.
On hardware, the atlas is VRAM and none of this shows.

### Where the private bytes are, attributed

`tools/vmwalk.c` walks another process's address space and buckets every
committed region, which is the only way to say anything about the 95% the
application cannot see. Build and run it against a live instance:

```bash
cmake --build build --target vmwalk
```

`build/vmwalk.exe <pid>` — read-only and out-of-process, so measuring does not
move what is being measured. A representative run, idle on the Login tab:

| Bucket | MB |
| --- | --- |
| heap and loader | 2.4–5.2 |
| large private blocks (≥1 MB) | 1.7–5.7 |
| image pages, copy-on-write dirty | 1.25 |
| thread stacks (commit) | 0.29 |
| private executable (JIT/shader) | 0.02 |
| **attributed** | **~9.5** |
| **`PrivateUsage` (ground truth)** | **~9.5–10.6** |
| **unattributed** | **0.90** |

Four things that table settles:

- **The heap and the large-block buckets trade places with window size** —
  WARP's arenas get carved differently, and the 1 MB threshold that separates
  the two buckets is a property of the classifier, not of the process. Read
  their **sum**, around 8 MB, not either one.
- **The unattributed 0.90 MB is stable to three decimal places across every
  run and every window size.** That is not measurement slop; it is what
  `PrivateUsage` counts and `VirtualQuery` does not expose — page tables and
  the per-process structures.
- **1.25 MB is copy-on-write image pages** spread over about fifty DLLs, at
  8–105 KB each. `curie.exe` itself contributes 47 KB. It is the loader's
  cost, and nothing in this repository can reduce it.
- **Twelve threads, none of them ours.** The application creates no threads;
  SDL, COM and the graphics stack do. They commit 0.29 MB of stack against
  11.7 MB of reserve — which is the number to quote whenever someone proposes
  `/STACK`.

### What using the app costs — and why 97% of it is not ours

Idle figures describe a process nobody has touched. Using it costs considerably
more, and that is the number anyone watching Task Manager will actually see:

| | private bytes |
| --- | --- |
| End of `SDL_AppInit` | 4.2 MB |
| First frame drawn, then idle | 8–10 MB |
| After sweeping every tab six times | **14–16 MB**, then flat |

**Roughly 8 MB of rise, of which about 254 KB is this application's — 3%.**
After the full sweep the app's own accounting reads: Nuklear command buffer
8 KB, icon cache 118 KB in 18 rasters, font atlas 128 KB. All three are
bounded by construction — the icon cache caps at `IMG_CACHE_MAX` entries and
the command buffer at the busiest frame ever drawn. `vmwalk` puts the other
97% in the heap (+2.6 MB) and in large private blocks (+5.2 MB): the rendering
stack reaching its steady working set.

That conclusion is worth the five checks it took, because *"it grew, therefore
we leaked"* is the easy wrong answer and it was wrong five different ways:

- **It plateaus.** Eighty theme switches and six further sweeps move it no
  further. A one-time ramp, not a leak.
- **It is not new.** The commit before this document's memory work behaves the
  same or worse under the identical script — 10.8 MB idle, 17→21 MB after
  sweeping, and still climbing where the current build has levelled off.
- **It is not the D3D11 path.** SDL's own software renderer shows the same
  shape: 9.9 → 16.0 MB, heap +4.7 MB. So it is not WARP specifically, which
  was the first hypothesis and the wrong one.
- **It is not page complexity.** Sweeping only the two simplest tabs costs
  +6.1 MB; the two heaviest cost +1.5 MB. It is a warm-up, not a function of
  what is drawn.
- **The extra threads are not ours.** The process goes from 12 to 15 threads,
  and `vmwalk`'s thread listing shows all three new ones starting at the same
  `ntdll` address as nine that were already there — the Windows thread pool.
  They cost 0.07 MB of stack commit between them.

**What would move it is hardware, not code.** On a GPU those rasterisation
buffers become VRAM and should largely leave the private-bytes figure. That is
a prediction, not a measurement: nobody has run this on a machine with a
Direct3D 11 adapter. `build/vmwalk.exe <pid>` settles it in one run on anyone's
machine that has one.

### Growth over time

Memory is flat in use, and is checked that way rather than assumed. Eighty
scheme switches followed by six sweeps through all seven tabs:

```
start                  11.20 MB private
after 20 theme loads   10.44
after 40                9.55
after 60                9.52
after 80                9.52
after 6 tab sweeps      9.52
```

This is worth re-running after any change to the style or icon paths. Before
the rule store was rebuilt per load, the same test climbed from 7.91 to
15.50 MB and had not levelled off.

## CPU

| Situation | Cost |
| --- | --- |
| At rest, any tab | **0.00%** of one core over 8 s |
| Pointer sweeping across the Login page | ~2.8% of one core, median |

At rest the figure is not "low", it is nothing: SDL is told to wait for
events, and `SDL_AppIterate` returns immediately unless something has actually
changed. A frame is built when a widget changes state or the pointer crosses a
region registered as hot in the previous frame.

Where a frame's time goes, on a typical drawn frame:

| Phase | Time |
| --- | --- |
| Build (running the UI code) | 1.3–1.9 ms |
| Render (Nuklear commands to geometry) | 0.4–0.9 ms |
| Present | 1.0–13 ms |

Present is the vsync wait and dominates, which is the intended shape. While a
mouse button is held, the callback rate is set to the display's refresh rather
than uncapped: presenting faster than the display accepts fills the swapchain
and then `SDL_RenderPresent` blocks on it — measured at 29 ms a frame against
0.11 ms of building, which held a drag at 35 fps.

Parsing both stylesheets takes 1.5–4.0 ms, and happens at startup and on each
scheme change.

## GPU

Modest by construction. The application holds one atlas texture plus one
texture per rasterised icon, uploads nothing per frame, and submits a few
hundred triangles — Nuklear's geometry for the visible widgets. Anti-aliasing
is Nuklear's own, done on the CPU during the render phase, and
`CURIE_AA=0` turns it off.

There is no shader, no framebuffer of its own and no compute: everything goes
through `SDL_Renderer`, so the graphics API is whichever one SDL picked.

**None of this has been measured on a GPU.** The reference machine has no
hardware D3D11 adapter, so every figure in this document comes from WARP doing
the rasterisation on the CPU. What that predicts for real hardware — lower
private bytes, a smaller present time, the texture rows leaving the memory
table — is an expectation, not a measurement.

## System requirements

**Windows** is the only platform in this table with measured numbers. The rest
follow from the build configuration and SDL's own requirements, and are marked
as expectations until somebody runs them.

| Platform | Renderer | Status |
| --- | --- | --- |
| Windows 10/11, x64 | Direct3D 11, or OpenGL if D3D is unavailable | **Measured.** Everything above. |
| macOS | Metal | *Expected.* Builds with CMake; no measurements. |
| Linux, x64/ARM | OpenGL or OpenGL ES, X11 or Wayland | *Expected.* Same. |
| FreeBSD and the other BSDs | OpenGL, X11 or Wayland | *Expected.* SDL supports them; not built here. |
| Browser | WebGL 2 (required — the build sets both the minimum and maximum) | **Measured** for size only; not profiled. |

**Filling that table in is planned work.** The macOS, Linux and BSD rows stay
marked as expectations until somebody builds on each and runs the same
measurements the Windows row came from — the Diagnostics tab reports
everything needed, so it is a matter of access to the machines, not of
tooling. The browser row needs a profiling pass rather than just a size
check.

In every case the requirement is modest: this is a 2.7 MB binary that draws a
few hundred triangles and idles at zero. On the desktop it needs about 10 MB
of RAM and whatever the platform's own graphics driver takes on top. A machine
that can run a browser can run it.

Two things a deployment does need:

- **The asset files.** A native build reads tiny.css and the Ionicons SVGs
  from disk at runtime, resolved relative to the `.curie-root` marker, so
  those files have to ship beside the binary or `CURIE_ROOT` has to point at
  them. The web build is the exception: Emscripten packages them into
  `curie.data`.
- **A served web build.** A `file://` page cannot fetch the `.wasm`.
