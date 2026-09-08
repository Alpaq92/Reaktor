# Contributing to Reaktor

Pull requests are welcome, and so is an issue that just says "this looks
wrong" with a screenshot — a good half of the work in this repository has come
from someone pointing at a control and saying it looked off.

## Getting started

You need the submodules and a C compiler:

```bash
git submodule update --init --recursive
```

Then `.\build.ps1` on Windows, or `cmake -S . -B build && cmake --build build
--parallel` anywhere else. `.\build-wasm.ps1` builds the web target, which is
worth doing before you send anything: it uses the same sources and catches
assumptions the native build lets through.

[docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) explains how the CSS reaches the
widgets, and lists the Nuklear behaviours that have already cost somebody an
afternoon. Read that first if you are touching drawing code.

## Review process — talk to the maintainer, not the bot

Automated review is a first pass, not the decision. It is good at spotting a
missing bounds check and bad at knowing that a widget is drawn the way it is
because the alternative was tried and looked wrong. If a change touches
layout, styling or the pacing of frames, say what you tried and what it looked
like, and expect a conversation rather than a checklist.

## Code conventions

- **C99, and portable.** No compiler extensions in `src/`. Platform code goes
  behind `_WIN32` / `__EMSCRIPTEN__` / POSIX in the one file that needs it —
  today that is only `src/util.c`.
- **Around 79 columns**, four-space indent, no tabs. Headers keep to it
  strictly; a source line may run over where breaking it would read worse.
- **Comments say why, not what.** The code already says what it does. A
  comment earns its place by recording the reason, the measurement, or the
  thing that was tried first and failed.
- **`reaktor_` prefixes** anything crossing a translation unit. Anything that
  does not cross one is `static`.
- **No new dependency without a licence check.** See below.

## Decisions of record

These came out of earlier reviews. Treat them as binding unless you make the
case for changing one.

- **Submodules are read, never edited and never quoted.** Everything in
  `third_party/` is used as it ships. Where that is genuinely impossible the
  file is copied into `src/`, marked as vendored, and every deviation from
  upstream commented — `src/nk_sdl3_renderer.h` is the only one, and it carries
  the maintenance cost of no longer tracking upstream. Copying is a last
  resort, not a shortcut. No colour, path, metric or other value
  is copied out of a submodule into this tree — the app opens the file at
  runtime and reads it. If you find yourself typing a hex code you saw in a
  submodule, that is the signal to load the file instead.
- **The stylesheet is the source of style.** A colour, radius, border width or
  padding that appears as a literal in `src/` needs a reason next to it. The
  legitimate ones are fallbacks for when a stylesheet fails to load.
- **MIT-preferred for anything vendored.** Permissive is the requirement; MIT
  is the default. Record the licence in [docs/NOTICE.md](docs/NOTICE.md) in
  the same change, read from the project's own licence file rather than from
  its README.
- **A vendored asset carries its terms in the tree.** Fonts are the exception
  to the submodule rule, and the reason the UI face changed: the old one came
  from inside a submodule that ships it with no licence anywhere near it, so
  its terms could only be read off a third party. If an upstream ships none,
  trace them to something the author actually published — the file's own
  metadata counts, and is better evidence than a text file beside it — then
  write down what you found, where it came from, and that you assembled the
  record. `assets/fonts/Aileron-Notice.txt` is the shape to copy.
- **Nothing is drawn when nothing has changed.** SDL waits on events and a
  frame is built only when a widget's state, the pointer's hot region or the
  window itself has moved. Do not add a timer, a polling loop or an
  unconditional repaint to make something update; make the thing that changed
  mark the frame dirty.
- **Nuklear fires buttons on press unless told otherwise.** The build defines
  `NK_BUTTON_TRIGGER_ON_RELEASE` because the default needs a frame while the
  mouse is held, and a browser delivers press and release in the same task, so
  that frame never exists. Leave it defined.

## Security

Reaktor reads files at startup and while running: stylesheets through libcss,
SVG through plutosvg, fonts through Nuklear's baker. All of them are parsers,
and all of them are pointed at paths derived from the application root.

- Keep every path going through `reaktor_path()`. It resolves against the root
  marker, which is what stops a relative path escaping the install.
- Do not add a code path that loads a stylesheet, font or image from a
  location the user has not chosen, and do not add a network fetch.
- If you extend the CSS pass in `src/cssflat.c`, remember it runs on input
  before libcss validates any of it. Bound your reads.

## Tests

There is no test suite, and adding one for a UI drawn by an immediate-mode
library is not obviously the right thing — so verification here is manual and
we would rather it were honest about that.

For a change of any size, say in the pull request:

- that both targets build — native and WebAssembly;
- which tabs you drove, and what you clicked;
- what the Diagnostics tab said before and after, if you touched anything that
  could affect frame time or memory;
- a screenshot, if the change is visible. Set `REAKTOR_TAB`, `REAKTOR_SCROLL` and
  `REAKTOR_THEME` to land on the same view every time rather than clicking your
  way there — see [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).

Claims about performance want a number from the Diagnostics tab or from the
process, not an expectation. Several plausible optimisations in this codebase
turned out to be measurably worse.
