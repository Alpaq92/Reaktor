# Contributing

Pull requests are welcome, and so is an issue that just says "this looks wrong"
with a screenshot — a good half of the work here has come from someone pointing
at a control and saying it looked off.

## Getting started

```bash
git submodule update --init --recursive
./build.sh          # build.ps1 on Windows
for t in laytest onlaytest a11ytest animtest keytest localetest texttest
do ./build/$t || break; done
```

`./build-wasm.sh` builds the web target from the same sources and catches
assumptions the native build lets through — worth running before you send
anything.

[docs/DOCUMENTATION.md](docs/DOCUMENTATION.md) is the map: the tree, the style
pipeline, the frame loop, and the Nuklear behaviors that have already cost
somebody an afternoon. Read it before touching drawing code.

## Conventions

- **C99, portable, no compiler extensions.** Platform code goes in
  `platform/` when it is a subsystem, or behind `_WIN32` / `__APPLE__` /
  `__linux__` / `__EMSCRIPTEN__` in the file that needs it when it is one fact.
- **Around 79 columns**, four-space indent, no tabs.
- **Comments say why, not what.** A comment earns its place by recording the
  reason, the measurement, or the thing that was tried first and failed.
- **`reaktor_` prefixes anything crossing a translation unit.** Anything that
  does not cross one is `static`.

## Decisions of record

Binding unless you make the case for changing one.

- **Submodules are read, never edited, never quoted.** No color, path, metric
  or other value is copied out of `external/` into this tree — the app opens
  the file at runtime. Where reading is genuinely impossible the file is copied
  into the tree, marked vendored, and every deviation commented;
  `core/render/nk_sdl3_renderer.h` is the only one, and it carries the cost of
  no longer tracking upstream.
- **The stylesheet is the source of style.** A color, radius, border width or
  padding appearing as a literal in the source needs a reason beside it. The
  legitimate ones are fallbacks for when a sheet fails to load.
- **MIT-preferred for anything vendored**, permissive required. Record it in
  [docs/NOTICE.md](docs/NOTICE.md) in the same change, read from the project's
  own license file rather than its README.
- **A vendored asset carries its terms in the tree.** If an upstream ships
  none, trace them to something the author published — the file's own metadata
  counts — then write down what you found and that you assembled the record.
  `assets/fonts/Aileron-Notice.txt` is the shape to copy.
- **An optional feature is a module, and a module can be left out.** It is an
  interface library with a `_none` twin that defines the same functions, and a
  `REAKTOR_<NAME>` option, `ON` by default, that swaps the twin in. A build with
  the module off draws the same pixels and writes the same `--a11y-dump`, except
  for what the module itself produces. Localization and text shaping are two
  modules, not one.
- **Nothing is drawn when nothing has changed.** Do not add a timer, a polling
  loop or an unconditional repaint to make something update; make the thing
  that changed mark the frame dirty.
- **Every path goes through `reaktor_path()`.** It resolves against the
  `.reaktor-root` marker, which is what stops a relative path escaping the
  install. Stylesheets, SVG and fonts all reach parsers; do not add a code path
  that loads one from a location the user has not chosen, and do not add a
  network fetch.

## Saying what you verified

For a change of any size, say in the pull request:

- that both targets build — native and WebAssembly — and the seven tests pass;
- **the accessibility dump before and after**, if anything moved. Run with
  `--a11y-dump <path>` and diff the two; it reports position to the pixel and
  is the closest thing here to a regression suite;
- a screenshot, if the change is visible. Pin the view with `--tab`,
  `--scroll` and `--theme` rather than clicking your way there.

Two things this repository has learned the hard way. A claim about performance
wants a number from the Diagnostics page, not an expectation — several
plausible optimizations here measured worse, and a claim against another
framework wants `benchmarks/run.ps1`, which draws the same picture in each and
samples them all from outside. Numbers read off somebody's README are how the
chart in PERFORMANCE.md came to compare a CPU rasterizer against four GPU ones
in the same panel. And a claim that a visual fix
works wants the framebuffer: `--shot <path.bmp>` writes the window and
quits. Deriving where a widget *should* be from the same model that put it in
the wrong place will agree with you every time.

## Review

Automated review is a first pass, not the decision. It is good at spotting a
missing bounds check and bad at knowing that a widget is drawn the way it is
because the alternative was tried and looked wrong. If a change touches layout,
styling or the pacing of frames, say what you tried and what it looked like,
and expect a conversation rather than a checklist.
