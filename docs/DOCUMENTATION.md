# Reaktor

Using it, and working on it.

**Using it** — [An application](#an-application) ·
[Declaring widgets](#declaring-widgets) · [Layout](#layout) ·
[Styling](#styling) · [Accessibility](#accessibility) ·
[Animation](#animation) · [Reaching past the library](#reaching-past-the-library)

**Inside** — [The architecture](#the-architecture) · [The tree](#the-tree) ·
[The style pipeline](#the-style-pipeline) ·
[The frame loop](#the-frame-loop) · [Tests](#tests) ·
[Build options](#build-options) ·
[Command-line flags](#command-line-flags) · [The renderer](#the-renderer) ·
[The web build](#the-web-build) · [Localization](#localization) ·
[Text](#text) ·
[Nuklear behaviors](#nuklear-behaviors-that-have-already-cost-an-afternoon)

---

## An application

Six functions, in [`samples/sample.h`](../samples/sample.h). The runtime owns
the window, the event loop, the stylesheets, the font atlas and the
accessibility tree; you own what is drawn and what the keys mean.

```c
void page_shell(App *app, struct nk_context *ctx, int win_w, int win_h);
int  sample_key(App *app, const SDL_Event *e);
void sample_window(reaktor_window_spec *out);
void sample_file_taken(App *app);
void sample_args(App *app, int argc, char **argv);
SDL_HitTestResult SDLCALL window_hit_test(SDL_Window *, const SDL_Point *, void *);
```

Only `page_shell` usually has anything in it. `samples/simple/simple.c` is a
working application in sixty lines and returns a constant from the other five.

`page_shell` is called once a frame inside a panel that already covers the
window, so you start by laying out rather than by opening anything.

## Declaring widgets

A widget is a designated-initialiser struct. Every field is optional.

```c
if (reaktor_button(&(reaktor_button_spec){
        .label = "Save",
        .keys  = "Control+S",           /* announced, not bound */
        .box   = { .w = 120.0f } }))
    save();
```

Answers non-zero on the frame it was pressed. There is no widget object, no
handle and no id to keep: the widget *is* the code that made it, reading your
variable and answering in your scope.

The fifteen specs are in [`core/ui/declare.h`](../core/ui/declare.h): button,
label, icon, field, link, swatch, check, radio, select, slider, progress,
knob, property, combo, color.

Two open a scope rather than answering:

```c
REAKTOR_COMBO(.label = sizes[pick], .name = "Size", .body_h = 130) {
    for (i = 0; i < 3; i++)
        if (reaktor_combo_item(sizes[i], i == pick)) pick = i;
}
```

A combo's body is Nuklear's own panel, so lay it out with
`nk_layout_row_dynamic` — a declared tree belongs to one panel and the popup
is another. Do not `return` or `break` out of a scope.

## Layout

Containers are scopes; children are declared inside them.

```c
REAKTOR_COLUMN(.gap = 10.0f) {
    REAKTOR_ROW(.h = 30.0f, .gap = 6.0f, .flags = REAKTOR_LAY_FILL_X) {
        reaktor_button(&(reaktor_button_spec){
            .label = "Left",  .box = { .w = 90.0f } });
        reaktor_button(&(reaktor_button_spec){
            .label = "Rest",  .box = { .flags = REAKTOR_LAY_FILL_X } });
    }
}
```

`REAKTOR_ROW`, `REAKTOR_COLUMN` and `REAKTOR_FREE` — the last places children
by their own margins and lets them overlap.

On a box:

| | |
| --- | --- |
| `.w`, `.h` | its size — **a floor** when the box also fills that axis |
| `.weight` | its share of what is left over, relative to its siblings; 0 reads as 1 |
| `.gap` | between *its children*, not outside them |
| `.ml .mt .mr .mb` | margins: space outside it |
| `.flags` | `FILL_X` `FILL_Y` `WRAP` `CENTER_X` `CENTER_Y` `PACK_CENTER` `PACK_END` `PACK_SPREAD` |

A width that is also a floor is one rule covering what CSS spells as three: a
fixed track is `.w` alone, a minimum that grows is `.w` with `FILL_X`, and a
fraction is `FILL_X` with `.weight`.

**A box is placed one frame late.** A frame declares its boxes as it draws
them, which is too early to know where any of them go — the last sibling is
declared long after the first is drawn. So a frame draws into the rectangles
computed at the end of the previous one, found by the accessibility id. A box
seen for the first time is skipped for one frame rather than guessed at.

Two things follow. Content-dependent sizes (a wrapped paragraph) need a
further frame to settle. And **a widget is found again by its name**, so one
whose text changes every frame needs a stable `.name` or it is a new box every
frame and never has a rectangle at all. The runtime notices: after sixteen
unsettled frames it stops asking for redraws and prints which box is missing.

## Styling

Sheets are read in order and the last one wins, because that is what CSS
already does with a tie. Nothing merges them.

```mermaid
flowchart LR
    pal["<b>the palette</b><br/>variables-light.css<br/>variables-dark.css"]
    tiny["<b>tiny.css</b><br/>what a document<br/>already looks like"]
    app["<b>the application's own</b><br/>whatever it ships"]
    over["<b>the override</b><br/>only while the<br/>application asks"]

    pal --> tiny --> app -- "beats everything left of it" --> over
```

A widget asks for a selector and gets computed values, or `matched` clear and
Nuklear's default:

```c
reaktor_style s;
reaktor_style_get("button", &s);
if (s.matched) { /* s.bg, s.fg, s.border, s.rounding, s.pad_x, s.font_px */ }
```

The override slot is read only while the application asks for it. The
showcase's **Styling** page reads simple.css over the top of tiny.css at the
flick of a checkbox, which is the only honest test of the claim that the look
is not in the code.

Two limits worth knowing:

- **Attribute selectors are rewritten, not matched.** LCUI implements type,
  class, id and descendant selectors, so `cssflat` turns `input[type=range]`
  into `input.type-range` and the widget asks for that spelling. An operator
  form — `[href^="http"]` — still cannot be expressed and is dropped.
- **Controls a document does not have** — a slider, a progress bar, a knob —
  have no rule to inherit, so they borrow the rule of what they resemble: the
  rail from `input`, the accent from `a`, a knob's body from `button`. Reading
  the accent off the `a` rule rather than a `--links` token is what makes that
  work with a sheet that never declares one.

`prefers-color-scheme` is honored: the branch matching the active scheme is
unwrapped before parsing. Width and print queries are dropped — a viewport
query means nothing to a window that is not a document.

## Accessibility

There is nothing to do. A declared widget reports its own role, name, value,
state and bounds, and the tree is diffed each frame and served to the
platform — UI Automation on Windows, NSAccessibility on macOS, AT-SPI on the
free desktops, a DOM subtree beside the canvas in a browser.

For the widgets the library has no spec for, report them yourself:

```c
reaktor_note(app, REAKTOR_A11Y_TREEITEM, label, NULL, state, bounds);
```

`-DREAKTOR_A11Y=OFF` builds without the platform bridges, and so does an
application that links `reaktor_a11y_none` — see
[Build options](#build-options). The tree itself is still built, because layout
finds a box again by its accessibility id; only serving it is dropped.

## Animation

A number that takes time to change, keyed by the same accessibility id.

```c
REAKTOR_COLUMN(.name = "Track", .h = 56.0f) {
    unsigned id = reaktor_box_id();
    float    x  = reaktor_animate(id, 0, target, 320.0f,
                                  REAKTOR_EASE_CUBIC_OUT);
    /* draw at x */
}
```

The first call starts at its target and stays there — an animation is a
*change*, and nothing has changed yet. Afterward a new target starts a run
from wherever the value is, so a target that changes mid-flight redirects
rather than restarts. `channel` distinguishes several numbers on one widget.

31 curves; `reaktor_ease_at(curve, t)` is public so a page can draw them.
Entries die when the accessibility diff says the node is gone. A page with
nothing moving asks for no frames at all.

## Reaching past the library

Nuklear is still there. Groups, trees, list views, charts, menus and popup
bodies have no spec, and the samples use `nk_*` directly for them. A declared
tree and `nk_layout_row_*` cannot share a panel, so convert a whole container
at a time — but a popup or a group is its own panel and may be laid out
imperatively inside a declared page.

---

## The architecture

```mermaid
flowchart TB
    app["<b>the application</b><br/>samples/ — showcase, notepad, simple, bench"]
    rt["<b>runtime/</b><br/>the window, the event loop, a frame per change"]
    ui["<b>core/ui</b><br/>declare, widgets, layout, keys, focus"]
    ren["<b>core/render</b><br/>Nuklear, then the SDL3 software rasterizer"]
    sdl["<b>SDL3</b><br/>window, input, surface, file dialogs"]

    css["<b>core/css</b><br/>cssflat, libcss, style_map"]
    tree["<b>core/a11y + core/anim</b><br/>the shadow tree, eased values"]
    plat["<b>platform/</b><br/>UIA, NSAccessibility, AT-SPI, DOM"]

    app --> rt --> ui --> ren --> sdl
    css -- "every color, size and radius" --> ui
    ui -- "one node per widget" --> tree
    tree --> plat
```

Only the top box is yours. A spec goes into `core/ui`, which asks `core/css`
what it should look like and `core/ui/layout` where it goes, files a node in
the shadow tree, and hands Nuklear a draw call — so the right column happens
whether the application asks for it or not.

## The tree

```
core/base      paths, metrics, the shared header
core/css       cssflat pre-processor, libcss front end
core/render    Nuklear impl, the SDL3 backend, drawing, SVG icons
core/ui        the declarative API, layout, widgets, style map, keys, focus
core/a11y      the shadow tree and its diff
core/anim      eased values keyed by a11y id
core/locale    the Locale module: catalogs, lookup, plurals, formatting
core/text      the Text module: direction, shaping, fallback glyphs, line breaking
platform/      system theme, and one accessibility bridge per platform
assets/locale  the showcase's catalogs, one file per language
runtime/       app.c: the window, the event loop, the frame
samples/       showcase, notepad, simple, bench
tools/         the tests, the icon compiler, vmwalk
benchmarks/    bench's workload in four other frameworks, and one sampler
external/      ten submodules, read as they ship
```

`runtime/` and `core/` build into `reaktor_runtime`, a static library the four
executables link — all but `core/locale`, `core/text` and the bridges, which
are the modules, compiled into the programs that take them. Nothing in `core/` knows
which application it is in.

## The style pipeline

```mermaid
flowchart LR
    sheets["<b>the stylesheets</b><br/>external/tinycss/src/*.css<br/>+ the application sheet, + the override"]
    flat["<b>cssflat.c</b><br/>resolve var()<br/>unwrap @media<br/>[attr=x] becomes .attr-x"]
    css["<b>style.c</b><br/>libcss:<br/>parse, match, compute"]
    map["<b>style_map.c</b><br/>the one writer<br/>of nk_style"]
    nk(["Nuklear draws it"])

    sheets --> flat --> css --> map --> nk
```

**`cssflat` exists because libcss is not a browser.** It has no custom
properties, so every `var()` is resolved before the engine sees the text; no
`@media`, so the branch matching the active scheme is unwrapped and the rest
dropped; and no attribute or pseudo-element selectors, so rules carrying them
are removed rather than left to fail silently.

**`style_map.c` is the only writer of `nk_style`.** Setting a widget style
anywhere else works right up until a theme change reloads the sheets.

**The rule store is rebuilt on every load.** libcss adds rules to a global
store and never takes a sheet's rules back out, so `reaktor_style_init()`
destroys and re-creates it — without that, each scheme switch stacked another
copy of tiny.css.

## The frame loop

There is no loop. SDL is told `SDL_HINT_MAIN_CALLBACK_RATE = "waitevent"`, so
`SDL_AppIterate` runs when an event arrives and returns immediately unless
`app->dirty` is set. What sets it: a widget being clicked, typed into or
dragged; the pointer crossing a **hot rectangle** registered last frame (that
is how hover repaints without polling); the window moving, resizing or changing
scheme; an animation still running.

While a mouse button is held the rate switches to the display's refresh, with a
two-frame settle after release, so the frame that completes a click is not also
the frame that stops scheduling frames.

The app therefore measures 0% of a core at rest, and **adding a timer or an
unconditional repaint to make something update is the wrong fix** — mark the
thing that changed instead. [PERFORMANCE.md](PERFORMANCE.md) is what that costs
on four platforms.

## Tests

Seven, built by default, run from `build/`:

| | |
| --- | --- |
| `laytest` | the layout engine's own placement |
| `onlaytest` | Onlay upstream's suite, unmodified |
| `a11ytest` | node identity, the diff, the pool |
| `animtest` | curves, retarget, eviction |
| `keytest` | chord parsing and formatting |
| `localetest` | catalog parsing, lookup, plural expressions, formatting |
| `texttest` | line breaks, paragraph direction, measuring, and the order glyphs reach the vertex buffer in — against the fonts in `assets/fonts`, with no window; built only with the Text module |

Beyond that, verification is the **accessibility dump**: run with
`--a11y-dump <path>` and every node's role, name, value, state and
rectangle is written out once the frame settles. Diff two of those and a change
that moved something by a pixel says so. It has caught more regressions here
than looking has.

And when a fix is visual, **look at the framebuffer**. `--shot <path.bmp>`
writes the window once it settles, then quits. Reasoning from a library's source
about where it puts a glyph produces confident, wrong answers.

## Build options

Passed to CMake. Both scripts forward them:

```bash
./build.sh -- -DREAKTOR_A11Y=OFF        # macOS, Linux, the BSDs
```

```powershell
.\build.ps1 -DREAKTOR_A11Y=OFF          # Windows
.\build-wasm.ps1 -DREAKTOR_A11Y=OFF     # the web build; build-wasm.sh takes -- as build.sh does
```

| Option | Default | Effect |
| --- | --- | --- |
| `REAKTOR_A11Y` | `ON` | Serve the accessibility tree to screen readers: UI Automation, AT-SPI, NSAccessibility, the DOM |
| `REAKTOR_LOCALE` | `ON` | Translations, from the catalogs in `assets/locale` — see [Localization](#localization) |
| `REAKTOR_LOCALE_EMBED` | `OFF` | Compile the catalogs into the executable rather than read them at startup |
| `REAKTOR_TEXT` | `ON` | Direction, shaping and fallback glyphs for the text the font atlas cannot draw, and line breaks by Unicode's rules — see [Text](#text) |
| `REAKTOR_SDL_GL` | `OFF` | Build SDL's OpenGL and GLES drivers on Linux and the BSDs |

**CMake remembers an option.** A later build without the flag keeps whatever
was set last, so turn one back on by passing `=ON`, not by leaving it out.

**A module is two libraries, and the application links one of them.**
`reaktor_a11y` or `reaktor_a11y_none`, `reaktor_locale` or
`reaktor_locale_none`, `reaktor_text` or `reaktor_text_none`: both halves
define the same functions, and each is an interface library, so its sources are
compiled into the program that links it and nowhere else. The showcase takes
all three modules; `simple`, `notepad` and `bench` take the `_none` halves, and
show what the runtime costs with nothing added. An option switched off turns a
module into its `_none` half for every program that links it.

```cmake
target_link_libraries(myapp PRIVATE
    reaktor_runtime reaktor_a11y reaktor_locale reaktor_text_none)
```

**`REAKTOR_A11Y=OFF` removes the bridges and nothing else.** The UI Automation,
AT-SPI and NSAccessibility bridges and the web DOM subtree are not compiled, and
the libraries only they need are not linked: `uiautomationcore`, `ole32` and
`oleaut32` on Windows, `dbus-1` on Linux and the BSDs. (Cocoa stays on macOS;
SDL links it for itself.) The accessibility tree is still built, because layout
finds boxes by its ids and animations are keyed to them, so `--a11y-dump` keeps
working. A switched-off build draws the same pixels and writes the same dump as
a default one; a screen reader just finds nothing in it.

It trims little, because the bridges are small — see
[PERFORMANCE.md](PERFORMANCE.md#modules). The reason to turn it off is the
libraries, and on Linux, not needing `dbus-1` at all.

**`REAKTOR_LOCALE=OFF` leaves every string as its key.** `reaktor_tr` answers
with the key it was given, the atlas bakes Latin-1 alone, and the web build
stops packing the catalogs and the Japanese font — most of the showcase's
`.data`.

**`REAKTOR_LOCALE_EMBED=ON` writes the catalogs into the executable.** CMake
reads every `assets/locale/*.txt` at configure time into a generated C file,
and a catalog added or edited later reconfigures on the next build. The program
then needs no `assets/locale` beside it; the Japanese font is still read from
`assets/fonts`.

**`REAKTOR_TEXT=OFF` leaves mojibake and kb_text_shape out of the build.**
Nothing is reordered or shaped, no glyph comes from another font, and a line
breaks after a space. A catalog Aileron can draw — Polish — is untouched,
because what Aileron has is Locale's to bake; the Japanese one comes out as
boxes.

## Command-line flags

**Reaktor reads no environment variables.** Every default is compiled in, and
the only things worth overriding are the ones that make a run reproducible —
so that two screenshots can be compared, or two accessibility dumps diffed.

The runtime takes these and removes them from `argv` before the application
sees it, so an application's own arguments are unaffected:

| Flag | Effect |
| --- | --- |
| `--shot <path.bmp>` | Write the window once it settles, then quit |
| `--a11y-dump <path>` | Write the accessibility tree once it settles |
| `--theme <system\|light\|dark>` | Which color scheme to open in |
| `--lang <code>` | Which catalog to open in, by its file name: `en`, `pl`, `ja` |
| `--font-fallback <path>` | A font for the Text module to take glyphs from when the UI font has none, tried before any the application adds; give it again for more |
| `--renderer <name>` | `software` (the default), `auto` to let SDL pick, or a driver name |

The showcase adds two of its own, through `sample_args`:

| Flag | Effect |
| --- | --- |
| `--tab <0-9>` | Login, Buttons, Inputs, Display, Layout, Popups, Animation, Styling, Translations, Diagnostics |
| `--scroll <px>` | How far that page starts scrolled |

And `bench` adds four:

| Flag | Effect |
| --- | --- |
| `--bench-seconds <n>` | How long to draw before printing and quitting |
| `--no-vsync` | Free-run, so the figures measure drawing and not waiting |
| `--fps <n>` | Hold the run to a rate, sleeping out the rest of each frame |
| `--boxes <n>` | How many boxes to rotate (64 by default) |

The last two exist because the same workload is drawn by four other frameworks
in `benchmarks/`, which take the same flags and print the same keys. Pair them
with `--renderer` and the same executable supplies three of the bars in
PERFORMANCE.md's chart.

```bash
./build/showcase --tab 7 --theme dark --shot styling.bmp --a11y-dump styling.txt
```

`--renderer` exists to measure that choice rather than assert it, and it is the
one flag that can change what the window looks like: the feathering rules in
`nk_sdl3_renderer.h` are written against the software rasterizer. On a machine
with no GPU it is a good way to confirm the choice — `--renderer direct3d11`
lands on WARP and costs four times the frame.

Everything else is a decision the library makes rather than one it asks about:
vsync and anti-aliasing are on, frames are drawn on events, and the assets are
found by walking up to the `.reaktor-root` marker. The showcase's Diagnostics page
reports what each of those settled on, plus where each frame's milliseconds
and each megabyte went. Prefer it to a guess: several plausible optimizations
here turned out to be measurably worse.

## The renderer

`auto` is **the software rasterizer, on every platform**. The app draws nothing
at rest, so a GPU buys it nothing it can measure; a GPU path costs
unconditionally, and on a machine without one it costs a great deal — SDL's
software renderer measured 13.1 ms of CPU per frame against Direct3D-on-WARP's
78.7. One rasterizer everywhere is also one set of pixels to reason about,
which matters because the feathering rules in `nk_sdl_render_ex` are written
against this one. `gpu`, or a driver by name, still pins what it always did.

The software renderer **puts every vertex on the pixel grid**. A correction
smaller than a pixel is therefore not a correction at all — it is discarded,
and no amount of re-deriving it will make it show.

`core/render/nk_sdl3_renderer.h` is Nuklear's SDL3 backend, vendored rather
than included because one of the changes is inside `nk_sdl_font_stash_end`.
Every deviation is marked `REAKTOR`: an 8-bit indexed atlas instead of RGBA32,
a 1×1 white texture for untextured geometry, pixel snapping, independent
feathering of fills and strokes, and the atlas's palette made public so the
[Text](#text) module's glyph textures can share it.

## The web build

Same sources, same `CMakeLists.txt`. `./build-wasm.sh` needs
[emsdk](https://emscripten.org) and produces `showcase.html`, `simple.html` and
`notepad.html` in `build-wasm/`, to be served over HTTP — a `file://` page
cannot fetch the `.wasm`.

Every push to `master` publishes the showcase to
[GitHub Pages](https://alpaq92.github.io/Reaktor/), as the site's front page —
`.github/workflows/pages.yml`, pinned to the emscripten the published sizes were
built with.

Assets are packaged with `--preload-file` and only the files the sources
actually name — the list is grepped at configure time, which took the bundle
from 3.55 MB to 1.30 MB. The showcase alone also packs `assets/locale` and the
Japanese font, which `simple` and `notepad` never open. `tools/shell.html` is
the page; its loading overlay sets `pointer-events: none`, without which a
full-viewport overlay swallows every canvas click and the app looks dead.

## Localization

The Locale module translates what a program says and formats what it counts:
a catalog per language, a lookup by key, plural forms, and numbers, money and
dates written the way the language writes them. The showcase's Translations
page is the working example — one page and one piece of code, in English,
Polish and Japanese.

**A catalog** is `assets/locale/<code>.txt`, one entry a line:

```
language = Polski
plural = n==1 ? 0 : n%10>=2 && n%10<=4 && (n%100<10 || n%100>=20) ? 1 : 2
file.save = Zapisz
file.count = {n} plik | {n} pliki | {n} plików
number.decimal = ,
money.PLN = zł
date.long = d MMMM y
```

A line splits at its first ` = `, and in a value `\n` is a newline and `\\` a
backslash. `language` is the catalog's name for itself, which is what a
language switch shows; `plural` is a gettext plural expression, as in a `.po`
header. There is no comment syntax, because a key is free text.

```c
char   buf[64];
time_t now = time(NULL);

reaktor_button(&(reaktor_button_spec){ .label = reaktor_tr("file.save") });
reaktor_label(&(reaktor_label_spec){
    .text = reaktor_trn("file.count", n, buf, sizeof buf) });   /* 3 pliki */

reaktor_format_money(buf, sizeof buf, 1234.5, "PLN");           /* 1234,50 zł */
reaktor_format_date(buf, sizeof buf, localtime(&now), REAKTOR_DATE_LONG);
```

`reaktor_tr` answers the current catalog's value, or the key itself when there
is none, so a string nobody translated shows up as its key rather than as
nothing. `reaktor_trn` picks one of the ` | ` forms by the catalog's rule, and
writes `{n}` as the number, formatted.

**Formatting** reads the keys a catalog carries for it, and a key a catalog
leaves out is formatted the way English is:

| Key | English | What it says |
| --- | --- | --- |
| `number.decimal` | `.` | The decimal separator |
| `number.group` | `,` | The grouping separator |
| `number.group.min` | `1` | The fewest digits allowed ahead of the first separator |
| `money` | `{s}{n}` | Where the symbol and the amount go |
| `money.<code>` | the code | A currency's symbol |
| `money.<code>.digits` | `2` | A currency's decimals |
| `date.short` | `M/d/yy` | A pattern, as are the three below |
| `date.long` | `MMMM d, y` | |
| `date.full` | `EEEE, MMMM d, y` | |
| `time.short` | `h:mm a` | |
| `date.months`, `date.months.short` | `January \| February \| …` | Twelve forms |
| `date.days`, `date.days.short` | `Sunday \| Monday \| …` | Seven forms, Sunday first |
| `time.ampm` | `AM \| PM` | |

A pattern is made of `d dd M MM MMM MMMM y yy EEE EEEE H HH h hh m mm a`, with
anything in `'quotes'` kept as it is. The weekday is worked out from the date,
not read from `tm_wday`. Each call writes at most `cap` bytes, terminator
included, and answers `buf`.

No table of conventions sits behind this: a catalog says how its language
writes a number the way it says how it writes "Save". The Polish one puts
no-break spaces where a line must not break, between groups of digits and
between an amount and its symbol. A symbol made of letters that would touch the
amount is kept off it by one regardless, so `PLN 12.00` rather than
`PLN12.00`.

**The language** is `--lang` when a catalog has that code; otherwise the first
of the system's preferred languages that one has; otherwise English; otherwise
the first catalog. `reaktor_locale_set` switches at any time, and the next
frame draws in it.

**Leave `.name` to the translated text**, so a screen reader reads what is
drawn. The price is that a language switch makes every translated widget a new
one: placed a frame late, once, and dropping focus if it had it.

**The atlas bakes what the catalogs use** and Aileron has: Latin-1, plus every
such character, at every size in both weights. The characters Aileron lacks
are the [Text](#text) module's to draw.

**Formatting stops at the patterns above.** No time zones, no calendar but the
Gregorian, and digits grouped in threes only — not the Indian lakh.

The accessibility tree carries the same strings the widgets draw, so a
translated interface is a translated screen reader with no extra work.

## Text

Nuklear draws a string from the font atlas, one glyph after another, left to
right. The Text module draws every string that needs more than that, and
breaks lines the way Unicode says to. A string whose characters are all baked
into the atlas is left to Nuklear, so Latin text costs what it always did.

- **Direction** by the Unicode bidirectional algorithm, through
  [mojibake](https://github.com/zaerl/mojibake). Arabic and Hebrew run right to
  left, with the numbers and Latin words inside them the right way round, and
  a bracket faces the way its direction needs. A wrapped paragraph that starts
  right to left keeps to the right edge, and every line keeps the paragraph's
  direction.
- **Shaping** by [kb_text_shape](https://github.com/JimmyLefevre/kb). Each run
  of one direction, one script and one font goes through OpenType's rules for
  its script: Arabic letters join, marks sit on their letters, Devanagari moves
  its vowel signs and forms conjuncts. Kerning is off, as it is in the atlas,
  so a word is as wide wherever it is drawn.
- **Fallback glyphs** from the first font that has them: the face's own file,
  then each `--font-fallback` in the order given, then each font the
  application adds with `reaktor_text_add_fallback`. A font is chosen a
  grapheme at a time, so a letter and its marks come from the same one.
- **Every size and weight.** Nothing is baked ahead of time. A glyph is
  rasterized the first time it is drawn, at the size it is drawn, into 512×512
  textures of the module's own, and lands on the pixel grid the way the atlas's
  glyphs do. A bold face takes its fallback glyphs from the same fonts as a
  regular one, so the showcase's bold Japanese is M PLUS 1p Regular. The
  Diagnostics page's font row counts the glyphs drawn from fallback fonts.
- **Lines break by UAX #14**, through mojibake: between Japanese characters but
  not before a full stop or a small kana, after a hyphen, never inside a
  number. A wrapped label is measured with the breaks it is drawn with, and each
  line is measured whole — shaped text is not the sum of its glyphs — so its
  box is exactly as tall as its lines.

**How it gets in.** `rebuild_font` hands the module every face after the bake,
and the module takes over each face's width function, so Nuklear measures a
string the way the module will draw it. Each frame, before `nk_sdl_render_ex`
turns the commands into vertices, the runtime calls `reaktor_text_prepare`,
which turns each text command the atlas cannot draw into a custom command in
the same place in the list — so the text still draws over what is under it and
under what is over it — and the custom command's callback adds the glyph
quads. Shaping is cached by string and font file, and does not depend on size;
rasterized glyphs are cached by font, glyph and size. Each cache starts over
when it fills.

**Fonts are the application's to supply.** The showcase ships M PLUS 1p for its
Japanese catalog, added in `sample_args`, and nothing else; a program that
shows Arabic, Hebrew or an Indic script points the module at a font that has
it:

```bash
./build/showcase --font-fallback C:/Windows/Fonts/segoeui.ttf --font-fallback C:/Windows/Fonts/Nirmala.ttc
```

Segoe UI has Arabic and Hebrew, Nirmala UI the Indic scripts. A font collection
is read at its first font.

Without the module, `reaktor_text_none` breaks a line after a space, or in a run
with none before the glyph that would not fit — keeping that glyph, which
`nk_label_wrap` loses.

mojibake is built with only what the module calls, its collation, IDNA,
security and character-name tables left out. kb_text_shape is one header,
compiled once, in `core/text/kb.c`. Between them they are most of what the
module costs — see [PERFORMANCE.md](PERFORMANCE.md#modules).

Not there yet:

- **Editing shaped text.** A text field measures its caret a glyph at a time,
  which is right for Japanese and wrong inside a joined Arabic word, and the
  caret moves in the order the text is stored, not the order it is shown.
- **A single-line label in a right-to-left language** is still set against the
  left edge; only wrapped paragraphs keep to the right one.
- **Language-specific forms.** The shaper is told a script but not a language,
  so a font's Serbian or Urdu variants go unused.
- **Color glyphs, emoji and vertical text.**
- **Normalization, case mapping and collation**, which mojibake has and
  nothing calls yet.

## Nuklear behaviors that have already cost an afternoon

Each was found the hard way. None is a bug.

- **Buttons fire on press.** `nk_input_is_mouse_pressed` needs a frame while
  the button is held, and a browser delivers press and release in one task, so
  that frame never happens. The build defines `NK_BUTTON_TRIGGER_ON_RELEASE`.
- **A button's content rect is bounds − padding − border − *rounding*.** A
  generous radius can drive a small button's content rect negative, at which
  point its symbol silently vanishes.
- **A negative content rect moves the *label* rather than hiding it.**
  `nk_widget_text` clamps a negative height to zero but leaves `content.y`
  below the middle, so tiny.css's 39.2px of inset drew the label **4.6px low**
  on every 30px control. `style_map.c` trims the vertical padding to what the
  row can hold, per widget.
- **`nk_stroke_rect` is asymmetric** — a 2px border measures 1px left and top,
  2px right and bottom. Anything stroking its own frame must stroke on the
  bounds, not inset by half, or it comes out shorter than the button beside it.
- **`nk_draw_button_text_symbol` always centers the label**; the alignment
  argument only chooses which side the glyph goes on.
- **`nk_do_selectable_image` reads inverted** — `NK_TEXT_ALIGN_LEFT` pins the
  image to the *right* edge.
- **A tree header's border rect is always square.** `NK_TREE_TAB` fills the
  header at a literal `0` rounding before the background, so the corners show
  through. `nk_combo_begin_color` does the same.
- **There is one `window.rounding` for every panel**, popups included.
- **`nk_menubar_begin` must precede all other layout** in its panel, and pins
  its row out of any scrolling.
- **A menu popup's height must be computed against its actual row spacing.**
  Nuklear's default is 4, not 2; assume 2 and the last item lands on the
  clipped edge with nothing left to click.
- **`nk_group_begin` returns 0 when scrolled out of view.** Returning early on
  that blanks everything below it.
- **`NK_POPUP_DYNAMIC` strokes its border at pre-shrink bounds**, so
  `nk_tooltip` shows a ghost frame.
- **`nk_layout_space` does not nest.**
- **`nk_image` stretches to its widget slot**, whatever raster size you asked
  for.
- **`nk_rule_horizontal` fills its whole widget rect** — the row height *is*
  the line thickness.
- **`nk_sdl_font_stash_begin` leaks the atlas it replaces.**
- **`nk_label_wrap` breaks at spaces only**, and a line without one keeps the
  glyph that overflowed, where the clip hides it — a character lost from the end
  of every line of Japanese. `reaktor_label` breaks its own lines.
- **A merged font joins the atlas's *first* font**, not the one added last:
  `merge_mode` extends `atlas->fonts`, whichever face the call was meant for.
- **`cfg.range` is read on every glyph lookup**, not only while baking. The
  ranges have to live as long as the fonts do, which is why `App` holds them.
