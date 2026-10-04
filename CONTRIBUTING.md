# Contributing

Pull requests are welcome, and so is an issue that just says "this looks
wrong" with a screenshot.

## Getting started

```bash
git submodule update --init --recursive
./build.sh          # build.ps1 on Windows
for t in laytest onlaytest a11ytest animtest keytest localetest texttest
do ./build/$t || break; done
```

`./build-wasm.sh` builds the web target from the same sources and catches what
the native build lets through; run it before sending anything.
[docs/DOCUMENTATION.md](docs/DOCUMENTATION.md) is the map — read its
[Nuklear traps](docs/DOCUMENTATION.md#nuklear-traps) before touching drawing
code.

## Conventions

- **C99, portable, no compiler extensions.** A platform subsystem goes in
  `platform/`; a single platform fact goes behind `_WIN32`, `__APPLE__`,
  `__linux__` or `__EMSCRIPTEN__` where it is needed.
- **Around 79 columns**, four-space indent, no tabs, American English.
- **Few comments.** One line, and only where removing it would invite a bug.
- **`reaktor_` prefixes anything crossing a translation unit**; everything
  else is `static`.

## Decisions of record

Binding unless you make the case for changing one.

- **Submodules are read, never edited, never quoted.** No value is copied out
  of `external/`; the app opens the file at run time. The only vendored source
  is `core/render/nk_sdl3_renderer.h`, with every deviation marked `REAKTOR`.
  The only bytes compiled in are `REAKTOR_BUILTINS`.
- **The stylesheet is the source of style.** A color, radius, border or
  padding written as a literal in C needs a reason beside it, such as a
  fallback for a sheet that failed to load.
- **Permissive licenses only, MIT preferred.** Record a dependency in
  [docs/NOTICE.md](docs/NOTICE.md) in the same change, from the project's own
  license file. A vendored asset carries its terms in the tree;
  `assets/fonts/Aileron-Notice.txt` is the shape to copy.
- **An optional feature is a module**: an interface library, a `_none` twin
  defining the same functions, and a `REAKTOR_<NAME>` option, `ON` by default.
  Switched off, a build draws the same pixels and writes the same
  `--a11y-dump`, except for what the module itself produces.
- **Nothing is drawn when nothing has changed.** No timers, polling or
  unconditional repaints; mark what changed.
- **Every asset goes through `reaktor_asset_load()`**, which resolves a name
  under the `.reaktor-root` marker, then the compiled-in copies, never the
  working directory. No network fetches.
- **No environment variables.** Anything worth overriding is a command-line
  flag the runtime strips from `argv`.

## Saying what you verified

In the pull request:

- that native and WebAssembly both build and the tests pass;
- **the accessibility dump before and after**, if anything moved: run with
  `--a11y-dump <path>` and diff the two;
- a screenshot for a visible change, pinned with `--tab`, `--scroll` and
  `--theme` and written by `--shot`.

A visual fix is proven by the framebuffer, not by reasoning from the source
that drew it wrong. A performance claim needs a number from the Diagnostics
page or `bench --no-vsync`, and a claim against another framework needs
`benchmarks/run.ps1`; several plausible optimizations here measured worse.

## Review

Automated review is a first pass. It finds a missing bounds check, but not that
a widget is drawn as it is because the alternative was tried and looked wrong.
For a change to layout, styling or frame pacing, say what you tried and what
it looked like.
