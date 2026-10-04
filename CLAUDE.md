# Reaktor

C99. Widgets are declared; the look comes from CSS read at runtime.

## Build and run

`./build.ps1` on Windows, `./build.sh` elsewhere — `cmake` is not on PATH, the
script finds MSVC's. `./build.ps1 <target>` builds one. Targets: `showcase`,
`notepad`, `simple`, `bench`, plus the tests in `tools/`. `-Arch x86` (or
`arm64`) builds that architecture into `build-<arch>`; ARM64 needs Visual
Studio's ARM64 tools, which this machine lacks, so CI is its only check.

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
- **Hover is drawn only across a hot rectangle.** Pointer motion runs a frame
  only when it crosses a rectangle `hot_push` registered the frame before, so a
  widget that looks different under the pointer and registers none keeps its
  old look until some other input.
- **libcss has no `em`/`rem`** — its units are px, %, dip, sp, pt. `cssflat.c`
  converts before the engine sees the text, along with `var()`, `color-mix()`,
  `@media` and attribute selectors.
- **The renderer snaps every vertex to whole pixels.** Sub-pixel corrections
  are discarded, and rects that abut can round apart into a visible seam.
- **SDL redraws a software rectangle its own way.** Its software renderer
  turns two triangles that make a rectangle into a fill or a copy, and
  truncates the scaled position and size apart, so at a fractional scale a run
  a whole number of device pixels wide loses one. `nk_sdl_quad` draws every
  such rectangle from device pixels before SDL sees it.
- **A one-pixel stroke is two pixels here.** `nk_stroke_rect` casts its rect to
  `short`, so a half-pixel inset never reaches the draw list, and with line
  anti-aliasing on a stroke of thickness one is always two half-alpha rows.
  A border is `reaktor_edge_round`: fills for its runs, a ring mask for its
  corners, inside the box.
- **A rounded fill is jagged unless it is a mask.** Fill anti-aliasing is off
  on every renderer, so a fill with corners goes through `reaktor_fill_round`,
  and `reaktor_render` swaps each one Nuklear drew itself for masks, relinking
  the built command list — whose end is wherever a `next` reaches
  `ctx->memory.allocated`.
- **Measure the present, not the draw.** On a machine with no GPU, most of a
  frame is `SDL_RenderPresent`. `build/render/present` split is on the
  Diagnostics page and in `bench --no-vsync`.
- **Text the atlas cannot draw is not Nuklear's to draw.** A string with any
  character the atlas lacks — Japanese, Arabic — is measured by
  `core/text/text.c` and drawn from the custom command it swaps in for
  Nuklear's text command, so a change to how Nuklear or the renderer places
  glyphs does not reach it. Its bidi levels are matched to mojibake's by order:
  mojibake 0.3.6 records `byte_offset` at a character's last byte.
- **Only Nuklear's active window gets input.** Every other window carries a
  sticky `NK_WINDOW_ROM` until a click or hover activates it inside
  `nk_begin`. A layer that must not take input is begun
  `NK_WINDOW_NOT_INTERACTIVE` with `ctx->input` blanked — `reaktor_layer_begin`
  with `HOLD_ALL` — and focus is handed back with `reaktor_layer_focus`.
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
- **A tap focuses the canvas by itself.** The canvas is focusable, so a
  touch's compatibility `mousedown` moves focus to it as its default action,
  with no script involved. Only canceling the touch `pointerdown` stops it.
- **SDL hands the app its events a frame late.** Its web handlers queue them
  as they happen, but the app sees them only when the next iteration pumps, so
  anything the page keeps in a queue of its own overtakes them. The page's
  typing goes onto SDL's queue for that reason.
- **A hidden web page runs no frames.** The web build's main loop waits on
  `requestAnimationFrame`, which never fires in a background tab, a minimized
  window or a browser pane that is not on screen. `#a11y` is made in `main()`,
  so the root is there, but the canvas stays blank and the tree empty until
  the page renders — and a scripted click or screenshot makes it render, so
  it looks as if the tree waits for input. Check `document.visibilityState`.

## House rules

- Dependencies are git submodules under `external/`, read as they ship. Never
  transcribe a value out of one, and never generate a header from one. The one
  exception is `REAKTOR_BUILTINS` in CMakeLists.txt: tiny.css's three sheets
  and the seven Ionicons the widgets draw (three chevrons, three discs, the
  toast's close cross) are compiled in byte for byte, beside the Aileron
  fonts, the mark and reaktor.css. Nothing else from a submodule is — not the
  rest of Ionicons, not simple.css.
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
