# Development

How the pieces fit, and what to know before changing them.

## The pipeline

```
  third_party/tinycss/src/*.css        the stylesheet, read as it ships
              │
              ▼
  src/cssflat.c                        narrow to what libcss implements,
              │                        and resolve every var() first
              ▼
  src/style.c                          libcss: parse, match, compute
              │
              ▼
  apply_widget_style()   in main.c     the one place that writes nk_style
              │
              ▼
  Nuklear                              draws it
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
single Nuklear atlas texture, plus one face of `Aileron-Bold.otf` at 16px for
the titlebar's name. `curie_font(app, px, bold)` returns the nearest baked
size; `bold` is honoured only at that one size, because a bold at all five
would put a second set of faces in the atlas and roughly double the largest
allocation the app makes. The one extra face left the atlas where it was:
1024x128.

The face is Aileron, CC0, vendored under `assets/fonts/`. It replaced Karla,
which came from inside the Nuklear submodule with no licence file anywhere near
it — see [NOTICE.md](NOTICE.md#fonts) for where Aileron's terms were traced to.
It is an OTF, which costs nothing: stb_truetype reads CFF outlines too. Changing
face is one `#define FONT_FILE` in `src/main.c`.

The atlas is 1024x128 and 8-bit indexed — **128 KB**. Three things got it
there: oversampling is off in both axes (it costs exactly its area, and the 3x2
default stores six copies of every glyph at every size, which made the atlas
1024x512), the 23px step was dropped because nothing asked for it, and
`nk_font_atlas_cleanup` releases the five copies of the font file the bake
keeps.

Where that 128 KB lives depends on the machine — see
[PERFORMANCE.md](PERFORMANCE.md).

Two things about rebaking, both found by audit rather than by symptom:

- **`nk_sdl_font_stash_begin` does not free the previous atlas.** It calls
  `nk_font_atlas_init`, which zeroes the struct outright, dropping the last
  bake's configs, blobs, fonts and glyph array unfreed. It only shows when the
  display scale changes and `rebuild_font` runs a second time. `rebuild_font`
  therefore keeps the atlas pointer and calls `nk_font_atlas_clear` first.
- **The face changes the atlas size.** Aileron's glyphs are wider than the
  previous face's and pushed the packer from 1024x128 to 1024x256 on their own.
  Check the atlas row in Diagnostics after any font change.

**The atlas is 8-bit indexed rather than RGBA32** — 128 KB instead of 512 KB,
and one fewer 524 KB buffer on the startup peak. The reasoning is at the code,
in `src/nk_sdl3_renderer.h`, which was vendored for this: the format is chosen
inside `nk_sdl_font_stash_end`. It has since picked up the rest of what the
renderer section below describes — a white texel of its own, vertices on the
grid for the software path, glyph quads on the grid for hardware, and fills and
strokes feathered separately — every hunk marked `CURIE`.

Three things were verified before taking it, and are worth re-checking after
any re-vendor:

- **Every SDL3 backend registers `SDL_PIXELFORMAT_INDEX8`** — D3D11, OpenGL,
  Metal, Vulkan, GLES2 and software — so no platform loses.
- **Vertex-colour modulation composes with the palette.** If it did not, every
  glyph would render white. Check a screen with text in several colours.
- **Fractional display scales still look right.** D3D11 forces
  `SDL_SCALEMODE_NEAREST` for indexed textures and does linear filtering in the
  palette shader instead. Checked at `CURIE_SCALE=1.5`.

It buys about 0.4 MB on the reference machine and nothing on a GPU, where the
atlas is VRAM.

## Icons

Every icon is an Ionicons SVG opened from the submodule at runtime, its stroke
recoloured to the theme's `--text-muted` (or an explicit colour), rasterised
by plutosvg at twice the drawn size and cached as an SDL texture. No path data
and no colour is copied into this tree. The cache is cleared on every theme
change, since the colour is baked into the raster.

## The mark

`branding/` holds the project's own icon rather than a borrowed glyph: a C over
a hexagon with the radiation trefoil in its counter. It is authored art, checked
in as SVG, PNG and `.ico` in two inks — six files, none of them generated at
build time.

**The app uses one of them: `curie-icon.png`, the dark ink**, named once as
`CURIE_MARK` in `src/curie.h`. The mark carries its own colours and sits on a
yellow ground, so it reads on either theme; a title bar that swapped inks with
the theme just made the same logo look like two logos. The light-ink files stay
in `branding/` for whatever needs a mark on a dark ground — a slide, a README on
a dark site — but nothing in `src/` opens them.

**Why the PNG and not the SVG.** plutovg starts a dashed stroke at the top of a
circle, where SVG says it starts at three o'clock. The mark's C is a ring with
an 84° wedge left out by `stroke-dasharray`, so plutosvg renders it a quarter
turn off — the opening lands at the top and the C reads as a U. Nothing else in
the app hits this, because the Ionicons draw arcs as path data rather than as
dashes, and the PNG has no argument with any renderer. `curie_image_surface`
loads it through plutovg's own image reader and box-filters it down: bilinear
would sample four texels out of the 28×28 block that 1024px collapses into at
title-bar size.

The dash bug is not a reason to change renderer, and the field was surveyed
before deciding that. What Curie needs is small — the 22 Ionicons it draws use
`path`, `line`, `circle`, `polyline` and `rect` with stroke width, cap, join and
miterlimit, nothing else — so the question is not features but rasteriser
quality at 16-18px, licence, and whether anyone still maintains it.

| | Language | Licence | Verdict |
| --- | --- | --- | --- |
| plutosvg + plutovg | C | MIT | in use; analytic coverage, and its image reader loads the mark |
| NanoSVG | C | zlib | rejected — own README: “only renders flat filled shapes”, “not particular fast or accurate” (5 subsamples/px), and marked not actively maintained |
| LunaSVG | C++ | MIT | pointless — it renders *through* plutovg, so the same rasteriser plus a C++ API |
| libsvgtiny | C | MIT | parser only, no rasteriser, and pulls libdom + libwapcaplet + libparserutils |
| Blend2D | C++/C API | zlib | no SVG parser at all |
| resvg | Rust | MIT/Apache-2.0 | the most correct renderer going, but it puts a Rust toolchain in the build |
| ThorVG | C++/C API | MIT | the only real alternative — see below |

**ThorVG is the one that would work.** MIT, actively maintained, a C API
(`tvg_swcanvas_set_target` renders into a caller-owned buffer),
`TVG_COLORSPACE_ARGB8888S` gives straight alpha directly so `curie_unpremultiply`
could go, it loads PNG as well as SVG so it would cover the mark too, and it
builds for WebAssembly. Its SVG Tiny 1.2 coverage is a superset of what the
icons use. The cost is that the core is C++ — though this build already links as
CXX, because SDL compiles C++ on Windows — and that it carries a great deal that
this app will never call: Lottie, GL/WebGL/WebGPU backends, a task scheduler.

Nothing here is worth a rewrite to fix one glyph that a PNG already fixes. The
note exists so the question is not reopened without new information: a second
dasharray-shaped bug, or plutovg going unmaintained, would be that information.

Three places consume the mark, and one constant keeps them from drifting apart:

- the title bar, at `MARK_SIZE`;
- `SDL_SetWindowIcon`, at 64px, for the task bar and Alt-Tab;
- `curie-icon.ico`, copied into the build directory and named by a two-line
  generated `.rc` — Explorer takes the icon from a linked Win32 resource, so it
  has to exist before the linker runs.

The first two go through the same renderer as an Ionicon — the mark is just an
SVG under the repo root — but with NULL for both colours, which renders the
file as authored. The stroke and fill substitutions exist for the Ionicons and
are skipped here.

Colours are USWDS system tokens (`yellow-20v`, `gray-90`, `gray-1`), because
tiny.css is greys and two blues and has no yellow to borrow.

## HiDPI

The rule is that **everything above the renderer is in logical pixels**. Layout
constants, CSS lengths, hit tests and mouse coordinates are all one unit;
`SDL_SetRenderScale` turns them into device pixels on the way out. Nothing in
`src/` multiplies a constant by the scale, and nothing should start.

Three pieces make that hold:

- `curie_scale()` (`src/metrics.c`) is the one place the scale is known. It
  reads `SDL_GetWindowDisplayScale`, or `CURIE_SCALE` when that is set.
- `apply_render_scale()` pushes it into the renderer once per scale change.
  Everything drawn afterwards is in logical units.
- Fonts are the exception that proves it. They are **baked** at the device size
  so glyphs are sharp, then `handle.height` is set back to the logical size so
  layout never sees the difference. `nk_font_text_width` and the glyph quads
  both take their scale from the height handed to them rather than from
  `font->scale`, so that one field is the whole of it.

The window is created at `WINDOW_WIDTH × scale` rather than at the logical
size, because `SDL_WINDOW_HIGH_PIXEL_DENSITY` gives a backbuffer in device
pixels and a forced `CURIE_SCALE` would otherwise get a window too small for
its own contents.

Verified at `CURIE_SCALE=1`, `1.5` and `2`. What has **not** been checked is a
window dragged between monitors of different scale mid-run: the event is
handled and the font rebakes, but nobody has watched it happen — the reference
machine is a VM with one display.

## Talking to the desktop

Three places where an immediate-mode app has to reach outside its own window.
Each is done as far as it can be, and the part that is not is named.

**File dialogs — done.** File > Open on the Popups page calls
`SDL_ShowOpenFileDialog`, so it is the platform's own picker, not a drawn
imitation. SDL runs it on its own thread and calls back from there, which is
the whole shape of `curie_file_open` / `curie_file_taken`: the callback fills
in a buffer, publishes it with an atomic store and pushes a registered event to
wake a loop that may be parked in `SDL_WaitEvent`; the main thread collects it
during the next frame. Nothing else in the app is touched from that thread.
Cancel and “no picker on this platform” both come back as text, because the page
that shows the answer should show those too — the web build has no dialog
backend and reports so.

**IME — positioned, not composed.** `SDL_SetTextInputArea` now gets the focused
field's rect and the caret's pixel offset within it, so a candidate window opens
beside what is being typed rather than at the window's origin. The SDL3 backend
cannot do this itself — its own FIXME says so — because Nuklear exposes neither
which edit widget is active nor where it is; the app knows both, since it laid
the widget out, so `note_ime_caret` records them at the call site and the shell
hands them over after `nk_sdl_update_TextInput`.

What is still missing is preedit: `SDL_EVENT_TEXT_EDITING` marks the frame dirty
but the composing text is not drawn, because `nk_text_edit` has no notion of an
uncommitted run. Typing in a CJK IME therefore shows nothing until the
composition commits. Fixing it properly means teaching Nuklear's editor about a
preedit span — upstream, not here.

**Accessibility — keyboard, and a tree with nothing reading it yet.** Tab and
Shift-Tab walk every focusable widget in reading order, the arrows move among
siblings, Home and End jump, Enter and Space press, and a 2px ring in `--focus`
shows where focus is — shown by a key, hidden by a click. Ctrl-Tab and
Ctrl-Shift-Tab still walk the tab strip, Ctrl-1..7 jump straight to a page, and
F1 opens Diagnostics. Tab leaves a text field rather than typing into it, which
is what every other toolkit does. Phase 3 of [ACCESSIBILITY.md](ACCESSIBILITY.md)
says how: focus is a state in the shadow tree, and Enter is a click Nuklear
receives at the focused node's centre. In the browser the tree is also served:
`src/a11y_web.c` mirrors it into hidden DOM beside the canvas, so a screen
reader on the web build gets every widget, its state and its position.

Underneath, every widget now reports itself as it is drawn, into the shadow tree
in `src/a11y.c` — role, name, value, state, bounds — which is diffed against the
previous frame. `CURIE_A11Y_DUMP=<path>` writes it out, and that is how the
instrumentation is checked, because none of it shows on screen. The seam is
`hot()` in `showcase.c` and the `curie_*` helpers in the shell: a widget that
takes a cursor reports a name in the same call, which is what stops the tree
falling behind the screen.

It runs unconditionally, because it is cheap enough that making it optional
would be the more expensive decision. `build/a11ytest --bench` puts describing
the busiest page — 82 nodes — at about 4 µs. Against the app's own frame build
the difference does not survive the noise: 30 samples with it on and 34 with it
off gave medians of 1.39 ms and 1.28 ms with standard deviations of 0.095 and
0.043 ms, twelve to twenty-seven times the effect being looked for. The isolated
benchmark is the number to trust; the A/B only shows that the app cannot feel
it.

Nothing reads that tree yet. Serving it to UI Automation, AT-SPI, NSAccessibility
or the DOM is phase 4 of [ACCESSIBILITY.md](ACCESSIBILITY.md), and until then a
screen reader still finds an empty window. It is worth being plain about why
that last step is the hard one. A screen reader reads
an accessibility tree — UI Automation on Windows, AT-SPI on Linux, NSAccessibility
on macOS — and an immediate-mode UI has no tree to expose: there are no widget
objects, only draw calls, and the button that existed last frame has no identity
this frame. Nuklear has no focus traversal to hook into either, so there is no
existing notion of “the focused widget” to move around.

Getting there would mean the app keeping a parallel model of what it drew — a
list of roles, labels, bounds and states, rebuilt each frame and served to the
platform's accessibility API. That is a real design, not a patch, and it is
written up as one in [ACCESSIBILITY.md](ACCESSIBILITY.md).

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
| `CURIE_VSYNC` | `0` turns vsync off |
| `CURIE_AA` | `0` turns Nuklear's anti-aliasing off |
| `CURIE_SW_NOAA` | `1` turns stroke feathering off too on the software renderer, which feathers no fills — see the renderer section |
| `CURIE_FRAME_RATE` | A frame cap, or `0` for uncapped, instead of `waitevent` |
| `CURIE_REDRAW` | `always` draws every callback — for profiling, not for use |
| `CURIE_A11Y_DUMP` | A path to write the accessibility tree to, once, after the first frame |
| `CURIE_HOVER_GAP_MS` | Pins the pointer-redraw gap and skips its calibration — for measuring, not for use |
| `CURIE_RENDERER` | `auto`, `cpu`, `gpu`, `list`, or a driver name — see the renderer section |
| `CURIE_STATS` | Logs frame timings once a second |

The Diagnostics tab reports all of it, plus where each frame's milliseconds
went and where the memory went. Prefer it to a guess: several plausible
optimisations here turned out to be measurably worse, and one 4x CPU
regression was invisible in the build, render and present timings because the
cost was inside the renderer.

For memory specifically, know what the numbers are before steering by them.
The tab reports **private bytes** (commit — the process's own) and **working
set** (residency, shared driver pages included), and the startup breakdown
gives both per step. Runs of the same binary vary by about 2 MB, so a single
reading proves nothing — see [PERFORMANCE.md](PERFORMANCE.md#memory) for what
that costs in sample size.

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
- No `.ico` is linked; there is no window to give an icon to.

## What a frame costs, and why pointer frames are coalesced

The reference machine has no GPU. Left to Direct3D, every presented frame is
rasterised in software by the display driver's user-mode part, which runs
*inside this process* on Windows thread-pool threads — measured at **about 78 ms
of CPU per frame**, of which Curie, Nuklear and SDL together are 4–5 ms on the
main thread. `auto` now sidesteps that by taking SDL's own software renderer
when the adapter is WARP (renderer section above), which draws the same frame
for **7–12 ms**. That is still a frame every time the pointer crosses something,
so the coalescing below still earns its keep — it just has less to hide. The numbers are in [PERFORMANCE.md](PERFORMANCE.md).

So on this machine CPU is a count of frames, and the frames that add up are
the ones the pointer asks for: a hover crossing is a frame, a tooltip that
follows the pointer is a frame per motion event. A pointer swept along the
Buttons page's 21 symbol buttons drew 41 frames in two seconds and showed as
15–22% of four cores in Task Manager for as long as it lasted.

`hover_redraw` in `src/main.c` coalesces pointer-driven frames to at most one
every *gap*, and the gap is not a constant: the app measures what a frame
costs it and chooses. Sustained random waving over the Buttons page, four
cores, this machine:

| gap | CPU |
| --- | --- |
| none | 20.6% |
| 50 ms | 19.6% — barely helps: crossings arrive at ~11/s anyway |
| 100 ms | 12.0% |
| **150 ms** | **9.1%** |
| 200 ms | 9.3% |
| 300 ms | 7.1% |

`calibrate_hover_gap` skips the first three drawn frames after startup (font
bake, stylesheet parse), then averages the process's CPU time — every thread,
via `curie_process_cpu_ms` — over the next eight, and sets the gap from the
result: 150 ms above 40 ms a frame, 100 ms above 15, 50 ms otherwise. Here it
measures ~62–78 ms and picks 150; on a GPU it will measure well under a
millisecond and keep 50, which is there only to bound a hover storm. The
Diagnostics page shows both numbers. `CURIE_HOVER_GAP_MS` pins the gap and
skips the calibration, which is how the table above was taken.

Why measure rather than detect the adapter: the same binary has to do the
right thing on a laptop and in this VM, and “is there a GPU” is a question
with platform-specific answers, while “what did the last eight frames cost
this process” is one number that is true everywhere.

The one subtlety is that a deferred frame cannot simply be dropped. At rest the
callback rate is `waitevent`, so the event that would have drawn the final
hover state may never come, and the highlight would be left one step behind
the pointer. A deferred frame therefore pins the callback rate to 20 Hz for
one tick, draws, and hands the rate back through `restore_rate` — the path a
drag already uses. Drags themselves are never coalesced: they own the rate and
draw every tick.

On a GPU none of this matters, and all of it still holds.

## A drag that never ends

While a mouse button is held the app pins `SDL_HINT_MAIN_CALLBACK_RATE` to the
display's refresh and draws every scheduled frame whether or not an event
arrived — that is what makes a text selection track the pointer smoothly. The
state is entered on `SDL_EVENT_MOUSE_BUTTON_DOWN` and left on `..._UP`.

**If the release never arrives, it is never left.** The app then redraws at the
display's refresh forever: a stuck 30–170% of a core that looks exactly like a
runaway loop, because it is one. It is reproducible in one gesture — press
inside the window, take the focus away while the button is still down, release
somewhere else — and it measured 171% of a core here. It was in the app from
the moment the drag rate was added, and `File > Open` made it easier to hit,
because a modal system dialog opening under the pointer is exactly this.

Two guards, because one is not enough:

- `SDL_EVENT_WINDOW_FOCUS_LOST` ends the drag. Whatever happens to the button
  after that happens to another window.
- Every frame, if the app thinks a drag is running, it asks whether a button is
  actually held. This must be **`SDL_GetGlobalMouseState` and not
  `SDL_GetMouseState`** — the latter answers from SDL's own cache, which still
  says the button is down for exactly the reason the app is stuck: it never saw
  the release. Written with the cached call first, the fix measured 163% and
  changed nothing.

The cost is one OS query per drawn frame, and only while dragging.

The second guard had a hole of its own, found by review after the measurement
said it worked: it cleared the drag and set `restore_rate`, but the countdown
that hands the callback rate back only runs after a *drawn* frame, and nothing
had asked for one. So the rate stayed pinned at the display's refresh and SDL
kept calling an iterate that drew nothing — about 1.5% of a core, forever,
which is cheap enough to hide from a measurement and wrong all the same. The
guard now marks the frame dirty, which is what the button-up path had been
getting for free from the event whitelist.

## The renderer, and the one that was six times cheaper

`CURIE_RENDERER` takes `auto` (the default), `cpu`, `gpu`, `list`, or the name
of any driver this SDL build has — on Windows `direct3d11`, `opengl`,
`opengles2`, `software`. A name that is not in the list, or one that is listed
but will not create (both GL drivers fail on a VM with no 3D), logs a line and
falls back rather than refusing to start.

**The reference machine has no GPU, and Direct3D still succeeds** — SDL's
`direct3d11` backend falls back to WARP, Microsoft's software implementation,
so the app pays for a full D3D11 pipeline and a WDDM swapchain present to draw
a few hundred flat triangles. The adapter says so plainly:
`Microsoft Basic Render Driver`, vendor `0x1414`, 0 MB dedicated. Curie asks
the device SDL gave it and reports it — Diagnostics shows `auto (no GPU)` —
because it is most of the explanation for every CPU figure in
[PERFORMANCE.md](PERFORMANCE.md).

SDL's own software renderer costs **13.1 ms of CPU per frame against WARP's
78.7**, medians of four interleaved runs. Six times. It does not draw the same
picture, and the reason is one line of SDL: `SDL_render_sw.c` turns each vertex
into a whole pixel with `(int)(x * scale)` — truncation — and the rasteriser
under it works on integer `SDL_Point`s. **There is no partial coverage anywhere
in that path.** Two consequences follow, and they need different answers.

**Alignment.** A rect whose edge lands on .5 covers one row more or less than a
GPU would: the tab underline arrived as two lines with a gap, accent at y=67 and
y=69 where hardware fills 68–69 solid, and the top row of the window came out
`(44,44,44)` instead of `(53,53,53)`. Fixed by storing `round(x) + 0.5` for
every vertex on the software path, so SDL's truncation lands on `round(x)`. The
worst pixel difference across the window fell from 201/255 to 88. One pass over
a two-kilobyte vertex buffer, and only when the software renderer is in use.

**Antialiasing.** Nuklear's is not edge coverage — it emits geometry a fraction
of a pixel wide whose alpha fades to zero and lets the rasteriser blend it. With
nothing to blend it, that geometry lands whole, at whatever alpha it
interpolated to. Snapping cannot help; the feather is a fixed pixel wide, so
rounding it aligns the band rather than removing it.

What took three wrong turns to see is that Nuklear emits two kinds of feather
and they fail differently. A **fill's** feather is a ring half a pixel outside a
fill shrunk by half a pixel; landed whole it is a half-tone column between a
panel's border and its fill, and a rule between menu items. A **stroke's**
feather is what grades a border's curve — and it also covers the fill's
staircase underneath, which is why a button corner stays graded with fill
feathering off. One switch cannot say that, so the backend takes the two
separately (`nk_sdl_render_ex`) and the software renderer runs **strokes
feathered, fills not.** Measured across a popup's edge it then matches the
hardware profile pixel for pixel — `53 53 53 53…` on both — and the profile
between menu items is flat. Both on gives the column and the rule; both off
gives corners that step in twos. `CURIE_SW_NOAA=1` is both off, and 6% cheaper.

One consequence to know about when adding a widget: Nuklear draws most borders
as two *fills* — the border colour, then the background shrunk by the width —
and a fill ring at a 6px radius with no feather reads as a square corner. The
shell's buttons stroke their border, which is why they kept their corners; the
text fields did not, and lost theirs until `stroke_edit_edge` drew the ring as
a stroke on the same footprint. A widget whose corners go square on the
software path and nowhere else is almost certainly a fill ring. A context menu
asks Nuklear for its border (`NK_WINDOW_BORDER`) at 2px for the same reason: a
contextual popup is dynamic, fills its body at `nk_panel_end` and strokes the
rim there at the final height — anything drawn inside it earlier is painted
over — and a 1px rim reaches only half a pixel either side of the arc, which is
exactly where the unfeathered fill's corner steps are.

A colour swatch (`curie_button_color`) is the button rule with the fill
replaced and the border tinted to match. The border is a *fill band* here, so
the swatch's own arc would otherwise sit at `rounding - border` — measurably
identical to its neighbour on the outside, and visibly tighter, because a grey
rim two steps off the page is not what the eye reads as the edge. Colouring the
band puts the visible edge back on the button's outer arc.

**A rounded rect gets its corners from a mask.** A stroke on the fill's own
footprint is the older remedy and still what the text fields use, but it only
grades the step by a fixed half: the snapping pass quantises the stroke's
feather onto the same staircase the fill made, so a large radius reads as
stairs with a halo rather than a curve. `curie_fill_round` computes the
coverage instead — one white disc of 2r whose alpha is the circle sampled four
by four, each quadrant drawn into a corner as a sub-image, three plain rects
for the rest — so the corner is exact at any radius and identical on both
backends, at one texture per radius for the session. The progress bar's track
and fill and the slider's bar and fill are drawn with it; the widgets' own
style items go transparent for the call, which keeps their geometry, their
drag and their value.

**Circles are the one shape this rasteriser cannot draw at all.** Nuklear fills
a circle as a polygon and grades its rim with the same fractional geometry, so
with fill feathering off there is nothing to grade it: a radio came out
nineteen pixels across and seventeen high with a rim that jumped between five
tones, and raising `circle_segment_count` to 48 bought nothing, because the
snapping pass quantises the arc as well. A texture's alpha *is* blended per
texel on both backends, so every circle on a page — a radio's rim and dot, a
combo's leading symbol, a selectable's — is an Ionicon drawn into the slot
Nuklear sized, with `NK_SYMBOL_NONE` handed to the widget. Two details make or
break it. The icon is rasterised **1:1** with the rect it is drawn in
(`curie_ionicon_exact`, against `icon`'s usual 2×): a downscale re-samples
plutovg's antialiasing, and on a rim one pixel wide that reads as a smear three
pixels across. And `nk_draw_option` fills the whole selector with
`border_color` before the background, without asking whether there is a border,
so that colour has to be pushed transparent along with the fills or Nuklear's
disc stands under the icon as a second rim a pixel outside the real one.

**Glyphs are snapped on hardware**, separately from either of those. Nuklear
advances the pen by fractional widths, so a glyph quad can begin mid-pixel;
hardware then samples the atlas between texels and softens the glyph, where the
software path is already on the grid. Rounding the quad on hardware puts both on
the same texel grid — which is what type rendering normally does anyway: a
bitmap baked for this size, drawn 1:1.

That snap has to reach glyphs and nothing else, and by default it cannot tell
them apart: `nk_font_atlas_end` sets `tex_null->texture` to the atlas handle, so
shape geometry and text arrive carrying the same `cmd->texture.ptr`, and a test
on it matches every draw command. `white_tex` — a 1x1 texture of its own for the
white texel Nuklear multiplies untextured geometry by — makes the handle mean
what it says. Without it the snap is a global one, and rounding the feather flat
is exactly the artefact described above, on hardware this time.

The transient panels keep Nuklear's 1px border. Zeroing it was tried twice —
once on the theory that the popup outline was geometry the GPU blended away (it
was not: removing it changed the *hardware* render by 3160 pixels), and once as
a design change. Both were reverted. The outline is what makes a menu read as a
surface above the page rather than a hole in it.

### What parity is available

Pointer parked, `direct3d11` against `software`, whole window, counting pixels
that differ by more than 8/255 out of 652800:

| Software configuration | Login | Buttons | Layout | |
| --- | --- | --- | --- | --- |
| Strokes feathered, fills not (the default) | 2938 | 11316 | 12350 | graded curves, no column, no rule |
| Both feathered | 4157 | 13620 | 14128 | half-tone column inside panels, rule between items |
| Neither | 2728 | 6703 | 7285 | clean edges, corners step in twos |
| Every vertex snapped on **both** renderers | 2 | 0 | 0 | hardware loses its feather |

The third row is worth stating plainly, because it was briefly taken for a
result. Snapping every vertex on both paths does give near-exact parity, and it
gets there by making hardware draw what software draws — the same hairline
stripes, 0 pixels apart across the menu region. Parity bought by removing
antialiasing from the renderer that has it is not parity worth having.
**Exact agreement is not available while one rasteriser has partial coverage and
the other has none.** Nor is the pixel count the thing to read: the "neither"
row scores best and looks worst. What the default row leaves is a stroke's
feather landing whole — a 1px darker line along a button's flat top where
hardware blends it away — which is what the two rasterisers actually are.
**That is the accepted compromise.** Looked at side by side at 1:1 it is not
noticeable, and it is not removable without partial coverage, so it is where
the software renderer is left; the columns, rules, doubled underlines and
stepped corners that preceded it were each real, and each is gone.

**`auto` now picks SDL's software renderer when there is no GPU** — that is,
when the Direct3D device turns out to be WARP — and reports itself as
`auto: software (no GPU)`. With the alignment fixed, glyphs on the grid and
fills unfeathered, what separates it from hardware is the accepted stroke
feather above, and six times the CPU is not worth that. A machine with a GPU
keeps it: there the hardware path is both cheaper and the better picture. A
driver named in `CURIE_RENDERER` is never swapped.

## A held button that changes nothing

`dragging` pins the callback rate to the display's refresh and draws every
tick. That is right while the pointer moves — it is what makes a text
selection track smoothly, and the comment above records that relying on motion
delivery alone measured 33–45 fps with the selection advancing unevenly. It was
also happening with the pointer **still**: a button held on blank page area,
nothing moving, nothing changing, drew sixty identical frames a second. 167% of
a core; 42% of this machine.

A hold now falls back to the pointer-redraw gap, but only when the pointer is
over something that cannot change under it — keyed on `hot_last_repaint`, the
same flag the hover logic uses to decide whether a crossing is worth a frame.
Blank page, a label, a heading: coalesce. A slider, a scrollbar arrow, a
repeater: full rate.

Coalescing *every* stationary hold was the first attempt and it was wrong.
It took the `NK_BUTTON_REPEATER` demo on the Buttons page from ~26 repeat ticks
a second to 8 — measured by holding the button and reading its own counter,
which is the only reason it was caught. Trading a visible change in how the app
behaves for CPU is not a trade worth making silently.

## Hot regions are clipped

`nk_widget_bounds` answers where a widget *would* go and applies no clip test,
so a page scrolled past the top used to register hover rects for widgets above
the body — over the tab strip, where the last-match rule let them win: wrong
cursor over the tabs, frames drawn for controls nobody could see, and on the
Popups page the tooltip row landing under the strip and turning it into a
pointer-following region. `hot_push_ex` now intersects every rect with the
panel's clip and drops what is left off screen. Also found by the audit.

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
