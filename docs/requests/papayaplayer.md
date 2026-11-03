# Requests from PapayaPlayer

What PapayaPlayer (a media player built on Reaktor) needs from Reaktor,
checked against revision `5a8b7f3`. Each section says what is missing, why the
app cannot do it on its own, a suggested shape and how to check it. No
Reaktor code is changed here; this file is the request.

Contents:

1. [Linear gradients in CSS](#1-linear-gradients-in-css)
2. [An option for HDR output](#2-an-option-for-hdr-output)
3. [SDL's GPU renderer, for app shaders](#3-sdls-gpu-renderer-for-app-shaders)
4. [The layout stays stale after a resize](#4-the-layout-stays-stale-after-a-resize)
5. [A quiet list header](#5-a-quiet-list-header)

## 1. Linear gradients in CSS

**What PapayaPlayer needs:** `linear-gradient()` as a background in Reaktor
stylesheets, so the "accent" controls style can be written in CSS. Decided
2026-10-30 (TECH-STACK.md, UI direction): gradients come from Reaktor's CSS,
not from PapayaPlayer drawing them itself.

Reaktor revision checked: `5a8b7f3` (the submodule in PapayaPlayer).

### Where it is used

From `design/mockups.html`, screen 02 (video, accent) and the audio bars:

| Element | Gradient |
|---|---|
| Seek bar fill, volume fill | horizontal / vertical, guava `#ff3d6e` to papaya `#ff7a45` |
| Play button (pill), folder and file pills | horizontal, same colours, with `border-radius` |
| Chosen row of the settings menu | horizontal, the same colours at 25 % / 18 % alpha |
| Shade behind the control bars (both styles) | vertical, black at 55 - 75 % to transparent |

So: two colour stops are enough for all of it; angles `to right`, `to top`
and `to bottom`; colours with alpha; rounded corners.

### What Reaktor has today

- `background` and `background-color` take a colour only (`core/css/css.c`,
  `K_BG`, around line 788: it takes the last colour in the value).
- Boxes are filled with one colour by `reaktor_fill_round()`
  (`core/render/draw.c`, around line 553), called from `core/ui/declare.c`
  around line 478 with `s.bg`.
- Nuklear already has `nk_fill_rect_multi_color()` (a colour per corner),
  which draws a two-stop gradient on a square rectangle.

### Suggested shape

1. **Parse** `background: linear-gradient(<angle>, <colour> [<pos>], <colour> [<pos>], ...)`
   with `to right | to left | to top | to bottom` and `<n>deg`. Store it in the
   style next to `bg` (say `bg2[4]` plus an angle, or a small stop list).
   Falling back to the first stop where a gradient is not drawable keeps old
   code paths working.
2. **Draw** in `reaktor_fill_round()` (or a sibling): for square corners,
   `nk_fill_rect_multi_color()`; for rounded corners, the rounded shape's
   vertices coloured by their position along the gradient axis (the corner
   fans already exist for the solid fill).
3. **Hover / active states** take gradients the same way (`core/ui/style_map.c`
   copies `bg` today).
4. **Inspector:** show the gradient in the background row (`runtime/inspector.c`).

More than two stops and other angles are nice to have; PapayaPlayer's design
needs only two stops and the four directions.

### Checks

- `tests/csstest.c`: the parse of each form above, and that a plain colour still
  parses as before.
- The showcase with a gradient button, pill and progress bar, rounded and
  square, in `--shot` output: the colours at both ends match the stops, no seam
  at the corners.
- Colours with alpha over another fill blend as solid ones do.

### In PapayaPlayer once it exists

`app/main.c`'s stylesheet gets the accent classes (`.accent .seek-fill`,
`.accent .play`, ...); the control code itself does not change.

## 2. An option for HDR output

**What PapayaPlayer needs:** a launch option that creates Reaktor's renderer
with an HDR output colorspace, so HDR10 video can light up an HDR screen.
Today HDR video is tone-mapped down to SDR on every screen.

Reaktor revision checked: `5a8b7f3` (the submodule in PapayaPlayer).

### Why it has to be Reaktor

SDL fixes a renderer's output colorspace when the renderer is created
(`SDL_PROP_RENDERER_CREATE_OUTPUT_COLORSPACE_NUMBER`). Nothing can change
it afterwards. Reaktor creates the window and renderer itself, in
`runtime/app.c` (around line 842) with `SDL_CreateWindowAndRenderer()`, which
takes no properties. The app never sees renderer creation, so it cannot ask
for HDR. Secondary windows (`runtime/window.c` around line 184) are created
the same way.

This was found in PapayaPlayer's phase 0 spike S3 (`spikes/RESULTS.md`).

### What already works without it

- SDL's Direct3D / Metal renderers take P010 textures with a BT.2020 / PQ
  colorspace, convert YUV themselves, and tone-map HDR10 to the SDR output.
- PapayaPlayer now gives each HDR10 texture a white point (203 cd/m²,
  BT.2408) and a headroom from the content's MaxCLL or mastering peak
  (`SDL_PROP_TEXTURE_CREATE_SDR_WHITE_POINT_FLOAT`,
  `SDL_PROP_TEXTURE_CREATE_HDR_HEADROOM_FLOAT`). Without a headroom, SDL clips
  everything brighter than white.

So SDR screens are handled. Only real HDR output is missing.

### Suggested option

```c
/* in reaktor_launch */
int hdr;   /* 1: ask for an HDR output where the display and renderer have one */
```

When it is set and the renderer is not software:

1. Create the window first, then the renderer with properties:
   ```c
   SDL_PropertiesID p = SDL_CreateProperties();
   SDL_SetPointerProperty(p, SDL_PROP_RENDERER_CREATE_WINDOW_POINTER, win);
   SDL_SetStringProperty(p, SDL_PROP_RENDERER_CREATE_NAME_STRING, name); /* the --renderer choice */
   SDL_SetNumberProperty(p, SDL_PROP_RENDERER_CREATE_OUTPUT_COLORSPACE_NUMBER,
                         SDL_COLORSPACE_SRGB_LINEAR); /* scRGB; SDL_COLORSPACE_HDR10 also works on D3D */
   ren = SDL_CreateRendererWithProperties(p);
   ```
   If that fails, fall back to the normal SDR renderer as today.
2. `SDL_COLORSPACE_SRGB_LINEAR` is the simplest choice for the UI. SDL converts
   sRGB draw colours and textures into it, scaled by the display's SDR white
   level (`SDL_PROP_RENDERER_SDR_WHITE_POINT_FLOAT`). Reaktor's widgets
   should then look as they do now, but this needs checking against
   Reaktor's own pixels (`--shot`).
3. Follow the display: Windows lets the user switch HDR on and off at any
   time, and a window can move between an HDR and an SDR monitor. SDL reports
   both with `SDL_EVENT_WINDOW_HDR_STATE_CHANGED`; the renderer's
   `SDL_PROP_RENDERER_HDR_ENABLED_BOOLEAN` and `..._HDR_HEADROOM_FLOAT` give
   the current state. On an SDR display a linear output still works: SDL
   tone-maps into it as it does now.
4. The same for secondary windows (`runtime/window.c`).

The software renderer has no HDR output, so the option does nothing there.

### Checks

- `--shot` / `--a11y-dump` of the showcase with the option on and off, on an
  SDR display: the pixels should match within rounding. The linear output
  path changes how blending is done, which may show in anti-aliased edges and
  in alpha.
- On an HDR display (Windows with HDR on, or a Mac with an XDR screen): the UI
  at normal brightness, and an HDR10 P010 texture brighter than white.
- Toggle HDR in Windows settings while running: the window keeps drawing.

### In PapayaPlayer once it exists

`app/main.c` sets `.hdr = 1` in its `reaktor_launch`. Nothing else changes:
the HDR10 textures already carry their colorspace, white point and headroom.


## 3. SDL's GPU renderer, for app shaders

**What PapayaPlayer needs:** a way to run its own fragment shaders when
drawing video: BT.2390 tone mapping on SDR screens (SDL's built-in operator
is not BT.2390), full chroma for 4:2:2 / 4:4:4 (SDL has no planar
4:2:2 / 4:4:4 textures, so PapayaPlayer averages the chroma down to 4:2:0
today), and later Lanczos scaling and deinterlacing.

**Why it has to be Reaktor:** `SDL_CreateGPURenderState()` (SDL 3.4) only
works on a renderer backed by an SDL GPU device, that is SDL's `gpu`
renderer. Reaktor builds SDL with `SDL_GPU` off, so the renderer is always
the native Direct3D / Metal / OpenGL one (PapayaPlayer spike S4,
`spikes/RESULTS.md`).

**Suggested option:** build SDL with `SDL_GPU` on, and a launch field such as
`const char *renderer; /* NULL: as now; "gpu": SDL's GPU renderer */` passed
as `SDL_HINT_RENDER_DRIVER` (or as the renderer name) before the window and
renderer are created. With it, PapayaPlayer would create a
`SDL_GPURenderState` with its shaders (built from HLSL with SDL_shadercross)
and set it while drawing the video texture.

**Checks:** the showcase under the `gpu` renderer on Windows (D3D12 / Vulkan),
macOS (Metal) and Linux (Vulkan) matches the default renderer within
rounding; the software fallback still works where no GPU device exists.

## 4. The layout stays stale after a resize

**Status:** suspected, not yet confirmed by watching a real window. Found in
PapayaPlayer on Windows; worked around there in `eafbafb`.

### What the user saw

After resizing PapayaPlayer's window, the window took "several seconds to
update". PapayaPlayer was idle at the time (paused, or no file open), so
nothing was animating.

### Why it happens

Three parts of Reaktor combine:

1. **Boxes are placed a frame late.** A box gets its rectangle from the
   previous frame and is found again by its `.name` (CLAUDE.md, "Traps that
   have cost real time"). The first frame at a new size is therefore laid out
   against the old sizes.

2. **A resize asks for exactly one frame.** `SDL_EVENT_WINDOW_RESIZED`,
   `PIXEL_SIZE_CHANGED`, `EXPOSED`, `MAXIMIZED` and the others set
   `app->dirty` and `app->must_draw` (`runtime/app.c`, the event switch around
   lines 981-1010). `must_draw` only stops the frame-hash check
   (`runtime/app.c` around 1431) from skipping that one frame. Nothing asks
   for a second one.

3. **An idle app draws nothing more.** `app_iterate` returns early while
   `!app->dirty` (`runtime/app.c` around 1690). The stale layout stays on
   screen until something else marks the window dirty: mouse motion over a
   hot rectangle, a key, a timer or a wake.

In PapayaPlayer the next wake is the control bar's auto-hide timer, 2.5 s
later. That matches "several seconds". Any Reaktor app with nothing animating
should show the same stale frame after a resize, until the next input.

### Reaktor already handles the same case elsewhere

The "settle" mechanism exists, but a resize never starts it:

| Event | What Reaktor does (`runtime/app.c`) |
|---|---|
| Mouse drag ends | `drag_end()` sets `restore_rate = 2` (around 961) |
| Mouse wheel | `restore_rate = 2`, because "Nuklear scrolls a panel at its end, after drawing what it holds" (around 1055) |
| Hover redraw | `restore_rate` at least 1 (around 1273) |
| Keyboard click release | `restore_rate = 2` (around 1486) |
| Window resize | `must_draw = 1` only: **no follow-up frame** |

At the end of a frame, `restore_rate > 0` counts down, sets `dirty` and calls
`reaktor_app_wake()` (around 1637-1645). That is exactly the extra frame a
resize needs.

### Suggested fix in Reaktor

In the second event switch (the `must_draw` one, around 1000-1010), also
schedule a settle frame for the size-changing events:

```c
case SDL_EVENT_WINDOW_RESIZED:
case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
case SDL_EVENT_WINDOW_MAXIMIZED:
case SDL_EVENT_WINDOW_RESTORED:
case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
    if (!app->dragging && app->restore_rate < 2) app->restore_rate = 2;
    break;
```

(`app->must_draw = 1` stays as it is.)

A more general fix: keep drawing while the layout still moves. After a frame,
compare the boxes' rectangles with the previous frame's, and draw again
(bounded, e.g. at most 3 frames) while any changed. That would also cover
other one-frame-late cases, not just resizes.

#### How to check it

- Run a sample (e.g. `showcase`) with nothing animating, resize the window,
  and stop moving the mouse. Before the fix, boxes that depend on the window
  size should sit at their old place until the next input; after it, they
  should settle at once.
- The regression oracle: a `--shot` / `--a11y-dump` taken after a scripted
  `SDL_SetWindowSize` should give the same bounds as one taken from a fresh
  start at that size.

### Not covered by this

- **Lag during the drag itself.** On Windows, the move/size loop is modal.
  If the window doesn't update *while* the edge is being dragged, that is a
  separate issue: whether SDL's live-resize callback runs `SDL_AppIterate`
  under Reaktor's `SDL_HINT_MAIN_CALLBACK_RATE` settings.
- Whether Reaktor's own samples show the stale frame has not been checked yet.

### PapayaPlayer's workaround (remove once Reaktor is fixed)

`app/main.c`, at the top of `page()`:

```c
/* Reaktor places boxes a frame late, so a new size needs a second frame;
 * when paused nothing else would draw it until the bar hides. */
if (win_w != activity.win_w || win_h != activity.win_h) {
    activity.win_w = win_w;
    activity.win_h = win_h;
    reaktor_wake(app);
}
```

Reaktor revision checked: `5a8b7f3` (the submodule in PapayaPlayer).

## 5. A quiet list header

**What PapayaPlayer needs:** a `reaktor_list` header row in a dark, low-contrast
colour of its own choosing (its playlist uses `--table: #232027` over rows of
`#18161b` / `#1d1b21`).

**What happens today:** `core/ui/views.c` (`reaktor_list`, around line 377)
takes `thead`'s background or `--table`, then passes it through
`reaktor_visible()` against the page colour and the odd-row colour. A colour
that close to the rows counts as not visible, so the header falls back to the
accent: PapayaPlayer's playlist gets a bright papaya-orange header row.

**Suggested shape:** when the stylesheet sets the header colour explicitly
(`thead { background-color: ... }` or `--table`), use it as written and keep
the visibility fallback only for the built-in default. Or a flag on the spec,
such as `.plain_header = 1`, or `.header = 0` to hide the row.

**Checks:** the showcase's list with `--table` set close to the rows shows that
colour in `--shot`; with nothing set, the header looks as it does now.
