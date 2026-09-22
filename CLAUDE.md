# Reaktor

C99. Widgets are declared; the look comes from CSS read at runtime.

## Build and run

`./build.ps1` on Windows, `./build.sh` elsewhere — `cmake` is not on PATH, the
script finds MSVC's. `./build.ps1 <target>` builds one. Targets: `showcase`,
`notepad`, `simple`, `bench`, plus the tests in `tools/`.

CMake options pass straight through: `./build.ps1 -DREAKTOR_A11Y=OFF`,
`./build.sh -- -DREAKTOR_A11Y=OFF`. The cache keeps an option, so a later build
without the flag does not reset it — pass `=ON` to switch back.

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
- **A one-pixel stroke is two pixels here.** `nk_stroke_rect` casts its rect to
  `short`, so a half-pixel inset never reaches the draw list, and with line
  anti-aliasing on a stroke of thickness one is always two half-alpha rows.
  An edge that has to be one pixel is two fills, as `reaktor_menu_edge` draws.
- **Measure the present, not the draw.** On a machine with no GPU, most of a
  frame is `SDL_RenderPresent`. `build/render/present` split is on the
  Diagnostics page and in `bench --no-vsync`.
- **Text the atlas cannot draw is not Nuklear's to draw.** A string with any
  character the atlas lacks — Japanese, Arabic — is measured by
  `core/text/text.c` and drawn from the custom command it swaps in for
  Nuklear's text command, so a change to how Nuklear or the renderer places
  glyphs does not reach it. Its bidi levels are matched to mojibake's by order:
  mojibake 0.3.6 records `byte_offset` at a character's last byte.
- **A field's keys are its own, wherever it was declared.** Nuklear keeps
  edit-active state on the window a widget sits in, so the page's window misses
  every field inside a group; `app->editing` is what navigation asks before it
  gives up the arrows, Home, End, Enter and Space. Home and End also scroll the
  panel out from under the edit, so the runtime keeps those two.
- **A field's value is not its buffer, and its length is the caller's.**
  `nk_edit_string` leaves the bytes past the length alone, so the buffer read as
  a string is the last, longer value; a length from anywhere but the field's own
  last frame reseeds it empty every frame.
- **An empty edit has no text pointer.** `nk_str_get_const` answers NULL while
  `nk_str_len_char` still answers bytes, so that pair measures a null pointer
  with a positive length.
- **A hidden web page runs no frames.** The web build's main loop waits on
  `requestAnimationFrame`, which never fires in a background tab, a minimized
  window or a browser pane that is not on screen. `#a11y` is made in `main()`,
  so the root is there, but the canvas stays blank and the tree empty until
  the page renders — and a scripted click or screenshot makes it render, so
  it looks as if the tree waits for input. Check `document.visibilityState`.

## House rules

- Dependencies are git submodules under `external/`, read as they ship. Never
  transcribe a value out of one, and never generate a header from one.
- American English, in code and prose.
- No environment variables. Anything worth overriding is a command-line flag
  the runtime strips from `argv`.
- `docs/DOCUMENTATION.md` is the whole of it; `PERFORMANCE.md` holds measured
  numbers only, with the machine named.
- An optional module is an interface library plus a `_none` twin defining the
  same functions, and an application links one of the two; its
  `REAKTOR_<NAME>` option, default `ON`, swaps in the twin. Localization and
  text shaping are two modules, not one. A module that is off must leave pixels
  and `--a11y-dump` byte-identical except for what the module itself produces.
