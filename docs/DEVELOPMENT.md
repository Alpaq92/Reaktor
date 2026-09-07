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

The atlas is 1024x128 RGBA32, half a megabyte, and lives on the GPU — it is a
texture, not process heap, so it appears in neither memory counter the
Diagnostics tab reports. Three things got it there: oversampling is off in both
axes (it costs exactly its area — the 3x2 default stores six copies of every
glyph at every size, which made the atlas 1024x512), the 23px step was dropped
because nothing asked for it, and `nk_font_atlas_cleanup` releases the five
copies of the font file the bake keeps.

Two things about rebaking, both found by audit rather than by symptom:

- **`nk_sdl_font_stash_begin` does not free the previous atlas.** It calls
  `nk_font_atlas_init`, which zeroes the struct outright, dropping the last
  bake's configs, blobs, fonts and glyph array unfreed. It only shows when the
  display scale changes and `rebuild_font` runs a second time. `rebuild_font`
  therefore keeps the atlas pointer and calls `nk_font_atlas_clear` first.
- **The face changes the atlas size.** Aileron's glyphs are wider than the
  previous face's and pushed the packer from 1024x128 to 1024x256 on their own.
  Check the atlas row in Diagnostics after any font change.

**The atlas is 8-bit indexed, not RGBA32.** A baked glyph is coverage and
nothing else: upstream's RGBA32 path runs `nk_font_bake_convert`, which writes
`((alpha << 24) | 0x00FFFFFF)` for every pixel, so three of every four bytes
are the constant `0xFF`. Colour comes from the vertex, not from the atlas.

SDL3 has no A8 texture format, so a plain ALPHA8 bake would have to be expanded
back to RGBA before upload and would save nothing. The route that works is
ALPHA8 plus `SDL_PIXELFORMAT_INDEX8` with a 256-entry palette whose entry `i`
is white at alpha `i` — which reproduces the RGBA32 texture exactly, at
**128 KB instead of 512 KB**. It also keeps a 524 KB RGBA conversion buffer out
of the startup peak, since `nk_font_atlas_bake` holds it live alongside the
alpha8 one.

This is why `src/nk_sdl3_renderer.h` exists: the format is chosen inside
`nk_sdl_font_stash_end`, so the backend had to be vendored. Four lines differ
from upstream, all marked `CURIE`. Three things were verified before taking it,
and are worth re-checking after any re-vendor:

- **Every SDL3 backend registers `SDL_PIXELFORMAT_INDEX8`** — D3D11, OpenGL,
  Metal, Vulkan, GLES2 and software — so no platform loses.
- **Vertex-colour modulation composes with the palette.** If it did not, every
  glyph would render white. Check a screen with text in several colours.
- **Fractional display scales still look right.** D3D11 forces
  `SDL_SCALEMODE_NEAREST` for indexed textures and does linear filtering in the
  palette shader instead. Checked at `CURIE_SCALE=1.5`.

Whether it is worth anything depends on the machine: on real hardware the atlas
is VRAM, so this buys nothing in private bytes. On the reference machine, which
has no GPU and rasterises through WARP, it is about 0.4 MB.

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

For memory specifically, know what the numbers are before steering by them.
The tab reports **private bytes** (commit — the process's own) and **working
set** (residency, shared driver pages included); the startup breakdown is
working set throughout. Runs of the same binary vary by about 2 MB, so a
single reading proves nothing — see
[PERFORMANCE.md](PERFORMANCE.md#memory) for what that costs in sample size.

When that is not enough, `tools/vmwalk.c` attributes another process's private
bytes to named buckets — heap, large private blocks, thread stacks, dirtied
image pages per module — and prints what it could not account for rather than
pretending the buckets are exhaustive. It is not built by default:
`cmake --build build --target vmwalk`, then `build/vmwalk.exe <pid>`. It reads
the target through `VirtualQueryEx` and `QueryWorkingSetEx` only, so it
allocates nothing there and faults nothing in.

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
  `nk_buffer_realloc` copies and frees the old pointer itself. It is, however,
  a full malloc-and-memcpy on every growth, and `nk_sdl_render` re-inits and
  frees its vertex and element buffers every frame.
- **`nk_sdl_font_stash_begin` leaks the atlas it replaces.** See
  [Fonts and the atlas](#fonts-and-the-atlas).
