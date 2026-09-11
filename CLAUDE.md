# Reaktor

C99. Widgets are declared; the look comes from CSS read at runtime.

## Build and run

`./build.ps1` on Windows, `./build.sh` elsewhere — `cmake` is not on PATH, the
script finds MSVC's. `./build.ps1 <target>` builds one. Targets: `showcase`,
`notepad`, `simple`, `bench`, plus the tests in `tools/`.

## Verify against pixels, not source

Reasoning from a dependency's source about what it will draw has been wrong
often enough to be a rule. Take the screenshot, diff the dump, read the
framebuffer.

```bash
./build/showcase --tab 7 --theme dark --shot a.bmp --a11y-dump a.txt
```

`--a11y-dump` writes one line per node with role, name, value, state and
**bounds**. Diff two and anything that moved by a pixel says so. It is the
regression oracle for every layout or style change.

## Traps that have cost real time

- **A rule cannot resize a box the page already sized.** Any `.box = { .h = N }`
  in a page locks CSS out of that axis. This is the usual reason a stylesheet
  "only changes the color".
- **`core/ui/style_map.c` is the only writer of `nk_style`.** Setting a widget
  style anywhere else works until the next theme change reloads the sheets.
- **A box is placed one frame late**, and found again by its **name**. A widget
  whose label changes every frame needs a stable `.name` or it never gets a
  rectangle.
- **libcss has no `em`/`rem`** — its units are px, %, dip, sp, pt. `cssflat.c`
  converts before the engine sees the text, along with `var()`, `@media` and
  attribute selectors.
- **The renderer snaps every vertex to whole pixels.** Sub-pixel corrections
  are discarded, and rects that abut can round apart into a visible seam.
- **Measure the present, not the draw.** On a machine with no GPU, most of a
  frame is `SDL_RenderPresent`. `build/render/present` split is on the
  Diagnostics page and in `bench --no-vsync`.

## House rules

- Dependencies are git submodules under `external/`, read as they ship. Never
  transcribe a value out of one, and never generate a header from one.
- American English, in code and prose.
- No environment variables. Anything worth overriding is a command-line flag
  the runtime strips from `argv`.
- `docs/DOCUMENTATION.md` is the whole of it; `PERFORMANCE.md` holds measured
  numbers only, with the machine named.
