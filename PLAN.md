# Plan: libreaktor as a shared library

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
GL, input-method and accessibility stacks, so PSS grows and the percentage is
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

1. Go or no-go, once a Linux desktop run is in. The threshold is 20% (agreed
   2026-10-30); CI says 20.2% at most.
