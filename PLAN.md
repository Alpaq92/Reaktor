# Plan: libreaktor as a shared library

**Decision (2026-11-02): go.** Three apps together save 14.8%, under the 20%
threshold, but four save 20.4% and five 24.2% (Phase 0d), and Reaktor apps
are expected to run four or more at a time. Phase 1 starts. Static stays the
default.

## Where things stand

- **libreaktor is static only.** CMake merges its parts into `libreaktor.a`
  (`CMakeLists.txt`, the `libreaktor` target), and `tools/libreaktor-pkg.sh`
  packs it with the headers as a .deb, .rpm, BSD package or .tar.gz.
  `tools/ci/check-libreaktor.sh` links `examples/consumer` against it.
- **No shared-library code exists yet.** No `.so`, no soname, no shared SDL.
- **Static stays the default** whatever this plan decides. A shared library is
  offered beside it, not instead of it.

## Why consider it at all

An app is a few MB (stripped Linux showcase: 3.97 MB; 4.3 MB private and
11.4 MB resident at run time). On Windows and macOS each app ships alone, so
sharing buys little there. The gain is in Linux and BSD packages, where several
Reaktor apps could install one copy of the library and share its code pages in
memory.

Two copies of the *same* app already share their code, static or not. A shared
library only saves when *different* apps run together, and at most it saves
libreaktor's resident code per extra app. Heap, glyph atlases and stylesheets
stay private either way. So the saving may be small, and that is why Phase 0
measures it before anything is built.

## Decisions already made (2026-10-24)

1. **SDL: our own build, shared, under its own name** (option B), for example
   `libreaktor-sdl3.so`. Built as now: GL out, dynamic API off. Not the
   distro's SDL, because that:
   - can load Mesa and LLVM again (the 123.6 MB Linux start in
     `docs/PERFORMANCE.md`);
   - sets a minimum SDL version on every distro;
   - can draw different pixels per distro;
   - brings `SDL3_DYNAMIC_API` back.
2. **Nuklear's struct layouts are part of the ABI.** Apps reach them through
   the `reaktor/nuklear.h` macros and by reading `ctx->style`, `ctx->input`,
   `ctx->current` and `ctx->last_widget_state`. Hiding all of that would be a
   large job for no real gain, since Nuklear is a pinned submodule whose
   layout changes only when we bump it. Instead, a CI table of `sizeof` and
   `offsetof` values forces a new soname whenever a layout changes.
3. **Style access is settled.** Apps keep `ctx->style`. Three public calls
   cover the most common reads for clarity: `reaktor_item_spacing`,
   `reaktor_text_color`, `reaktor_current_font`. Done and merged (`5a8b7f3`).
4. **Platforms, in order:** Linux and the BSDs first. Windows (as an MSIX
   framework package) and macOS (same Team ID, or Homebrew only, since
   hardened-runtime library validation refuses other developers' dylibs) are
   in scope at lower priority.

## Phase 0: measure (go / no-go)

Goal: know what sharing would save before writing any of it. Needs no new
library code.

1. Build `showcase`, `notepad` and `simple` statically on Linux, as now.
2. Run the three together. For each process record, from
   `/proc/<pid>/smaps_rollup` and `/proc/<pid>/smaps`:
   - total `Pss`;
   - `Shared_Clean` and `Private_Clean` of the app's own binary mapping,
     which is roughly libreaktor's resident code.
3. Estimate the shared case: summed PSS minus the libreaktor code that the
   second and third app would no longer carry.
4. Ten launches, medians, as elsewhere in `docs/PERFORMANCE.md`. The numbers
   go there, with the machine named.
5. Put it in a script, `tools/ci/pss.sh`, run in the Debian container that
   `linux.yml` already uses, so it can be repeated.

**Go / no-go threshold (agreed 2026-10-30):** go if three different
apps running together save at least **20% of their summed PSS**. Disk space
does not count toward it; smaller packages would be a separate argument.

## Phase 0 results (CI, 2026-10-30)

**What was measured.** `showcase`, `notepad` and `simple` run together, idle,
for 10 seconds; ten launches, medians. For each app:

- **PSS** from `/proc/<pid>/smaps_rollup`: memory it has to itself in full,
  plus its share of pages it shares with other processes.
- **Library pages**: the resident pages of its own binary that hold libreaktor
  or SDL. Today each app links them statically, so each carries a copy.

A shared `libreaktor.so` would hold one copy for all three. The saving is the
three apps' library pages minus the largest app's, as a share of their summed
PSS.

**Machine.** GitHub `ubuntu-24.04` runner, x86_64, Debian 12 container, GCC
Release, Xvfb, no GPU.

**Two estimates.**

1. `tools/ci/pss.sh` counts every clean page of each binary as library, so it
   overcounts by the apps' own code: **21.3%**.

   | | PSS | Clean pages of its binary |
   | --- | --- | --- |
   | `showcase` | 9,180 KB | 2,796 KB |
   | `notepad` | 5,957 KB | 2,208 KB |
   | `simple` | 5,251 KB | 2,164 KB |

   Summed PSS 20,433 KB; sharing saves 4,366 KB.

2. `tools/ci/pss-pages.sh` links the three with linker maps and sorts each
   resident, file-backed page of a binary (from `/proc/<pid>/pagemap`) by the
   object it came from. Only a page holding nothing but libreaktor or SDL
   counts: **20.2%**.

   | | PSS | lib | app | mixed | other |
   | --- | --- | --- | --- | --- | --- |
   | `showcase` | 9,105 KB | 2,386 KB | 170 KB | 32 KB | 144 KB |
   | `notepad` | 6,028 KB | 2,090 KB | 16 KB | 24 KB | 132 KB |
   | `simple` | 5,217 KB | 2,018 KB | 0 KB | 28 KB | 128 KB |

   Summed PSS 20,423 KB; sharing saves 4,120 KB.

**Still an upper bound.** It assumes the smaller apps use no library page the
showcase does not, and leaves out what the dynamic linker costs each app at
start (relocations, private GOT pages). Both pull the real figure down, most
likely under 20%.

**Reading.** About 4 MB saved across three apps, on the line. Recommendation
so far: no-go, pending a run on a real Linux desktop.

## Phase 0 results (Linux desktop, 2026-11-02)

**Machine.** Linux Mint 22.3, Cinnamon on X11, Mesa llvmpipe (no GPU), a KVM
guest on an AMD Ryzen 5 4600H with 4 cores, kernel 7.0, GCC Release. Same
three apps, ten launches, medians.

1. `pss.sh`: **19.0%**. Summed PSS 24,157 KB; sharing saves 4,602 KB.

   | | PSS | Clean pages of its binary |
   | --- | --- | --- |
   | `showcase` | 14,224 KB | 2,898 KB |
   | `notepad` | 5,386 KB | 2,322 KB |
   | `simple` | 4,553 KB | 2,262 KB |

2. `pss-pages.sh`: **17.3%**. Summed PSS 24,200 KB; sharing saves 4,176 KB.

   | | PSS | lib | app | mixed | other |
   | --- | --- | --- | --- | --- | --- |
   | `showcase` | 14,282 KB | 2,606 KB | 232 KB | 32 KB | 148 KB |
   | `notepad` | 5,382 KB | 2,124 KB | 80 KB | 24 KB | 136 KB |
   | `simple` | 4,516 KB | 2,064 KB | 36 KB | 24 KB | 132 KB |

**Reading.** As expected, the desktop stacks grow the showcase's PSS by about
5 MB, while the library pages stay at about 4 MB. Both estimates are upper
bounds, and both are under 20%. **No-go**, unless a run on real GPU hardware
differs.

## Phase 0b: a real shared build, measured (2026-11-02)

The estimates above are upper bounds, so a working prototype was built to
measure the real difference. `-DREAKTOR_SHARED_PROTO=ON` (with
`-DCMAKE_POSITION_INDEPENDENT_CODE=ON`) links the runtime, the core, SDL,
FreeType, plutosvg, plutovg and onlay into `libreaktor-proto.so`; each sample
keeps its modules (or their `_none` twins) and exports their symbols back to
the library. No soname, no hidden symbols: everything is exported, which is
the worst case for relocations and calls.

Same desktop as above; ten launches, medians, static and shared measured in
the same session.

**Memory** (`pss.sh`, the three apps together, idle):

| | static | shared | saved |
| --- | --- | --- | --- |
| `showcase` | 14,261 KB | 13,007 KB | 1,254 KB |
| `notepad` | 5,370 KB | 4,239 KB | 1,131 KB |
| `simple` | 4,501 KB | 3,453 KB | 1,048 KB |
| **summed** | **24,152 KB** | **20,698 KB** | **3,454 KB (14.3%)** |

The library's pages that each app dirties (GOT, relocated data) are about
124 KB per app in `simple`.

**Disk:** 11.4 MB for the three static binaries, against 6.2 MB shared
(`libreaktor-proto.so` 5.0 MB, unstripped, plus 1.16 MB of apps).

**Start:** the dynamic loader takes 0.1 to 0.2 ms for either build;
`showcase` goes from 19,525 to 21,709 relocations. CPU in the first 3
seconds: `simple` 40 ms both, `showcase` 220 ms static and 230 ms shared.

**Frame cost** (`bench --no-vsync`, five runs):

| | build | render | whole frame |
| --- | --- | --- | --- |
| static | 0.04 ms | 0.05 ms | 1.46 to 1.63 ms |
| shared | 0.05 ms | 0.10 ms | 1.48 to 1.53 ms |
| shared, `-fno-semantic-interposition` | 0.04 ms | 0.08 ms | 1.40 to 1.61 ms |
| shared, that and `-Wl,-Bsymbolic-functions` | 0.04 ms | 0.06 ms | as static |

Present (llvmpipe) dominates the frame, so fps does not change.

**Why the shared build was slower** (`perf`, both builds):

- **Frame.** The extra render time is in `nk_sdl_render_ex`, Nuklear's
  SDL drawing loop. With every symbol exported and interposable, GCC stops
  inlining and specialising small functions inside SDL and Nuklear:
  `SDL_floorf`, `SDL_roundf`, `SDL_memcmp`, `nk_buffer_memory`, `nk_vec2`
  become real calls, and the `.constprop`/`.isra` clones go. Calls between
  files then go through the PLT. `-fno-semantic-interposition` restores the
  inlining (build back to static); `-Bsymbolic-functions` binds the calls
  inside the library directly (render back to static). Hidden symbols
  (Phase 1, item 2) do both, so Phase 1 needs no extra flags.
- **Start.** `showcase` uses about 10 ms more CPU in its first 3 s
  (`perf stat -r 10`: 222 against 232 ms), split about evenly between user
  and kernel time, with the same page faults (4,170 against 4,199). Lazy
  binding in the dynamic loader (`_dl_fixup`, `_dl_lookup_symbol_x`) is
  about 0.3 ms of it; the rest is the slower code above running during start
  (font rasterising, the first frames) and the kernel mapping one more
  library. With both flags the difference falls inside run-to-run noise
  (225 against 234 ms in one pass).

**Reading.** The real saving is 14.3%, under the 20% threshold and under the
17.3% estimate. As built first it cost about 10 ms of start CPU in the showcase
and twice Reaktor's render time; with hidden or directly bound symbols both
costs go away. The disk saving
(46%) is the stronger argument, but it was agreed not to count toward the
threshold.

## Phase 0c: the same, with hidden symbols (2026-11-02)

`-DREAKTOR_SHARED_PROTO_HIDE=ON` adds a version script that exports only
`reaktor_*`, `nk_*`, `nka_*`, `SDL_*` and the five FreeType and plutovg
symbols the modules call, and compiles the library's parts with
`-fno-semantic-interposition`. Everything else (X11 and Wayland glue,
FreeType's and plutovg's insides) binds inside the library. Exports fall from
4,653 to 3,365; SDL's own API is 1,961 of them, as it would be as
`libreaktor-sdl3.so`. Every sample starts with `LD_BIND_NOW=1`, so nothing
it needs is hidden.

| | static | shared | shared, hidden |
| --- | --- | --- | --- |
| summed PSS, three apps | 24,152 KB | 20,698 KB | 20,617 KB |
| saving against static | | 14.3% | 14.6% |
| library pages each app dirties (`simple`) | | 124 KB | 116 KB |
| `showcase` relocations | 19,525 | 21,709 | 20,559 |
| `bench` build / render | 0.04 / 0.06 ms | 0.05 / 0.10 ms | 0.04 / 0.08 ms |
| `showcase` CPU, first 3 s | 216 to 222 ms | 225 to 232 ms | 224 to 230 ms |

**Reading.** Hiding symbols does not move memory: 81 KB of 20.7 MB. The
saving stays at about 14.5%. It does recover build time, and half the render
time. The rest of render is the calls into SDL, which go through the PLT
because SDL's API stays exported. With SDL as its own library, as decided,
that cost stays. Binding SDL inside libreaktor (`-Bsymbolic-functions`,
Phase 0b) removes it, but then an app cannot have SDL from anywhere else.

**Seen and not explained:** in three of six early runs of `pss.sh` on this
build, `notepad` had exited by the time it was measured, with nothing in its
log. A signal, a closed window, Ctrl+Q and the menu's Quit all end it exactly
so: status 0, empty log; a crash would have left a core dump and a kernel
line, and there was neither. Since then the runtime logs one `quitting:` line
with the reason, and `pss.sh` prints how each app ended. A soak of 15 rounds
of ten launches for the hidden build and 15 for the static one, alternating,
ran clean (300 launches each of the three apps). Not reproduced; most likely
something on the live desktop asked notepad to quit.

## Phase 0d: more apps at once (2026-11-02)

The saving grows with each extra app, since each one stops carrying its own
copy of the library. `pss.sh -a` takes the list of apps. The fourth and fifth
apps are copies of `notepad` and `simple` under new names (`notepad2`,
`simple2`): a copy is another file, so statically it shares nothing with the
original, as a different app would not. Hidden-symbol shared build against
static, same desktop, ten launches, medians.

| apps together | static | shared | saved | saving |
| --- | --- | --- | --- | --- |
| 3 (`showcase`, `notepad`, `simple`) | 24,200 KB | 20,630 KB | 3,570 KB | 14.8% |
| 4 (+ `notepad2`) | 29,802 KB | 23,726 KB | 6,076 KB | **20.4%** |
| 5 (+ `simple2`) | 34,437 KB | 26,087 KB | 8,350 KB | **24.2%** |

Each extra app saves about 2.3 to 2.5 MB: its own library pages, and a
little of everyone else's PSS as the shared pages split one more way.

**Reading.** With four different Reaktor apps running together, sharing
crosses the 20% threshold; with five it saves about a quarter. With three it
does not. Real different apps carry a little more code of their own than
these copies, which pulls the figures down slightly. Whether to go now turns
on how many Reaktor apps a user would really run at once.

## Running Phase 0 on a Linux desktop

Needs GCC with GNU ld from binutils 2.38 or later (`-Map=DIR/` writes one map
per program; not lld or gold), CMake, Ninja, `readelf`, GNU `dd` and `od`, a
kernel with `smaps_rollup` (4.14+). No root needed: an unprivileged
`pagemap` read still has the present and file-page bits.

From the checkout, on branch `shared-phase0`:

```sh
git submodule update --init --recursive
./build.sh                      # build/showcase, build/notepad, build/simple
sh tools/ci/pss.sh build        # estimate 1, on the desktop's own display
sh tools/ci/pss-pages.sh        # estimate 2; builds build-map with maps first
```

Without `-x` both run on the current `DISPLAY` (or Wayland), which is the
point of a desktop run. `-x Xvfb` starts a virtual display instead, as CI does;
`-n RUNS` changes the ten launches; `-w WRAP` runs each app under a wrapper.
Close any running Reaktor app first: the showcase is single instance, so a
running copy would take its launch, and another app's pages would skew PSS.

Output lands in `smoke/pss/` and `smoke/pss-pages/`: `runs.txt` holds each
launch's raw numbers (kB), `saving.txt` the per-launch saving, and each app's
`.log` its output. The last lines printed are the medians and the saving
against 20%.

Worth noting with the result: distribution, desktop (X11 or Wayland), GPU or
not, and whether the numbers differ from CI's. A desktop run loads the real
input-method, accessibility and tray stacks, so PSS grows and the percentage is
likely to fall.

## Phase 1: the shared build (only if Phase 0 says go)

1. A `REAKTOR_SHARED` option, default `OFF`, building `libreaktor.so` with a
   soname (`libreaktor.so.0`) beside the static archive.
2. Only the `include/reaktor/` API exported; everything else hidden
   (`-fvisibility=hidden` and an export macro on the public declarations).
3. SDL built as `libreaktor-sdl3.so`, which `libreaktor.so` links.
4. Optional modules keep working: an app still links a module or its `_none`
   twin, so each module's code stays out of the shared core.
5. `reaktor.pc` and `reaktorConfig.cmake` learn the shared variant.

## Phase 2: keeping the ABI honest

1. The CI `sizeof` / `offsetof` table for the Nuklear structs apps can see,
   and for Reaktor's own public structs. A change fails CI until the soname
   is bumped and the table regenerated.
2. A check that the exported symbol list matches `include/reaktor/`, so
   nothing internal leaks out and nothing public is missing.
3. `check-libreaktor.sh` gains a shared run: `examples/consumer` linked
   against the `.so` must draw the same pixels as the static `simple`.

## Phase 3: packages

1. `tools/libreaktor-pkg.sh` packs the shared library and SDL as their own
   packages (`libreaktor0`, `libreaktor-sdl3-0` or the distro's spelling) and
   the headers as a `-dev` package.
2. The samples get a shared-linked variant to prove a package installs and
   runs as intended.
3. BSD packages follow the same split.

## Later, lower priority

- **Windows:** an MSIX framework package carrying `reaktor.dll` and SDL, which
  packaged apps declare as a dependency.
- **macOS:** a framework signed with the app's Team ID, or a Homebrew formula,
  since apps from other developers cannot load it under the hardened runtime.

## Open questions

1. ~~Go or no-go.~~ Go (2026-11-02), on four or more apps. The threshold is 20% (agreed 2026-10-30). CI says 20.2% at
   most, and a Linux desktop says 17.3% (2026-11-02). That points to no-go;
   a run on a desktop with a real GPU would confirm it. A real shared
   build (Phase 0b) saves 14.3%, and 14.6% with hidden symbols (Phase 0c),
   with three apps. Four apps save 20.4%, five 24.2% (Phase 0d).
