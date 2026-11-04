# Reaktor

C99. Widgets are declared; the look comes from CSS read at runtime.

## Build and run

`./build.sh [target]` (`./build.ps1` on Windows; `cmake` is not on PATH, the
script finds it). Targets: `showcase`, `notepad`, `simple`, `bench`, the tests
in `tests/`, and `libreaktor`. `-Arch x86|arm64` builds into `build-<arch>`;
ARM64 is checked only by CI.

CMake options pass through: `./build.ps1 -DREAKTOR_A11Y=OFF`,
`./build.sh -- -DREAKTOR_A11Y=OFF`. The cache keeps them, so pass `=OFF` or
`=ON` again to switch back. Check `build/CMakeCache.txt` before measuring:
`REAKTOR_SHARED=ON` left in `build/` makes every sample a shared one.

Shipping: `tools/package.sh` packs a release folder, `tools/libreaktor-pkg.sh`
a system package (`libreaktor` static; with `-s`, `libreaktor0`,
`libreaktor-sdl3-0` and `libreaktor-shared`). A version tag such as `1.2.3`
publishes through `release.yml`.

## Verify against pixels, not source

Reasoning about what a dependency will draw has been wrong too often. Take the
screenshot and diff the dump:

```bash
./build/showcase --tab 7 --theme dark --shot a.bmp --a11y-dump a.txt
```

The dump has one line per node with role, name, value, state and bounds, so
a one-pixel move shows. Lines marked `volatile` (Diagnostics timings) differ
on every run.

## Traps that have cost real time

- **A rule cannot resize a box the page sized.** `.box = { .h = N }` locks CSS
  out of that axis; usually why a stylesheet "only changes the color".
- **`core/ui/style_map.c` is the only writer of `nk_style`.** Anything set
  elsewhere is lost on the next theme change.
- **A box is placed one frame late and found by its name.** A widget whose
  label changes every frame needs a stable `.name`.
- **Hover redraws only across a hot rectangle.** Motion runs a frame only when
  it crosses a rectangle `hot_push` registered the frame before.
- **A percentage is not a length to `core/css/css.c`.** `width: 100%` is
  dropped and a lower rule shows through. `[type=range]` reads as
  `input.type-range`.
- **Vertices snap to whole pixels.** Sub-pixel corrections vanish; abutting
  rects can round apart into a seam.
- **SDL's software renderer redraws rectangles its own way**, truncating
  position and size apart at fractional scales. `nk_sdl_quad` draws them from
  device pixels first.
- **A one-pixel stroke is two pixels.** `nk_stroke_rect` casts to `short` and
  anti-aliasing splits it. Borders are `reaktor_edge_round`: a ring mask for
  corners, runs cut from its middle texels.
- **Rounded fills are masks.** Fill anti-aliasing is off on the software
  renderer, so `reaktor_render` swaps each rounded fill for
  `reaktor_fill_round`, relinking the command list (it ends where a `next`
  reaches `ctx->memory.allocated`).
- **Measure the present, not the draw.** Without a GPU most of a frame is
  `SDL_RenderPresent`; the Diagnostics page and `bench --no-vsync` split it.
- **Text the atlas lacks is not Nuklear's.** Strings with Japanese, Arabic and
  the like are measured and drawn by `core/text/text.c` through a custom
  command. mojibake 0.5.0 records `byte_offset` at a character's last byte.
- **Only Nuklear's active window gets input.** Others carry a sticky
  `NK_WINDOW_ROM`. A layer that must not take input uses `reaktor_layer_begin`
  with `HOLD_ALL`; `reaktor_layer_focus` hands focus back.
- **A field's keys are its own.** Edit state lives on the widget's window, so
  navigation asks `app->editing` before taking arrows, Home, End, Enter or
  Space. The runtime keeps Home and End, which would scroll the panel.
- **A field's value is not its buffer.** `nk_edit_string` leaves bytes past
  the length; a length not from the field's own last frame reseeds it empty.
- **An empty edit has no text pointer.** `nk_str_get_const` is NULL while
  `nk_str_len_char` is positive.
- **Web: a tap focuses the canvas.** Only canceling the touch `pointerdown`
  stops it.
- **Web: SDL delivers events a frame late**, so the page's own typing goes
  through SDL's queue to keep its order.
- **Web: a hidden page runs no frames.** No `requestAnimationFrame` in a
  background tab, so the canvas and a11y tree stay empty. Check
  `document.visibilityState`.
- **The shared library exports only `tools/reaktor.map`.** A new public call
  goes in the map, or `check-exports.sh` fails; a struct an app sees that
  changes needs a new SOVERSION and `check-abi.sh -u`. OpenBSD installs no
  unversioned `libreaktor.so`.

## House rules

- Dependencies are submodules under `external/`, used as they ship. Never
  copy a value out of one or generate a header from one. The exception is
  `REAKTOR_BUILTINS` in CMakeLists.txt: Water.css's palettes and `src/parts/`,
  the seven Ionicons the widgets draw, the Aileron fonts, the mark and
  reaktor.css, compiled in byte for byte. SDL is built with
  `core/sdl/no_dynapi.h`; NOTICE.md says why.
- Applications see only `include/reaktor/`; `App` is opaque. What a sample
  needs becomes a public call. MSVC treats implicit declarations as errors.
- American English.
- No environment variables: overrides are command-line flags the runtime
  strips from `argv`.
- `docs/DOCUMENTATION.md` is the only manual; `PERFORMANCE.md` holds measured
  numbers only, with the machine named.
- An optional module is an interface library plus a `_none` twin, switched by
  `REAKTOR_<NAME>` (default `ON`). Off, pixels and `--a11y-dump` must not
  change beyond what the module draws. Localization and text shaping are
  separate modules.
