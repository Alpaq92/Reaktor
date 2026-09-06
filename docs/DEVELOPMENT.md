# Development

How the pieces fit, and what to know before changing them.

## The pipeline

```
third_party/tinycss/src/*.css
        |
        v
  src/cssflat.c      narrows the text to what libcss implements,
        |            and resolves every var() before it gets there
        v
  src/style.c        libcss: parse, match a selector, compute
        |
        v
  apply_widget_style()   in src/main.c - the one place that writes nk_style
        |
        v
  Nuklear draws
```

Three things are worth knowing about that chain.

**`cssflat` exists because libcss is not a browser.** It parses type, `#id`,
`.class` and `:status` selectors joined by descendant combinators, and 85
properties. It has no custom properties — `css_computed_style_t` declares a
`custom_props` field, but nothing in `lib/css` reads it — and no `@media` at
all. Since tiny.css keeps its whole palette in `:root` custom properties, the
substitution has to happen before the engine sees the text. That pass also
drops `@media` blocks and any selector carrying `[attr]` or a `::pseudo`,
which the parser cannot represent, and hands the resolved palette back so the
surfaces this app paints itself can read the same values the rules do.

**`apply_widget_style()` is the only writer of `nk_style`.** Every widget
style — button, input, combo, tab, progress, checkbox, slider, tooltip,
scrollbar — is set there, from a resolved rule or a palette token. Setting one
somewhere else works right up until a theme change reloads the sheets and
undoes it.

**Style lookups are cached, and the rule store is rebuilt per load.** A
resolved selector is kept in a small fixed cache; `curie_style_init()` destroys
and re-creates libcss's rule store on every call, because parsing adds rules to
a global store and nothing takes the previous sheet's rules back out. Without
that, switching schemes stacked another copy of tiny.css each time.

## The frame loop

There is no loop. SDL is told `SDL_HINT_MAIN_CALLBACK_RATE = "waitevent"`, so
`SDL_AppIterate` runs when an event arrives and not otherwise, and it returns
immediately unless `app->dirty` is set.

What sets `dirty`:

- a widget the shell knows about being clicked, typed into or dragged;
- the pointer crossing one of the **hot rectangles** registered during the
  previous frame (`curie_hot()` and friends) — that is how hover states repaint
  without polling;
- the window moving, resizing or changing scheme.

While a mouse button is held the callback rate switches to the display's
refresh, and a two-frame `restore_rate` settle runs after the release, so the
frame that completes a click is not also the frame that stops scheduling
frames.

This is why the app measures 0.00% CPU at rest, and why **adding a timer or an
unconditional repaint to make something update is the wrong fix**. Mark the
thing that changed instead.

Frame gaps are measured between the last two drawn frames, not counted over a
second: a second only advances when a frame is drawn, so a counted figure
reads stale for exactly as long as the app is quiet.

## Fonts and the atlas

Five sizes are baked — 12, 13, 14, 16 and 19px — at the display scale, into a
single Nuklear atlas texture. `curie_font(app, px, bold)` returns the nearest
baked size; `bold` is accepted and ignored — not for want of a face, since
`Aileron-Bold.otf` sits beside the regular, but because baking it would put a
second set of five faces in the atlas and roughly double the largest allocation
the app makes.

The face is Aileron, CC0, vendored under `assets/fonts/`. It replaced Karla,
which came from inside the Nuklear submodule with no licence file anywhere near
it — see [NOTICE.md](NOTICE.md#fonts) for where Aileron's terms were traced to.
It is an OTF, which costs nothing: stb_truetype reads CFF outlines too. Changing
face is one `#define FONT_FILE` in `src/main.c`.

The atlas is 1024x128 RGBA32, half a megabyte, and is the largest single
allocation the application makes. Two things got it there: oversampling is 2x1
rather than 3x2 (oversampling costs exactly its area), and the 23px step was
dropped because nothing ever asked for it.

**The remaining lever, not yet taken.** Baking `NK_FONT_ATLAS_ALPHA8` instead
of RGBA32 would take the atlas to roughly 0.13 MB with no visible difference —
Nuklear draws glyphs by modulating a colour, so the three colour channels
carry nothing. It is not done because `nk_sdl_font_stash_end()` in Nuklear's
SDL3 backend hard-codes the RGBA32 bake, and this project does not edit
submodules. Taking it would mean vendoring that backend — roughly 700 lines,
MIT — into `src/` and maintaining it against upstream. Worth doing if the
memory matters more than the drift: against a process whose private bytes vary
by about 1.8 MB run to run because of the graphics driver, 0.4 MB is below the
noise floor.

## Icons

Every icon is an Ionicons SVG opened from the submodule at runtime, its stroke
recoloured to the theme's `--text-muted` (or an explicit colour), rasterised
by plutosvg at twice the drawn size and cached as an SDL texture. No path data
and no colour is copied into this tree. The cache is cleared on every theme
change, since the colour is baked into the raster.

The one exception is the window icon, which `tools/mkicon.c` bakes during the
build — a window needs its icon before there is a renderer to make textures
with.

## Environment variables

All read once at startup. The first three exist so a screenshot lands on the
same view every time instead of being clicked into place.

| Variable | Effect |
| --- | --- |
| `CURIE_TAB` | Which tab opens (0 = Login … 6 = Diagnostics) |
| `CURIE_SCROLL` | How far that page starts scrolled |
| `CURIE_THEME` | 0 system, 1 light, 2 dark |
| `CURIE_SCALE` | Overrides the display scale, in the spirit of `GDK_SCALE` |
| `CURIE_ROOT` | Where the assets are, if not found by walking up to `.curie-root` |
| `CURIE_RENDERER` | `auto`, `gpu` or `software` |
| `CURIE_VSYNC` | `0` turns vsync off |
| `CURIE_AA` | `0` turns Nuklear's anti-aliasing off |
| `CURIE_FRAME_RATE` | A frame cap, or `0` for uncapped, instead of `waitevent` |
| `CURIE_REDRAW` | `always` draws every callback — for profiling, not for use |
| `CURIE_STATS` | Logs frame timings once a second |

The Diagnostics tab reports all of it, plus where each frame's milliseconds
went and where the memory went. Prefer it to a guess: several plausible
optimisations here turned out to be measurably worse, and one 4x CPU
regression was invisible in the build, render and present timings because the
cost was inside the renderer.

## The web build

Same sources, same `CMakeLists.txt`. The Emscripten-specific parts:

- Assets are packaged with `--preload-file`, and only the files the sources
  actually name. The list is built by grepping `src/*.c` at configure time,
  which is what took the bundle from 3.55 MB to 1.30 MB.
- `-Oz` and `--closure 1`. `-Oz` is what runs Binaryen's `wasm-opt`; there is
  no separate step to add.
- `tools/shell.html` is the page. Its loading overlay sets
  `pointer-events: none` — without that, a full-viewport overlay swallows
  every canvas click and the app looks dead.
- `mkicon` is not built; there is no window to give an icon to.

## Nuklear behaviours that have already cost an afternoon

Each of these was found the hard way. They are in the library, not in this
code, and none of them is a bug.

- **Buttons fire on press.** `nk_input_is_mouse_pressed` is `down && clicked`,
  which needs a frame while the button is held. A browser delivers press and
  release in one task, so that frame never happens and the button never fires.
  The build defines `NK_BUTTON_TRIGGER_ON_RELEASE`.
- **A button's content rect is bounds − padding − border − *rounding*, on both
  axes.** A generous radius from the stylesheet can drive a small button's
  content rect negative, at which point its symbol or image silently vanishes.
  `compact_push()` in `showcase.c` exists for exactly this.
- **`nk_stroke_rect` is asymmetric.** A 2px border measures 1px on the left
  and top and 2px on the right and bottom. Combos therefore set
  `combo.border = 0`, and the page strokes the frame itself, inset by half the
  width.
- **`nk_draw_button_text_symbol` always centres the label.** The alignment
  argument only chooses which side the glyph goes on.
- **`nk_do_selectable_image` reads inverted.** `NK_TEXT_ALIGN_LEFT` pins the
  image to the *right* edge.
- **A tree header's border rect is always square.** `NK_TREE_TAB` fills the
  header twice — the border rect at a literal `0` rounding, then the
  background inset by `tab.border` at `tab.rounding`. Whatever radius the
  stylesheet asks for, the outer rect's colour shows through at the corners,
  so the header reads as a square box. The fix is to paint that rect in the
  surface behind the header. `nk_combo_begin_color` has the same literal `0`.
- **There is one `window.rounding` for every panel**, popups included. Set it
  around the popup, not globally, or the window corners round twice.
- **`nk_menubar_begin` must precede all other layout** in its panel, and pins
  its row out of any scrolling — hence its own group.
- **`nk_group_begin` returns 0 when scrolled out of view.** Returning early
  from a page on that blanks everything below it.
- **`NK_POPUP_DYNAMIC` does not fill its body at begin**, and strokes its
  border at pre-shrink bounds, so `nk_tooltip` shows a ghost frame. The
  tooltip here is drawn straight onto the canvas instead.
- **`nk_layout_space` does not nest.**
- **`nk_image` stretches to its widget slot**, whatever raster size you asked
  for.
- **`nk_rule_horizontal` fills its whole widget rect** — the row height *is*
  the line thickness.
- **Some widgets have no aligned variant.** `nk_checkbox_flags_label` is one;
  `showcase.c` wraps `nk_checkbox_label_align` in the same four lines the
  library would have.
- **The SDL backend's malloc-only allocator is not a leak.**
  `nk_buffer_realloc` copies and frees the old pointer itself.

## Loose ends

- `src/cssflat.h` still describes the pass in terms of Open-Color and
  `app.css`, both of which are gone. The code is current; the comment is not.
- `assets/demo.html` refers to `third_party/pico`, a submodule that no longer
  exists, and nothing loads it.
