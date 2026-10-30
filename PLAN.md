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

None. The threshold is 20% (agreed 2026-10-30), and Phase 0 runs in CI only:
`tools/ci/pss.sh`, a step of `linux.yml` on branch `shared-phase0`.
