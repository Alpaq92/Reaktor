# Plan: libreaktor as a shared library

> **Finished.** Every phase below has shipped; this is the record of how, and
> it no longer describes the code. Since it was written the soname became
> `libreaktor.so.1` (runtime package `libreaktor1`, ABI tables
> `tools/abi/libreaktor.so.1-*.abi`), the packages became `libreaktor`
> (static) and `libreaktor-shared` (the split's development package), and
> the release archives `libreaktor-*` and `libreaktor-shared-*`. The current
> state is in [DOCUMENTATION.md](docs/DOCUMENTATION.md#in-your-own-project),
> the measurements in [PERFORMANCE.md](docs/PERFORMANCE.md).

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
4. **Platforms, in order:** Linux and the BSDs first. Windows was dropped
   (2026-11-03; see *Later*). macOS (same Team ID, or Homebrew only, since
   hardened-runtime library validation refuses other developers' dylibs) is
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

## Phase 1: the shared build (started 2026-11-02)

**Status.** Items 1 to 4 are in: `-DREAKTOR_SHARED=ON` builds
`lib/libreaktor.so.0` (2.0 MB) and `libreaktor-sdl3.so.0` (2.9 MB, SDL 3.4.18
with `core/sdl/no_dynapi.h` as for the static build) beside `libreaktor.a`,
and links the samples and tests to them. Exports come from a version script,
`tools/reaktor.map`, not an export macro: 1,185 symbols, all `reaktor_`, `nk_`,
`nka_` and the five FreeType and plutovg calls the modules make. The modules
reach some `reaktor_` functions that are not in `include/reaktor/`, so for now
the `reaktor_*` glob exports those too; Phase 2's symbol check will draw the
line. The measurement prototype (`REAKTOR_SHARED_PROTO`, commit `0948611`) is
gone.

Measured as in Phase 0d: three apps 24,200 KB static against 20,659 KB
shared (14.6%), four 29,802 KB against 23,810 KB (20.1%). `bench` build and
render 0.05 and 0.08 ms; `showcase` CPU in its first 3 s 232 ms, static 231 ms.
All unit tests pass in both builds.

Item 5 is in too. With `REAKTOR_SHARED`, `cmake --install` adds to the
static files `libreaktor.so*`, `libreaktor-sdl3.so*` and
`libreaktor-modules.a`, the modules (and mojibake and kb_text_shape) for apps
built outside the tree. Such an app links `libreaktor.so`, then the modules
archive, which fills in what the library calls, then `libreaktor-sdl3.so`,
and exports the modules back with `-Wl,--export-dynamic`. That is what
`reaktor-shared.pc` and the CMake target `reaktor::shared` give it; in CMake,
`reaktor::reaktor` stays the static one. Since the `.a` and the `.so` share a
`lib/`, `reaktor.pc` then names `-l:libreaktor.a`, so it stays static.

Checked by hand: `examples/consumer` linked statically, linked with
`-DCONSUMER_SHARED=ON`, and `simple.c` built with `pkg-config reaktor` and
`reaktor-shared` all draw the same pixels and the same accessibility tree as
the tree's own `simple`. Phase 2, item 3 puts that into `check-libreaktor.sh`.


1. A `REAKTOR_SHARED` option, default `OFF`, building `libreaktor.so` with a
   soname (`libreaktor.so.0`) beside the static archive.
2. Only the `include/reaktor/` API exported; everything else hidden
   (`-fvisibility=hidden` and an export macro on the public declarations).
3. SDL built as `libreaktor-sdl3.so`, which `libreaktor.so` links.
4. Optional modules keep working: an app still links a module or its `_none`
   twin, so each module's code stays out of the shared core.
5. `reaktor.pc` and `reaktorConfig.cmake` learn the shared variant.

## Phase 2: keeping the ABI honest (in, 2026-11-02)

**Status.** All three items are in, and CI's x86-64 job runs them in a step
of its own (`Shared libreaktor`) on a `-DREAKTOR_SHARED=ON` build:

- `tools/ci/check-abi.sh` compiles `tools/abi/abi.c` (just `reaktor.h`) with
  debug types and has gdb print, with `ptype /o`, every `reaktor_` struct,
  union and enum in `include/reaktor/`, `nk_window` and `nk_input`, every
  Nuklear type `reaktor/nuklear.h` renames, and the value of every `NK_`
  constant it renames. Nothing is copied out of Nuklear by hand. The output
  must match `tools/abi/libreaktor.so.0-ARCH.abi`, ARCH being the compiler's
  target (`cc -dumpmachine`); otherwise the soname must go up and the table be
  rewritten with `-u`. Adding one field to `reaktor_shortcut` fails it. Only
  x86_64 has a table so far (12,266 lines, made here). Every native Linux job
  (x86-64, x86, arm64) runs the check in a step of its own and uploads what it
  wrote to `smoke/abi/`; a missing table is a warning, exit status 3, so the
  x86 and arm64 tables, and the x86_64 one if CI's older gdb lays it out
  differently, come from CI's artifact.
- `tools/ci/check-exports.sh` holds the built library to `tools/reaktor.map`,
  which lists Reaktor's names instead of `reaktor_*`. Two symbol versions:
  `LIBREAKTOR_0` holds the 149 public functions the library defines and
  Nuklear's and nukanim's APIs; `LIBREAKTOR_PRIVATE_0` the 34 non-public calls
  the modules make and five FreeType and plutovg ones. It fails on a name
  exported but not listed, listed but not exported, public but in neither the
  library nor the modules, under `LIBREAKTOR_0` without being public, private
  and no longer called by the modules, private and called by an app's own
  code (samples and tests linked to the library), or under the wrong version.
  `tests/launchtest.c`, which reaches past `include/reaktor/`, links the
  archives in a shared build (`reaktor_add_app(... STATIC ...)`), so nothing
  is exported for a test. Exports fell from 1,185 to 1,030.
- `check-libreaktor.sh` links `examples/consumer` against `libreaktor.so`
  too, where the install has it, checks the program needs it, and requires
  the same pixels and accessibility tree as the built `simple`.


1. The CI `sizeof` / `offsetof` table for the Nuklear structs apps can see,
   and for Reaktor's own public structs. A change fails CI until the soname
   is bumped and the table regenerated.
2. A check that the exported symbol list matches `include/reaktor/`, so
   nothing internal leaks out and nothing public is missing.
3. `check-libreaktor.sh` gains a shared run: `examples/consumer` linked
   against the `.so` must draw the same pixels as the static `simple`.

## Phase 3: packages (in, 2026-11-02)

**Status.** `tools/libreaktor-pkg.sh -s` builds with `REAKTOR_SHARED` (and
`-b` takes a shared build as it is). When the install holds `libreaktor.so`,
the package splits in three: `libreaktor-sdl3-0` (`libreaktor-sdl3.so.0*`
and SDL's license), `libreaktor0` (`libreaktor.so.0*`, the other licenses,
`NOTICE.md`; needs the first) and the development package under its old
name (`libreaktor-dev`, `libreaktor-devel`, `libreaktor` on the BSDs) with
the headers, both archives, the `.so` links, `reaktor.pc`,
`reaktor-shared.pc` and the CMake files; it needs both. Each file is in one
package. A static build packs one package, renamed `libreaktor-static` in
every format (2026-11-03), so a release can carry both; it and the split's
development package install the same headers and conflict with each other
in every format. `/opt/reaktor/lib` is
not on the loader's path, so `reaktor-shared.pc` and `reaktor::shared` add
an rpath to the install's `lib/`.

Checked here: the three `.deb`s, their dependencies and files, and
`examples/consumer -DCONSUMER_SHARED=ON` built against them, unpacked, which
draws the same as the built `simple`; a static build still packs one
`libreaktor-dev`; `-f tar` writes three tarballs. CI now packs the shared
build as `.deb`s on x86-64, installs them, builds the shared consumer,
removes `libreaktor-dev` and runs it on the two runtime packages alone; the
rpm job does the same with `-s`. The rpm, FreeBSD, OpenBSD and NetBSD
branches are written but have not run: rpmbuild and the BSD tools are not on
this machine. `bsd.yml` packs the shared build on each BSD and runs the shared
consumer on the runtime packages alone, in a step that warns rather than
fails until it has passed there.


1. `tools/libreaktor-pkg.sh` packs the shared library and SDL as their own
   packages (`libreaktor0`, `libreaktor-sdl3-0` or the distro's spelling) and
   the headers as a `-dev` package.
2. The samples get a shared-linked variant to prove a package installs and
   runs as intended.
3. BSD packages follow the same split.

## In releases (2026-11-03)

CI builds the shared library next to the static one, and a release carries
both. Every Linux job (x86-64, x86, arm64, and armhf and riscv64 cross-built)
builds `-DREAKTOR_SHARED=ON`, checks it with `check-libreaktor.sh` (natively,
or on the emulated machine after the builder installs it) and uploads
`libreaktor-VERSION-linux-ARCH.tar.gz` and the three split `.deb`s;
the rpm job adds the split rpms; the x86-64 BSD jobs add
`libreaktor-VERSION-OS-x86-64.tar.gz`. The static tarballs beside them, on
Linux and the BSDs, are now `libreaktor-static-VERSION-OS-ARCH.tar.gz`,
matching the `libreaktor-static` package; Windows, macOS and the web, which
have only the static library, are named `libreaktor-static-` too. Installs
that ship (`check-libreaktor.sh`, `libreaktor-pkg.sh`, CI's own) pass
`--strip`, which strips the two shared libraries and leaves the archives:
`libreaktor.so.0` 2.04 to 1.87 MB, `libreaktor-sdl3.so.0` 2.92 to 2.66 MB,
exports unchanged. All of them are `package-*`
artifacts, so `release.yml` publishes them with the rest, and its notes say
how to use them. Not covered: the BSDs' arm64 (cross-built in `bsd-arm64`),
Windows, macOS and the web.

## Later, lower priority

- ~~**Windows:** an MSIX framework package carrying `reaktor.dll` and SDL, which
  packaged apps declare as a dependency.~~ Dropped (2026-11-03): each Windows
  app ships alone, so sharing buys little, and a framework package serves only
  MSIX-packaged apps while needing a signing certificate, an export macro on
  every public declaration and the DLL C runtime. Windows stays static.
- **macOS:** a framework signed with the app's Team ID, or a Homebrew formula,
  since apps from other developers cannot load it under the hardened runtime.
  Homebrew is ruled out (2026-11-03: no access), which leaves only the
  framework, shared by apps under one Team ID. Kept in scope (2026-11-03).
  It saves memory only when several of those apps run at once and the
  framework is installed once, in `/Library/Frameworks`, not inside each
  `.app`. Still open: an Apple Developer ID to sign and notarize it, and a
  universal binary or one per architecture. It cannot be built or checked on
  the Linux machine this plan was worked on; `macos.yml` would be its check.

  **Written, not yet run (2026-11-03).** `-DREAKTOR_SHARED=ON` on macOS builds
  `Frameworks/Reaktor.framework` (`Versions/A`, install name
  `@rpath/Reaktor.framework/...`, identifier `io.github.alpaq92.reaktor`)
  from the same parts plus SDL, linked in with `-force_load`, so there is no
  `libreaktor-sdl3`: no system SDL stands to be confused with ours. ld64 has
  no version script, so CMake turns `tools/reaktor.map` into an exported
  symbol list (190 names, checked here with `cmake -P`); the framework binds
  its calls into the modules at load (`-undefined dynamic_lookup`) and apps
  link with `-export_dynamic`, which also keeps those functions through
  `-dead_strip`. `reaktor::shared` links the framework and
  `libreaktor-modules.a`, with an rpath to the install and to
  `/Library/Frameworks`. `macos.yml` builds it on both runners, runs
  `check-libreaktor.sh` on it and uploads `libreaktor-VERSION-macos-ARCH.zip`.
  Per architecture, signed ad hoc, as today.

  **Universal too (2026-11-03).** A third job in `macos.yml` takes the two
  per-architecture zips, joins every Mach-O file and archive in them with
  `lipo` (the framework, `libreaktor-modules.a`, `libreaktor.a`), requires
  every other file to be the same in both, signs the framework again ad hoc,
  builds the shared consumer for `arm64;x86_64` against it, runs it, and
  uploads `libreaktor-VERSION-macos-universal.zip`. If CMake writes a path
  that differs between the two runners into the config or `.pc` files, that
  check fails and the file needs one form for both.

  **No Developer ID (2026-11-03), and none soon.** So the framework stays
  signed ad hoc and is not notarized, like the rest of the macOS release.
  Without a Team ID, "apps under one Team ID" means apps that do not turn on
  the hardened runtime: its library validation is what refuses a framework
  from another signer, and an ad hoc one has no signer to match. Reaktor's
  own apps do not turn it on. A framework downloaded from the release carries
  the quarantine flag, so the person installing it clears that once
  (`xattr -dr com.apple.quarantine`) before apps can load it.

## Open questions

1. ~~Go or no-go.~~ Go (2026-11-02), on four or more apps. The threshold is 20% (agreed 2026-10-30). CI says 20.2% at
   most, and a Linux desktop says 17.3% (2026-11-02). That points to no-go;
   a run on a desktop with a real GPU would confirm it. A real shared
   build (Phase 0b) saves 14.3%, and 14.6% with hidden symbols (Phase 0c),
   with three apps. Four apps save 20.4%, five 24.2% (Phase 0d).
