# Curie — Plan

Companion to [FINDINGS.md](FINDINGS.md), which records *what was chosen and
why*. This file records *what to build next*.

---

## 1. Where we are

| Piece | State |
|---|---|
| **Build: one `CMakeLists.txt`** | ✅ clean build, 333 targets; `build.ps1` is now just a wrapper |
| **SDL3 backend** (`nuklear_sdl3_renderer`) | ✅ running; picks `direct3d11` here, Metal/GL elsewhere |
| **Callback main loop** (`SDL_AppInit/Iterate/Event/Quit`) | ✅ browser-safe from the start (§8.1) |
| **litehtml + gumbo** built from submodule | ✅ C++17, no patching needed |
| **pico.css rendering** (`container_nk.cpp`, 30 virtuals) | ✅ **first slice renders** |
| **Zerove font** (CC0) | ✅ vendored, baked via stb_truetype at device size |
| **HiDPI scaling** (`src/metrics.c`, LCUI model) | ✅ verified 1x / 2x; `CURIE_SCALE` override |
| System theme + live switching | ✅ portable — `SDL_GetSystemTheme` |
| Ionicons → Open-Color icon (window + `.ico` resource) | ✅ verified by pixel sampling |
| plutovg + plutosvg | ✅ built as CMake targets |
| **Cross-platform (macOS/Linux/BSD/WASM)** | ⬜ **unblocked but unproven** — nothing left but to try it |
| Text input, devtools, accessibility | ⬜ see FINDINGS §15.3, §14.1, §15.1 |

**Binary size:** `curie.exe` 3.99 MB — 2.66 MB of that was the SDL3-trimmed
baseline; litehtml accounts for the rest.

### Two bugs worth remembering

**`curie_root()` used an ambiguous marker.** It walked up from the executable
looking for a `third_party` sibling — but `add_subdirectory(third_party/SDL)`
makes CMake mirror that path into the build tree, so `build/third_party/`
exists and the walk stopped one level early, resolving the root to
`…/Curie/build`. Every asset lookup then failed. Now it looks for a dedicated
`.curie-root` sentinel, with a `CURIE_ROOT` environment override for installed
layouts and for WASM, where walking up means nothing.

**Silent fallbacks hide their own cause.** The font loader fell back to
Nuklear's bitmap ProggyClean without saying so, which is indistinguishable
from "the font never loaded". It now reports either `Zerove @ 16px` or the
exact path that failed, in the UI.

> The window currently shows a **placeholder** ("smoke / toolchain ok /
> click"). It exists only to prove Nuklear + GDI compiles, links and runs. No
> pico.css is rendered yet — that is M2.

### Icon pipeline (done)

`tools/mkicon.c` renders `browsers-outline.svg` at 16/24/32/48/64/128/256 px,
recolours it from `open-color.json` (**violet-9** outline `#5f3dc4`,
**indigo-3** inside `#91a7ff`), writes `gen/curie.ico` + `gen/curie.rc`;
`rc.exe` compiles it and the linker embeds it, so Explorer shows the icon.
The running window sets the same glyph at 16/32 px via `WM_SETICON`.

Both paths share one code path (`curie_svg_surface`), and both read the SVG
and the palette from `third_party/` — nothing is hand-transcribed. Changing
the glyph or shades means editing the five variables at the top of
`build.ps1`. Inspect the recolour without launching the UI:

```bash
./build/curie.exe --dump-icon ./build/icon_check.png
```

---

## 2. Portability to Linux and macOS

Currently Windows-only. The blocker is narrower than it looks: the *asset*
pipeline is already portable, and only the platform shell is not.

| Piece | Today | Portable? |
|---|---|---|
| plutovg / plutosvg | pure C | ✅ already |
| litehtml | portable C++ | ✅ already |
| pico.css, Open-Color, Ionicons | data files | ✅ already |
| **Window / input / paint** | Nuklear **GDI** backend | ❌ Windows-only |
| **Build** | `build.ps1` + MSVC | ❌ Windows-only |
| **Exe icon** | `HICON`, `.rc`, `.ico` | ❌ Windows-only |
| **Path helpers** | `GetModuleFileNameA` | ❌ Windows-only |
| **Font loading** | `AddFontResourceExW` | ❌ Windows-only |

### 2.1 macOS is the deciding constraint

To be precise, because it is easy to overstate: **Nuklear runs on macOS.**
`nuklear.h` is 31k lines containing *zero* platform includes — no `windows.h`,
no X11, no Cocoa, no GL. It only emits draw commands and has no notion of an
operating system.

What is missing is a **native Cocoa/Metal backend** equivalent to `gdi`
(Windows) or `x11` (Linux). macOS is nonetheless covered by backends Nuklear
already ships demos for:

| Backend | Windows | Linux | macOS |
|---|---|---|---|
| `glfw_opengl2/3/4`, `glfw_vulkan` | ✅ | ✅ | ✅ |
| `sdl_opengl2/3`, `sdl_renderer`, `sdl3_renderer` | ✅ | ✅ | ✅ |
| `sfml_opengl2/3`, `allegro5` | ✅ | ✅ | ✅ |
| `rawfb` (you supply the window) | ✅ | ✅ | ✅ |
| `gdi`, `gdip`, `d3d9/11/12` | ✅ | ❌ | ❌ |
| `x11`, `x11_opengl2/3`, `xcb_cairo` | ❌ | ✅ | ❌ |

So the constraint is narrower than "macOS is unsupported": Windows and Linux
*could* each run a zero-dependency native backend (GDI, X11), but macOS needs
a cross-platform one. Maintaining three backends to save one dependency is a
bad trade — hence a single cross-platform backend everywhere.

| Option | Licence | Covers | FreeBSD | Notes |
|---|---|---|---|---|
| **SDL3 + `sdl3_renderer`** | Zlib | Win, Linux, macOS | ✅ `devel/sdl3` | Nuklear ships the backend; best macOS story. **Recommended** |
| GLFW + `glfw_opengl3` | Zlib | Win, Linux, macOS | ✅ `graphics/glfw` | Needs a GL context; GL is deprecated on macOS |
| sokol_app + `util/sokol_nuklear.h` | Zlib | Win, Linux, macOS, wasm | ⚠️ untested | Single-header, no external build; only option with BSD risk |
| `rawfb` + own shell | — | — | ✅ | No GPU dependency, but you write the platform shells |

All three are **Zlib**, which is MIT-compatible and slightly *more*
permissive — it requires no attribution in binary distributions. Full licence
and FreeBSD evidence in FINDINGS §8bis.

**Zlib is MIT-compatible** — permissive, no copyleft, and it does not even
require attribution in binary distributions. Same reasoning as CC0 in
FINDINGS §8.1: it satisfies the single-MIT policy.

### 2.2 Sequencing matters: switch the backend *before* litehtml

Changing backend also changes font handling — GDI fonts give way to Nuklear's
`stb_truetype` font atlas, which is portable **and** is what is needed to load
the CC0 TTF anyway (no `AddFontResourceExW`).

litehtml's `create_font` / `get_text_width` / `split_text` must bind to
whatever measures text. Doing the backend switch first means writing that
binding **once** instead of twice. This reorders the milestones below.

### 2.3 Build system: adopt CMake

The earlier "no CMake" stance was correct for a Windows-only build compiling a
handful of submodule `.c` files. It does not survive three platforms, three
compilers and a C++ dependency that already ships CMake. `build.ps1` becomes a
thin convenience wrapper around `cmake --build`.

### 2.4 Per-platform icon packaging

The SVG → RGBA pipeline is portable (plutosvg); only packaging differs.
`tools/mkicon.c` grows two output modes:

| Platform | Artifact | Notes |
|---|---|---|
| Windows | `.ico` + `.rc` → linked resource | ✅ done |
| macOS | `.icns` inside `Foo.app/Contents/Resources` | plus `Info.plist` |
| Linux | PNGs in `hicolor/<size>/apps` + `.desktop` | no in-binary icon concept |

`write_dib` stays Windows-specific; the render loop above it is shared.

### 2.5 System theme following

The title bar is drawn by the OS or window manager, never by Nuklear, so
matching it to the system scheme is irreducibly platform-specific.
`src/theme.h` keeps the portable surface to two calls —
`curie_prefers_dark()` and `curie_window_set_dark()` — taking the native
window as an opaque `void *`.

| Platform | Detect | Apply | State |
|---|---|---|---|
| Windows | `AppsUseLightTheme` registry value | `DwmSetWindowAttribute` attr 20 (19 pre-20H1) | ✅ done |
| macOS | `AppleInterfaceStyle` user default | `[NSWindow setAppearance:]` (usually automatic) | ⬜ needs an Obj-C TU |
| Linux/FreeBSD (X11) | XDG portal `org.freedesktop.appearance` over D-Bus | `_GTK_THEME_VARIANT` = `"dark"` window property | ⬜ |
| Wayland | portal | **not possible** without client-side decorations | ⬜ |

Live switching is wired on Windows via `WM_SETTINGCHANGE`
(`"ImmersiveColorSet"`) and `WM_THEMECHANGED`, which set a dirty flag the
main loop consumes — so flipping the system theme re-themes the running app
with no restart. Verified: `DwmGetWindowAttribute` reports `value=1` on a
dark system, and the re-theme path survives a live notification.

**Adopting SDL3 deletes this table.** SDL3 ships `SDL_GetSystemTheme()` and
`SDL_EVENT_SYSTEM_THEME_CHANGED`, and already calls
`DwmSetWindowAttribute` itself on Windows — so both unimplemented rows above
(the macOS Objective-C TU and the Linux/FreeBSD portal + `_GTK_THEME_VARIANT`
work) simply disappear. `src/theme.c` collapses into two SDL calls at M1.
See FINDINGS §8bis.

**Wayland is the one genuine gap** — a client cannot theme a decoration it
does not draw. Applications there either accept the compositor's decoration
or do client-side decorations themselves.

Nuklear's own palette is themed alongside the frame, but only lightly: once
litehtml renders pico.css, content theming becomes the stylesheet's job via
`prefers-color-scheme`, and `curie_prefers_dark()` becomes the input that
picks which media query applies.

### 2.6 Path helpers

`curie_root()` needs a portable "where am I" implementation:
`GetModuleFileNameA` (Windows), `/proc/self/exe` (Linux),
`_NSGetExecutablePath` (macOS). Roughly 20 lines behind one `#ifdef`.

---

## 3. Asset loading: runtime vs build-time (the `-Assets` flag)

Today every asset is read from `third_party/` at startup. That is ideal while
iterating and wrong for shipping a single binary. Both modes should exist.

### 3.1 This does not violate the no-hardcoded-values rule

Worth stating, because it looks like it might. The rule is that no upstream
value is **hand-transcribed** — upstream stays the single source of truth.
Baking preserves that: the generator reads the *same submodule files* the
runtime path reads, and emits `gen/` output that is generated, never edited
and never committed. Bumping a submodule and rebuilding propagates the change
with no hand-editing. What stays forbidden is a human copying `#5f3dc4` into
a `.c` file. `tools/mkicon.c` already works exactly this way.

### 3.2 Flag design

```powershell
.\build.ps1                    # default: -Assets Runtime
.\build.ps1 -Assets Baked      # embed everything into the binary
```

Sets `/DCURIE_BAKED_ASSETS` and puts `gen/` on the include path.

### 3.3 One API, two providers

Call sites must not branch. `src/curie.h` keeps its signatures; only the
provider changes:

| Function | Runtime | Baked |
|---|---|---|
| `curie_read_file` | `fopen` under repo root | pointer into a `const` blob |
| `curie_oc_color` | scans `open-color.json` | indexes a generated table |
| `curie_svg_icon` | plutosvg rasterises on demand | `memcpy` from a pre-rendered array |

Consequence: in baked mode plutosvg links only into the *generator*, so the
shipped binary loses a dependency and all startup rasterisation.

### 3.4 The generator

`tools/bake.c`, compiled from the same sources as the app — no new toolchain.
Emits into `gen/` (git-ignored):

| Output | Contents | Source |
|---|---|---|
| `gen/oc_palette.h` | family/shade → RGB | `open-color/open-color.json` |
| `gen/icons.h` | RGBA arrays per icon per size | `ionicons/src/svg/*.svg` |
| `gen/pico_css.h` | stylesheet as a byte blob | `pico/css/pico.css` |
| `gen/font.h` | TTF bytes | vendored Zerove |

A manifest (`tools/icons.txt`) keeps it from rasterising all 1357 Ionicons.

### 3.5 Trade-offs

| | Runtime | Baked |
|---|---|---|
| Distribution | needs `third_party/` beside the binary | self-contained |
| Startup cost | file I/O + SVG raster per icon | zero |
| Iteration | edit CSS, relaunch | rebuild |
| Binary size | small | grows with assets |
| Hot reload | possible | impossible by construction |

Keep **Runtime** as the development default; **Baked** is the release mode.

---

## 4. Milestones

### M1 — portable shell ← next
1. Adopt CMake; keep `build.ps1` as a wrapper.
2. Replace the GDI backend with **SDL3 + `sdl3_renderer`** (or sokol, §2.1).
3. Swap GDI fonts for Nuklear's `stb_truetype` atlas.
4. Make `curie_root()` portable (§2.5).
5. Confirm the window opens on Windows, Linux and macOS.

**Exit criterion:** the placeholder window runs on all three platforms.

### M2 — litehtml vertical slice
The decisive experiment; everything after is incremental.

1. Add `third_party/litehtml` to the CMake build.
2. `src/container_nk.cpp` — the single C++ TU implementing
   `document_container`, exposing a C API. Bind first: `create_font`,
   `get_text_width`, `split_text`, `draw_text`, `draw_solid_fill`,
   `draw_borders`, `set_clip`/`del_clip`.
3. Load `third_party/pico/css/pico.css` + a small HTML fragment, render one
   styled `<button>` into Nuklear's canvas via `nk_window_get_canvas()`.
4. Feed mouse position into `on_mouse_event` so `:hover`/`:active` light up.

**Exit criterion:** a pico.css-styled button that visibly reacts to hover.

**Risk to watch:** font metrics. `get_text_width` / `split_text` must agree
with what actually gets drawn, or text mis-wraps. Verify by measuring a known
string against its drawn width before building anything on top.

### M3 — Zerove font
Vendor the CC0 `.ttf` (itch.io is not a git host — the one documented
exception to the submodule rule, FINDINGS §8.3) and load it through the font
atlas. Pair with Unitblock for monospace.

### M4 — `-Assets Baked`
Implement §3, after M2, so the generator bakes formats that have settled.

### M5 — icons in content
Route litehtml's `draw_image` to plutosvg so `<img src="…ionicon.svg">`
resolves against the Ionicons submodule. The recolour helper generalises.

### M6 — devtools panel
Inspect the live tree, modelled on Freya's devtools (FINDINGS §14.1). Build
it with **plain Nuklear widgets** — `nk_tree_push` for the DOM, property rows
for computed styles — which is legitimate here because the "no Nuklear
widgets" rule (§2.1) constrains application *content*, not tool panels.
Sources are all data litehtml already holds: DOM, computed styles, box model,
winning cascade rule. Compile-time gated so release builds carry none of it.

### M7 — behaviour layer
Keyboard navigation, focus and widget semantics, ported from Zag.js state
machines (FINDINGS §7). litehtml supplies mouse hover/active only.

---

## 5. Open decisions

1. **Accept C++ for litehtml?** (FINDINGS §10) Blocks M2. The pure-C
   alternative cannot render pico.css without hand-writing var(), calc(),
   pseudo-elements, block/inline layout and flexbox.
2. ~~Backend~~ — **decided: SDL3.** Vendored at `third_party/SDL` (3.5.0,
   Zlib), paired with Nuklear's `nuklear_sdl3_renderer.h`. Covers Windows,
   macOS, Linux and FreeBSD, and collapses the theme/dialog/clipboard/IME/DPI
   layer into SDL calls (FINDINGS §8bis).
3. **Accessibility** — nothing in this stack provides it and SDL3 did not
   change that (FINDINGS §13.1). Decide before M2 whether it is a
   requirement, because it argues against the architecture rather than
   against any one component.
3. **Vendor the Zerove `.ttf`?** Blocks M3; needs a download.

## 6. Deliberately not doing

- ~~No `display:grid`~~ — **moved out of this list; it must be handled.**
  See §7 below.
- **No general SVG CSS engine.** `src/appicon.c` does a targeted recolour of
  the Ionicons outline idiom (`fill:none`, `stroke:#000`, unfilled `<path>`),
  not arbitrary SVG restyling.
- **No Nuklear widgets.** Nuklear is the canvas and input source; CSS layout
  and immediate-mode row/column layout cannot coexist (FINDINGS §2.1).

---

## 7. ⚠️ `display: grid` — must be handled

litehtml has **no grid renderer** (confirmed: no `render_grid`, no grid
properties in `css_properties.h`), so `display: grid` silently falls back to
block. Left alone, every `.grid` container stacks vertically — the mobile
layout, on every screen size. It fails quietly, which is what makes it worth
flagging.

**The exposure is small and precisely known.** Pico's entire grid system is
one utility class:

```css
.grid {
  grid-column-gap: var(--pico-grid-column-gap);
  grid-row-gap: var(--pico-grid-row-gap);
  display: grid;
  grid-template-columns: 1fr;                              /* < 768px */
}
@media (min-width: 768px) {
  .grid { grid-template-columns: repeat(auto-fit, minmax(0%, 1fr)); }
}
.grid > * { min-width: 0; }
```

That is all of it: **3 × `grid-row-gap`, 3 × `grid-column-gap`, 2 ×
`grid-template-columns`**, and a single `display: grid`.

**Fix: a CSS shim, not engine work.** litehtml *does* implement flexbox —
`m_flex_grow`, `m_flex_shrink`, `m_flex_basis`, `m_flex_direction`,
`m_flex_wrap`, `m_flex_justify_content`, `m_flex_align_items`,
`m_flex_align_self` are all present. And `repeat(auto-fit, minmax(0, 1fr))` is
semantically "equal columns that wrap", which is exactly `flex: 1 1 0` on the
children:

```css
/* loaded after pico.css */
.grid      { display: flex; flex-wrap: wrap; }
.grid > *  { flex: 1 1 0; min-width: 0; }
```

Roughly five lines, no engine changes, and it keeps upstream pico.css
untouched — the override is a separate stylesheet, so the submodule stays the
source of truth.

**Open detail:** `gap` / `row-gap` / `column-gap` support in litehtml's flex
implementation is **unconfirmed** — the property names did not appear in the
grep. If gaps are unsupported, use margins on `.grid > *` with a negative
margin on the container. Verify during M2, when the first real stylesheet
renders.

---

## 8. WASM — and what it forces on the design

A fifth target: the browser, via Emscripten. SDL3 supports it well
(`src/video/emscripten/` with events, framebuffer and mouse backends;
`SDL_PLATFORM_EMSCRIPTEN`; CMake carries `PreseedEmscriptenCache.cmake`), so
the platform itself is not the problem. Two consequences are, and both are
cheaper to accept **now** than to retrofit.

### 8.1 The main loop must be callback-driven — decide before M1

A browser tab cannot be blocked. A `while (running) { ... }` loop freezes the
page: it never yields to the event loop, so nothing renders and the tab hangs.

SDL3 solves this natively with **main callbacks**: `SDL_MAIN_USE_CALLBACKS`
plus `SDL_AppInit` / `SDL_AppIterate` / `SDL_AppEvent` / `SDL_AppQuit`,
returning `SDL_APP_CONTINUE`. SDL drives the loop, so on the web it hands
control back to the browser each frame, and on native it is an ordinary loop —
**identical application code on all five targets**.

**Therefore M1 should adopt the callback model directly**, rather than porting
today's `while (running)` loop and rewriting it later. This is the single
highest-value consequence of adding WASM, and it costs nothing to take now.

It also composes with the redraw gating in §2.5: `SDL_AppIterate` returning
early when nothing changed is exactly the right shape, on both web and native.

### 8.2 Assets: there is no filesystem in a browser

The governing rule — read every asset from `third_party/` at runtime — has no
meaning in a browser tab. `fopen("third_party/pico/css/pico.css")` cannot
work. Two options, both legitimate:

| Option | How | Trade-off |
|---|---|---|
| **Emscripten preload** | `--preload-file third_party/pico/css/pico.css@/pico.css` packages assets into a `.data` file mounted as a virtual FS | `fopen` keeps working unchanged; **no code changes at all**. Ships a second file next to the `.wasm` |
| **`-Assets Baked`** (§3) | assets compiled into the binary | One artifact; smaller total; but requires the baked provider from §3 to exist |

**Recommendation:** preload for the first WASM build (zero code change), and
treat baked mode as the size optimisation it is. Note this **promotes §3 from
optional to eventually-required** if a single-artifact web build is wanted.

### 8.3 An honest note

We are building an HTML/CSS renderer that will run *inside* a browser, which
already has one. That is coherent — the point is one UI codebase rendering
identically on five targets, and the web build is how you demo or embed it —
but it is worth stating plainly that the browser is the one target where the
DOM is already free, and where a thin DOM backend would be less work than
litehtml-on-canvas. Not a reason to change course; a reason not to pretend the
tradeoff is invisible.

### 8.4 Toolchain

**Emscripten is not installed** (checked at project start: `emcc` absent).
An `emsdk` install is roughly 1 GB. Nothing else is missing: SDL3 builds under
`emcmake cmake`, litehtml is portable C++, plutovg/plutosvg are portable C,
and Nuklear has no platform code at all.

### 8.5 Platform matrix

| Target | Backend | Status |
|---|---|---|
| Windows | SDL3 `windows` | ✅ SDL3 built static here (15.6 MB lib, 278 objects) |
| macOS | SDL3 `cocoa` (Metal) | ⬜ |
| Linux | SDL3 `x11` / `wayland` / `kmsdrm` | ⬜ |
| FreeBSD | SDL3 `x11` / `wayland` | ⬜ (FINDINGS §8bis) |
| **WASM** | SDL3 `emscripten` | ⬜ needs emsdk; callback loop required |

---

## 9. Rendering performance: why we cost more than Explorer, and the ladder out

All figures below are measured on this machine, not estimated. "Hover" means
the pointer driven continuously at ~66 moves/second across the card's buttons
and links — a deliberate worst case, several times harsher than real use.

| Configuration | Hover | Idle |
|---|---|---|
| AA on (default) | **32.1%** | 0.4% |
| AA off (`CURIE_AA=0`) | **8.0%** | 0.3% |
| AA off + software renderer | 9.7% | 0.6% |

Counters from the same runs: `layouts=1, rebuilds=1` (constant), and roughly
**6 paints per second** during a sweep. Six repaints costing ~32% means a
single full repaint costs on the order of **20 ms of CPU**.

### 9.1 Why Explorer does more work for less CPU

It does not do more work. It does **far less work per change**, because the
whole native stack is retained-mode with damage tracking:

| | Explorer / Win32 | Curie today |
|---|---|---|
| Idle window | composited by DWM from a cached surface; app does nothing | nothing (we gate redraws) — parity |
| Hover a button | `InvalidateRect` on that button; `WM_PAINT` arrives clipped to ~100x40 px | **entire page repainted** |
| What gets regenerated | the damaged rectangle only | full document walk, every draw command, full tessellation, full vertex upload |
| Text | DirectWrite with a system-wide glyph cache | our atlas (fine) but re-tessellated per frame |
| Geometry | retained in the control's cached bitmap | rebuilt from scratch every paint |

So the gap is **granularity, not efficiency**. Explorer repaints ~4,000 px²
when you hover a button; we repaint ~294,000 px². That is the entire story,
and it is why micro-optimising our paint path would miss the point.

The AA multiplier compounds it: `nk_convert` emits fringe geometry for every
shape, so anti-aliasing costs 4x — but it is also what fixed the jagged edges,
so it is a real trade rather than waste.

### 9.2 The optimisation ladder

**Level 0 — redraw gating. Done.** Skip the frame entirely when nothing
changed. Idle went from ~47% to ~0.3%. This is why idle is already at parity
with a native app.

**Level 1 — use the damage rectangles we already receive.** Cheap, and
currently thrown away:

```cpp
// container_nk.cpp today
static void noop_redraw(const litehtml::position &) {}
d->document->on_mouse_over(x, y, x, y, noop_redraw);
```

That callback **is** the damage list — litehtml tells us exactly which
rectangles changed, and we discard every one. Separately,
`document::draw(hdc, x, y, const position *clip)` already takes a clip, so
passing a damage rect makes litehtml skip elements outside it: fewer draw
commands emitted, less geometry tessellated. On its own this is not enough,
because the rest of the window would be undefined — which leads to:

**Level 2 — retained render target. The actual fix.**

1. Create an `SDL_Texture` with `SDL_TEXTUREACCESS_TARGET`, window-sized.
2. Render the page into it **once**.
3. Every frame: blit that texture. One textured quad, essentially free.
4. On damage: `SDL_SetRenderTarget(tex)`, scissor to the damaged rect, redraw
   only that region (passing it as litehtml's clip), restore the target.
5. Resize or theme change: re-render the whole texture.

This is precisely Explorer's model — a cached surface plus partial
invalidation. Expected effect: hover cost falls from ~20 ms to roughly the
damaged fraction of the page, so **1–2 ms**, with anti-aliasing left on.
That is the "best of both worlds": idle ~0%, hover near-free, no quality
sacrifice, and no loss of responsiveness.

Estimated effort: moderate — a render target, damage collection, and clip
plumbing through `curie_doc_draw`. Roughly 150–250 lines, all in
`main.c` and `container_nk.cpp`.

**Level 3 — stop re-parsing CSS on every page rebuild.** Each theme or
diagnostics toggle re-parses ~91 KB of pico plus Open-Color plus app.css,
because `document::createFromString` parses stylesheets every time. Rebuilds
are rare and user-initiated, so this is not urgent — but it is the reason the
theme switch feels heavier than the diagnostics toggle. The real fix is DOM
mutation instead of rebuilding, which litehtml supports poorly (§14.1 in
FINDINGS covers the same limitation).

**Level 4 — selective anti-aliasing.** The backend hardcodes
`config.shape_AA = config.line_AA = AA`. Text and images gain nothing from
fringe geometry; rounded rectangles and strokes do. Splitting them needs
either a patched backend or our own `nk_convert` config, which means giving up
`nuklear_sdl3_renderer.h` as-is.

**Level 5 — vertex caching.** Nuklear re-tessellates identical geometry every
frame. Level 2 makes this mostly moot: unchanged regions are never
re-tessellated at all because they are never redrawn.

### 9.3 Recommendation

Do **Level 2**. Levels 3–5 are refinements of a path that Level 2 largely
eliminates, and Level 1 is a prerequisite that falls out of it naturally.

Until then, `CURIE_AA=0` is the honest lever: 4x cheaper hovering, at the cost
of the smooth edges AA was added to provide.

### 9.4 Click latency is a different problem from hover cost

Measured directly: a page rebuild takes **45-65 ms**. That is the delay on any
click that changes state - the theme switch, the diagnostics toggle - and it
is entirely CSS re-parsing.

`document::createFromString` accepts stylesheets only as **strings**, so every
rebuild re-parses Open-Color, pico and app.css from scratch. There is no API
to hand it an already-parsed stylesheet.

| Attempt | Rebuild time |
|---|---|
| `pico.css` (93 KB) | 51-65 ms |
| `pico.min.css` (83 KB) | 45-56 ms |

Minifying bought ~15% and is kept, but it does not solve the problem: parse
cost scales with stylesheet size, and pico is simply large.

**Note the asymmetry the user observed:** hovering is smooth while clicking
lags. That is exactly right and diagnostic - hovering only repaints
(~20 ms, and no rebuild), whereas clicking re-parses everything (~50 ms) *and*
repaints. The two need different fixes.

**Options for the click path, in order of preference:**

1. **Re-apply the cascade without re-parsing.** `media_changed()` internally
   does `refresh_styles()` + `compute_styles()` on the existing tree - exactly
   the operation needed - but it is gated behind a media-list check and there
   is no public entry point for "the DOM changed, restyle it". Exposing one
   (a small patch to litehtml, or a PR upstream) would make theme switching
   essentially free.
2. **Cache documents by state.** Keep a small map from state key
   (theme + diagnostics visibility) to a built document and swap pointers on
   click. First visit to a state costs 50 ms, every later toggle is instant.
   Costs memory: a full DOM and style tree per cached variant.
3. **Shrink the stylesheet.** Only pico's used rules are needed; a build-time
   subset would cut parse time proportionally. Cuts against reading upstream
   files verbatim at runtime.

Option 1 is the correct fix and the only one that scales; option 2 is the
pragmatic one that needs no upstream change.
