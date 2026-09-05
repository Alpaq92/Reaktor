# Curie — Findings

Research log for a **portable desktop UI that renders real HTML/CSS through
Nuklear**. Every claim below was verified against source or a live API, not
recalled from memory. Date: 2026-09-05.

**Governing constraint:** all external sources are **git submodules read
dynamically at runtime**. No values are transcribed or code-generated into C
headers — upstream stays the single source of truth.

---

## 1. Toolchain (verified on this machine)

| Tool | Version | Note |
|---|---|---|
| MSVC | 14.44.35207 | VS 2022 Build Tools |
| Windows SDK | 10.0.26100 | also 10.0.22621 |
| node / npm | 24.16.0 / 11.13.0 | research + asset inspection only |
| git | 2.54.0.windows.1 | |
| Python | 3.13.13 | |
| Emscripten | **absent** | not needed for the native target |

`build.ps1` locates MSVC via `vswhere`, emits a temp `.bat` (avoids
PowerShell→cmd quote mangling) and compiles with `/TC`.

> **Gotcha:** with `/TC`, `.lib` files **must** follow `/link`, or `cl` treats
> them as C source files.

**Status: `build\curie.exe` compiles and runs** — Nuklear + GDI, zero external
runtime dependencies (`user32` / `gdi32` / `Msimg32` only).

---

## 2. The pipeline, and who does each stage

An HTML/CSS renderer decomposes into six stages. The central question of this
research was which are available and which we must write.

| Stage | Provider | Status |
|---|---|---|
| HTML → DOM | engine | ✅ |
| CSS parse | engine | ✅ |
| Cascade → computed properties | engine | ✅ |
| **Layout (boxes → x/y/w/h)** | engine | ⚠️ **only litehtml** |
| Paint | Nuklear canvas | ✅ |
| Interaction / behavior | engine + us | ⚠️ **only litehtml** |

### 2.1 Nuklear must be used as a canvas, not a widget toolkit

CSS box layout and Nuklear's immediate-mode row/column layout are
fundamentally incompatible. `nk_button_label` and friends are unusable here.
The supported approach is `nk_window_get_canvas()` → `nk_fill_rect` /
`nk_stroke_rect` / `nk_draw_text`. Nuklear still supplies the window, input,
fonts, clipping and the draw-command buffer.

The GDI backend's command coverage was checked and is sufficient:
`LINE RECT RECT_FILLED CIRCLE CIRCLE_FILLED ARC ARC_FILLED TRIANGLE POLYGON
POLYGON_FILLED POLYLINE TEXT CURVE RECT_MULTI_COLOR IMAGE CUSTOM SCISSOR`.

### 2.2 CSS carries no behavior

`:hover` / `:focus` / `:active` are *selectors*. Something must still decide
when an element is hovered or focused and drive keyboard navigation. litehtml
supplies mouse-driven hover/active state; keyboard navigation and widget
semantics remain ours (see §7).

---

## 3. 🔴 Blocker that decided the engine: custom properties

pico.css is built **entirely** on CSS custom properties — **501 declarations**,
plus 63 `calc()` and 69 `::before/::after`. Support is therefore
make-or-break, and it was measured directly:

| Engine | `var()` / custom properties | Layout | Verdict |
|---|---|---|---|
| **libcss** (NetSurf) | **none** — 0 matches in 246 source files | none | ❌ pico renders as nothing |
| **htmlcss** | **none** | none | ❌ |
| **litehtml** | **yes** — `style.cpp`, `html_tag.cpp` | **yes** | ✅ |

The libcss result was confirmed against a working control grep
(`CSS_PROP_COLOR` matches; `custom` does not), so it is a true negative, not a
path error. libcss supports 126 CSS properties, none of them custom.

**Consequence:** with libcss or htmlcss, every color, spacing and radius in
pico.css resolves to unset and the page renders as effectively nothing. Making
pico work on those engines would require writing a `var()` substitution pass, a
`calc()` evaluator, pseudo-element box generation, a full block/inline layout
engine *and* flexbox.

---

## 4. Engine decision: **litehtml**

Given the delegation to pick the best library for interpreting pico.css,
litehtml wins unambiguously — it is the only candidate covering the whole
pico.css feature surface:

| pico.css requires | litehtml | evidence |
|---|---|---|
| 501 custom properties / `var()` | ✅ | `src/style.cpp`, `src/html_tag.cpp` |
| 63 `calc()` | ✅ | 5 modules incl. `css_properties.cpp` |
| 69 `::before` / `::after` | ✅ | `src/el_before_after.cpp` |
| block + inline layout | ✅ | `render_block.cpp`, `render_inline_context.cpp` |
| 4 flex rules | ✅ | `render_flex.cpp`, `flex_item.cpp`, `flex_line.cpp` |
| 1 grid rule | ⚠️ **must be handled** | no `render_grid` — silently degrades to block. Scope and a ~5-line flexbox shim in **PLAN §7** |
| tables | ✅ | `render_table.cpp` |

### 4.1 It also solves the two biggest risks

Choosing litehtml **eliminates the layout engine** — previously the single
largest work item — and supplies mouse-driven `:hover`/`:active` state.

### 4.2 The integration surface is a clean fit for Nuklear

`document_container` is a 34-virtual interface that maps almost 1:1:

| litehtml callback | Nuklear |
|---|---|
| `draw_text` | `nk_draw_text` |
| `draw_solid_fill` | `nk_fill_rect` |
| `draw_borders` | `nk_stroke_rect` |
| `draw_linear_gradient` | `nk_fill_rect_multi_color` |
| `set_clip` / `del_clip` | `nk_push_scissor` |
| `draw_image` | plutosvg → `nk_image` |
| `create_font` / `split_text` | GDI font + `nk_font` metrics |
| `on_mouse_event` / `on_element_click` / `set_cursor` | Nuklear input |

It builds with **CMake**, which is MSVC-friendly — unlike NetSurf's Makefile
buildsystem.

### 4.3 ⚠️ The cost: this is no longer strictly "pure C"

litehtml is **C++ and BSD-3-Clause**, not C and MIT. The mitigation is that it
is the *only* C++ component: application logic stays in C, and
`document_container` is implemented in a single C++ translation unit that
forwards into Nuklear and exposes a C API. The realistic shape is
**"pure C app + one C++ binding shim."**

The alternative — staying strictly pure C on libcss — means writing
var()/calc()/layout/flexbox by hand, with pico.css as the hardest possible
first target. That is thousands of lines before a single pixel is correct.

### 4.4 Full candidate comparison

| Library | License | Lang | Cascade | var() | Layout | Verdict |
|---|---|---|---|---|---|---|
| **litehtml** | BSD-3 | C++ | ✅ | ✅ | ✅ | **Chosen** |
| NetSurf libcss | MIT | C | ✅ | ❌ | ❌ | pure-C fallback |
| katana-parser | MIT | C | ❌ parse tree only | ❌ | ❌ | lightweight fallback |
| htmlcss | Apache-2.0 | C | ✅ | ❌ | ❌ | dropped |
| lexbor | Apache-2.0 | C | ✅ | — | ❌ | license |
| gumbo-parser | Apache-2.0 | C | ❌ HTML only | — | ❌ | license |

### 4.5 htmlcss, for the record

`run.h` — its "Block run header", i.e. the layout module — declares **zero
types and zero functions**. Started, never implemented. What it *does* provide
is real: `hcHTMLImport`, `hcCSSImport`, `hcHTMLFindNode`, and a 3411-line
`css-compute.c` exposing `hcNodeComputeCSSBox` / `CSSText` / `CSSDisplay`,
producing populated `hc_box_t` and `hc_text_t`. Rejected on Apache-2.0 plus
the missing var() and layout support.

### 4.6 The NetSurf stack, for the record

All MIT, all pure C, all actively maintained — the strongest pure-C option if
the C++ dependency is ever ruled out:

| Repo | Role | Last push |
|---|---|---|
| `netsurf-browser/libcss` | CSS parser + selection/cascade | 2026-01 |
| `netsurf-browser/libdom` | DOM | 2026-01 |
| `netsurf-browser/libhubbub` | HTML5 parser | 2026-01 |
| `netsurf-browser/libparserutils` | input streams, encodings | 2026-01 |
| `netsurf-browser/libwapcaplet` | string interning | 2026-02 |

---

## 5. CSS primitive libraries — measured, not guessed

Every file parsed directly. All are MIT (verified via npm registry).

| Library | KB | rules | flex | grid | vars | media | `::before/after` | calc |
|---|---|---|---|---|---|---|---|---|
| 98.css | 26 | 143 | 7 | 0 | **0** | **1** | **0** | 1 |
| simple.css | 13 | 124 | 1 | 1 | 26 | 7 | 5 | 3 |
| xp.css / 7.css | 250* | 207 | 7 | 0 | **0** | **0** | **0** | 2 |
| water.css | 32 | 313 | 1 | 0 | 42 | 92 | 5 | 10 |
| **pico.css** ← chosen | 91 | 409 | 4 | 1 | **501** | 18 | **69** | **63** |
| nes.css | 276 | 488 | 6 | 0 | 0 | 13 | **169** | 3 |
| bulma | 746 | 4502 | 80 | 11 | **5891** | 251 | 37 | 272 |
| open-props | 8 | 81 | 1 | 2 | 1 | 2 | 2 | 3 |

\* xp.css's size is inflated by base64 image assets, which need image decode.

- **pico.css — selected.** Viable *because* litehtml handles var/calc/pseudo.
- **98.css** — most tractable (zero vars, zero pseudo-elements); would have
  been the bring-up target on a hand-written engine. litehtml removes the need.
- **bulma** rejected: 5891 custom properties, 251 media queries.
- **nes.css** rejected: its pixel art *is* 169 `::before/::after` + box-shadow.
- **CCSS** (`sathify/CCSS`) rejected: MIT, but last commit **2014-11-28**, and
  it is a CSS *architecture methodology*, not a component library.

### USWDS — dropped, for the record

`uswds.css` measures 645 KB / 7706 rules / 424 media queries / 252
pseudo-elements — bulma's weight class. Separately, **`uswds/uswds` ships no
`dist/`** (API returns 404): compiled CSS exists only in the npm package, and
no C engine parses SCSS. Replaced by Open-Color.

---

## 6. Resolved component choices

| Concern | Choice | License |
|---|---|---|
| Renderer | Nuklear (as canvas, §2.1) | MIT / public domain |
| **Platform backend** | **SDL3 3.5.0** + Nuklear's `nuklear_sdl3_renderer.h` | **Zlib** |
| HTML/CSS engine | **litehtml** | BSD-3-Clause |
| CSS primitives | **pico.css** | MIT |
| Colors | **Open-Color** v1.9.1 | MIT |
| Icons | **Ionicons** v8.1.0 — 1357 SVGs at `src/svg/` | MIT |
| SVG raster | **plutosvg** v0.0.8 + plutovg | MIT |
| Font | **open** — see §8 | |

**Open-Color** ships `open-color.json` beside `open-color.css` at the repo
root. The JSON path avoids CSS parsing entirely for palette lookups — useful
regardless of engine.

---

## 7. Behavior layer

litehtml covers mouse hover/active. Keyboard navigation, focus management and
widget semantics (roving tabindex, menu/combobox/tabs behavior) remain ours.
Evaluated sources for the *specification* of that behavior:

| Source | License | Portability to C |
|---|---|---|
| **Zag.js** (Ark UI engine) | MIT | **Best** — explicit FSMs, DOM isolated per component |
| Ariakit | MIT | `.d.ts` gives state shape; behavior spread across React hooks |
| Radix / Base UI / Headless UI | MIT | behavior welded to React components |
| React Aria | Apache-2.0 | broad, fails license filter |

Zag ships 20 primitives split into `anatomy` / `machine` / `connect` / `props`
/ `dom`. A machine is literally `states → on → {guard, target, actions}`,
which transliterates into a C `switch` table. TypeScript is an *asset* here:
the types are the machine-readable contract.

---

## 8. Fonts

### 8.1 CC0 is MIT-friendly — in fact strictly more permissive

**CC0 is a public domain dedication, not a licence with conditions.** It waives
copyright and neighbouring rights worldwide, so it grants *everything* MIT
grants and then removes MIT's one obligation: the attribution / licence-notice
requirement. Anything CC0 can therefore be vendored into an MIT-licensed
project without adding a single condition, and there is no compatibility
question to resolve. **CC0 satisfies the project's single-MIT policy.**

Practical consequence: CC0 assets need no `NOTICE` entry and no licence header
propagation, though crediting the author remains courteous.

### 8.2 Selected: GGBotNet CC0 fonts

All three verified directly from their itch.io pages — each states *"This Font
Software is licensed under the Creative Commons Zero v1"*:

| Font | Licence | Type | Source |
|---|---|---|---|
| **Zerove** ← chosen | **CC0 v1** ✅ | display / UI | `ggbot.itch.io/zerove-font` |
| **Jupiteroid** | **CC0 v1** ✅ | techno sans | `ggbot.itch.io/jupiteroid-font` |
| **Unitblock** | **CC0 v1** ✅ | **monospace** | `ggbot.itch.io/unitblock-font` |

Unitblock being monospace makes it the natural pairing for any code or tabular
UI alongside Zerove.

### 8.3 ⚠️ Distribution wrinkle

itch.io is **not a git host**, so these fonts cannot be git submodules — the
one exception to the project's submodule rule. Options: vendor the `.ttf`
into the repo (CC0 makes this unencumbered), or add a fetch step to
`build.ps1`. Vendoring is recommended; CC0 imposes no redistribution terms.

### 8.4 Alternatives considered, for the record

| Font | Licence | Type |
|---|---|---|
| Hack | **MIT** | monospace |
| Go Regular / Go Mono | BSD-3 | proportional + mono |
| Inter, Public Sans, Fira Sans | OFL-1.1 | proportional |
| JetBrains Mono | OFL-1.1 | monospace |

Hack's licence text confirms: *"The work in the Hack project is Copyright 2018
Source Foundry Authors and licensed under the MIT License"* (bundled DejaVu is
public domain; the Bitstream Vera portion carries reserved font names).

Worth recording: **MIT *proportional* UI fonts are essentially nonexistent** —
SIL OFL is the fonts world's standard, so an MIT-only filter would have
excluded nearly every good UI typeface. For embedding a font in an
application, OFL is not meaningfully more restrictive than MIT either; it
forbids only selling the font by itself and requires renaming modified
versions carrying reserved names. The CC0 route sidesteps the question
entirely.

### 8.5 Bundling mechanics

With the Nuklear GDI backend a private TTF loads via
`AddFontResourceExW(path, FR_PRIVATE, 0)` — pure C + Win32, no new deps.
litehtml's `create_font` / `split_text` / `get_text_width` must then be wired
to the same GDI metrics, or text will mis-wrap (see §11).

---

## 8bis. Platform backends and FreeBSD

### Licences (read from each project's LICENSE file, not inferred)

| Backend | Licence | Copyright |
|---|---|---|
| **SDL3** | **Zlib** | Sam Lantinga, 1997–2026 |
| **GLFW** | **Zlib/libpng** | Marcus Geelnard; Camilla Löwy |
| **sokol** | **zlib/libpng** (stated verbatim) | Andre Weissflog, 2018 |

**Zlib is MIT-compatible and marginally more permissive.** Its three
conditions are: don't misrepresent the origin, mark modified versions, don't
strip the notice from *source* distributions. Unlike MIT it requires **no
attribution in binary distributions** — so, as with CC0 (§8.1), it satisfies
the single-MIT policy rather than straining it.

### FreeBSD

Yes — the stack runs on FreeBSD. Verified against the FreeBSD ports tree and
each project's platform gating:

| Component | FreeBSD | Evidence |
|---|---|---|
| Nuklear | ✅ | zero platform code in `nuklear.h`; the `x11` backend is native |
| **SDL3** | ✅ | `devel/sdl3` in ports; `__FreeBSD__` in `SDL_platform_defines.h` |
| **GLFW** | ✅ | `graphics/glfw` in ports; CMake gates X11/Wayland on `UNIX;NOT APPLE` |
| **sokol** | ⚠️ | gate is `__linux__ \|\| __unix__` and FreeBSD defines `__unix__`, so it compiles down the X11 path — but BSD is neither referenced nor advertised (0 hits for `__FreeBSD__` in 15k lines of `sokol_app.h`) |
| plutovg / plutosvg | ✅ | `graphics/plutovg`, `graphics/plutosvg` both in ports |
| litehtml | ✅ | not packaged (`www/litehtml` absent), but portable C++ built from our submodule |
| pico.css, Open-Color, Ionicons | ✅ | data files |
| OpenGL / X11 | ✅ | `graphics/mesa-libs`, `x11/libX11` in ports |

**Consequence for the backend choice:** SDL3 and GLFW have first-class
FreeBSD support; sokol would probably work but is the only one carrying BSD
risk. If FreeBSD is a real target rather than a nice-to-have, that breaks the
tie in favour of **SDL3**.

### SDL3 also solves system theming portably — decisive

Discovered while implementing the dark title bar. SDL3 ships exactly the API
`src/theme.c` hand-rolls per platform:

| SDL3 symbol | Replaces |
|---|---|
| `SDL_GetSystemTheme()` → `SDL_SYSTEM_THEME_LIGHT/DARK/UNKNOWN` | registry read, `AppleInterfaceStyle`, XDG portal over D-Bus |
| `SDL_EVENT_SYSTEM_THEME_CHANGED` | `WM_SETTINGCHANGE` / `WM_THEMECHANGED` and their per-platform analogues |
| `DwmSetWindowAttribute` inside `SDL_windowswindow.c` (4 call sites) | our dark-frame call |

So adopting SDL3 **deletes the whole per-platform theming layer**, including
the two unimplemented paths: the macOS Objective-C translation unit and the
Linux/FreeBSD XDG-portal + `_GTK_THEME_VARIANT` work. Combined with
first-class macOS and FreeBSD support and Nuklear's ready-made
`sdl3_renderer` / `sdl3_opengl3` backends, **SDL3 is the recommendation**.

Residual gap unchanged by any of this: under Wayland a client still cannot
theme a decoration it does not draw.

### Why a portable backend was sought at all — and when it is *not* worth it

Recorded because it justifies the project's first external runtime
dependency, and the answer is conditional.

Nuklear already ships **native, zero-dependency** backends for Windows
(`gdi`) and Linux/FreeBSD (`x11`). Those three platforms need nothing added.
**macOS is the entire reason a portable layer came up**: no native backend
exists, so without one you write a Cocoa/Metal backend yourself.

Beyond windowing, SDL3 also supplies five services Nuklear deliberately does
not — the same ones raised as objections #3, #4 and #6 in §13:

| Service | Hand-rolled | SDL3 |
|---|---|---|
| System theme + change events | registry / `AppleInterfaceStyle` / XDG portal | `SDL_GetSystemTheme`, `SDL_EVENT_SYSTEM_THEME_CHANGED` |
| File dialogs | 3 native implementations | `SDL_ShowOpenFileDialog` / `SaveFile` / `OpenFolder` |
| Clipboard | 3 native implementations | `SDL_GetClipboardText` / `SetClipboardText` |
| IME / composition | 3 native implementations, genuinely hard | `SDL_StartTextInput`, `SDL_EVENT_TEXT_EDITING(_CANDIDATES)` |
| HiDPI scale | 3 native implementations | `SDL_GetWindowDisplayScale`, `SDL_GetWindowPixelDensity` |

**The decision therefore hinges on one question — is macOS a target?**

- **No (Windows + Linux + FreeBSD only):** SDL3 buys little. Use `gdi` +
  `x11`, keep **zero external runtime dependencies**, and implement the five
  services twice. Windows theming is already done (`src/theme.c`); IME is the
  costly one. This fits the project's stated instincts best.
- **Yes:** SDL3 pays for itself twice — a Cocoa backend nobody writes, plus
  ~15 platform implementations (5 services × 3 platforms) collapsed into 5
  calls.

Cost either way: `curie.exe` currently links only `user32` / `gdi32` /
`msimg32`. SDL3 would be the first real external dependency in the tree.

---

## 9. License ledger

| Component | License | Filter |
|---|---|---|
| Nuklear | MIT / public domain | ✅ |
| pico.css | MIT | ✅ |
| Open-Color | MIT | ✅ |
| Ionicons | MIT | ✅ |
| plutosvg / plutovg | MIT | ✅ |
| **Zerove / Jupiteroid / Unitblock** | **CC0 v1** | ✅ more permissive than MIT (§8.1) |
| Hack | MIT | ✅ |
| Zag.js | MIT | ✅ |
| NetSurf stack (libcss et al.) | MIT | ✅ |
| katana-parser | MIT | ✅ |
| **litehtml** | **BSD-3-Clause** | ⚠️ permissive, not MIT |
| SDL3 / GLFW / sokol | **Zlib** | ✅ more permissive than MIT (§8bis) |
| htmlcss | Apache-2.0 | ❌ dropped |
| lexbor, gumbo | Apache-2.0 | ❌ |
| USWDS | CC0 + Apache-2.0/OFL assets | ❌ dropped |

---

## 10. Open decisions

1. **Accept BSD-3 + C++ for litehtml?** It removes the layout engine entirely.
   The pure-C alternative (libcss) cannot render pico.css without a large
   hand-written CSS subsystem. This is the only remaining fork.

Resolved: engine → litehtml (§4); CSS → pico.css (§5); colours → Open-Color
(§6); font → Zerove, CC0 (§8).

## 11. Risks

- **C++ boundary** — one shim TU; keep litehtml's types out of the C headers.
- **Font metrics** — `create_font` / `split_text` / `get_text_width` must agree
  with the GDI backend's metrics or text will mis-wrap.
- **`display:grid`** — litehtml has no grid renderer; pico's 1 grid rule
  degrades to block layout.
- **Scrolling / viewport** — litehtml renders to a size we choose; scrolling is
  ours.

---

## 12. Repository state

| Path | Pin | Key asset | Status |
|---|---|---|---|
| `third_party/nuklear` | master | `nuklear.h`, `demo/gdi/nuklear_gdi.h` | ✅ |
| `third_party/litehtml` | master | CMake lib, `document_container.h` | ✅ added |
| `third_party/pico` | master | `css/pico.css` (committed) | ✅ added |
| `third_party/open-color` | master | `open-color.json` + `.css` | ✅ added |
| `third_party/ionicons` | v8.1.0 | `src/svg/` — 1357 SVGs | ✅ |
| `third_party/plutosvg` | v0.0.8 | + nested plutovg | ✅ |
| `third_party/98css` | main | `style.css` | ⚪ unused, retained as fallback |
| ~~`third_party/htmlcss`~~ | — | — | ⛔ removed (§4.5) |
| Zerove font | — | `.ttf` | ➕ pending, not a git host (§8.3) |

`build.ps1` compiles all of `src/*.c`; the build is currently green.

### Next step

Vertical slice: implement `document_container` over Nuklear's canvas, load
`third_party/pico/css/pico.css` plus a small HTML fragment, and render one
real styled button with working `:hover`/`:active`. That validates fonts,
metrics, painting and hit-testing in one screen; everything after is
incremental.

---

## 13. Nuklear: carried-over evaluation

Imported from a sibling project's assessment (a git client on `libgiturbo`,
weighing Nuklear against NAppGUI). Some framing is specific to that project —
its `src/sys/` rule, its USWDS palette — but the substance applies here, so it
is recorded to be worked through. Each point is annotated with its status
**given the decisions already taken in this document**: SDL3 as backend
(§8bis), litehtml as engine (§4), plutovg already in-tree.

### For

| # | Point | Status here |
|---|---|---|
| 1 | Best licence on the list: MIT **or** Unlicense, caller's choice. The Unlicense arm is zero-obligation — nothing downstream must carry. NAppGUI's MIT obliges a notice; Nuklear need not | ✅ holds |
| 2 | Vendoring is trivial: one header, no build system, no CMake integration | ✅ confirmed — it is a submodule here and `nuklear.h` compiles with two `#define`s |
| 3 | Zero dependencies by construction — never opens a window or calls the OS | ✅ confirmed — 31k lines, **zero** platform includes (§2.1) |
| 4 | Immediate mode fits a UI that is a projection of state, re-derived each frame | ✅ holds, and more so here: litehtml re-lays-out from the DOM, so the frame is already a projection |
| 5 | One code path, three platforms; no per-platform behaviour to test | ✅ strengthened — SDL3 extends this to four (incl. FreeBSD) |
| 6 | Design tokens reach **every** pixel, which native widgets structurally cannot | ✅ holds — here it is pico.css + Open-Color rather than USWDS, but the argument is the same, and stronger since litehtml paints everything |

### Against

| # | Objection | Status here |
|---|---|---|
| 1 | You supply the backend; Nuklear's are demos, not a supported portable layer | ⚠️ **Mitigated.** SDL3 *is* a supported portable layer; Nuklear ships `sdl3_renderer`. The glue is ours but thin, and the platform `#ifdef`s collapse to almost none |
| 2 | Text is the hard part: font atlas, **no shaping** | ⚠️ **Partly resolved.** Nuklear's atlas is not needed — **plutovg is already in-tree and is a full TTF renderer** (`plutovg_font_face_load_from_file/data`, `plutovg_canvas_fill_text`, `plutovg_font_face_get_metrics`), and litehtml requires its own text path regardless. CJK needs glyph coverage, not shaping, so `目录/文件.txt` is fine. **Complex-script shaping (Arabic, Indic) remains genuinely absent** — that would need HarfBuzz |
| 3 | No file dialog | ✅ **Resolved.** SDL3 ships `SDL_ShowOpenFileDialog`, `SDL_ShowSaveFileDialog`, `SDL_ShowOpenFolderDialog` |
| 4 | Edit widget has no IME composition and no system clipboard | ✅ **Resolved.** SDL3 ships `SDL_GetClipboardText`/`SDL_SetClipboardText`/`SDL_HasClipboardText`, and `SDL_StartTextInput` / `SDL_SetTextInputArea` with `SDL_EVENT_TEXT_INPUT`, `SDL_EVENT_TEXT_EDITING`, `SDL_EVENT_TEXT_EDITING_CANDIDATES`. Wiring these into the text field is ours |
| 5 | **No accessibility on any platform. Not partial — none** | ❌ **Open and unresolved.** Nothing in this stack provides it. litehtml computes ARIA-relevant semantics from the DOM but exposes them to nothing; there is no UIA / AT-SPI / NSAccessibility bridge. This is the one objection no decision here answers |
| 6 | HiDPI is manual; redraws every frame; gating is yours | ⚠️ **Partly resolved.** SDL3 supplies `SDL_GetWindowDisplayScale`, `SDL_GetDisplayContentScale`, `SDL_GetWindowPixelDensity`. Applying scale to layout and **gating redraw on input** stay ours |

### What actually remains open

1. **Accessibility (#5)** — genuinely nothing, on any platform. If it is a
   requirement rather than a nice-to-have, it argues against this entire
   architecture, not against Nuklear specifically. Worth deciding early.
2. **Complex-script shaping (#2)** — absent. CJK is fine; Arabic/Indic would
   need HarfBuzz. Acceptable if the target text is Latin/CJK.
3. **Redraw gating and DPI-aware layout (#6)** — ours to write, but bounded.

### 13.1 Status after the SDL3 decision

The backend is settled (SDL3 3.5.0, vendored at `third_party/SDL`), which
closes several objections outright. What is left, ranked by risk:

| # | Objection | Now |
|---|---|---|
| 1 | You supply the backend | ✅ **Closed.** SDL3 is a supported portable layer, and Nuklear ships `nuklear_sdl3_renderer.h`. `SDL_Renderer` selects D3D11/12, Metal or OpenGL/Vulkan per platform, so no GL context is ours to manage |
| 3 | No file dialog | ✅ **Closed.** `SDL_ShowOpenFileDialog` / `SaveFile` / `OpenFolder` |
| 6 | HiDPI | ⚠️ **Half closed.** SDL3 reports the scale factors; **applying** scale to litehtml layout and **gating redraw on input** remain ours — bounded work |
| 4 | IME + clipboard | ⚠️ **API closed, wiring open.** SDL3 supplies the events and clipboard. But Nuklear's edit widget is unusable here (litehtml owns content, §2.1), so a **caret, selection and text-input model over litehtml is ours to build**. Larger than it first appears |
| 2 | Text / shaping | ⚠️ **Partly open.** plutovg (already in-tree) renders TTF; CJK needs coverage, not shaping. **Complex scripts — Arabic, Indic — remain unsupported** and would need HarfBuzz. Separately, font *metrics* must agree between `get_text_width`/`split_text` and what is drawn, or text mis-wraps |
| 5 | **Accessibility** | ❌ **Fully open, unchanged.** SDL3 does not provide it either. No UIA / AT-SPI / NSAccessibility bridge exists anywhere in this stack |

**The two that need a decision rather than just work:**

1. **Accessibility (#5).** Nothing in this architecture provides it, and SDL3
   did not change that. If it is a requirement, it argues against the whole
   approach, not against Nuklear. Decide before M2, not after.
2. **Complex-script shaping (#2).** Acceptable to skip for Latin/CJK targets;
   adding HarfBuzz later is possible but is a C++ dependency of its own.

---

## 14. Target API/SDK shape

**Goal recorded; details to be elaborated later.** The developer-facing API
that Curie exposes should read like these three, all MIT:

| Reference | Language | Why it is a model |
|---|---|---|
| [LCUI](https://github.com/lc-soft/LCUI) | **C** | The closest existing analogue: a C library that builds interfaces from **CSS and XML**. Same premise as Curie — a stylesheet-driven UI in C — so its API decomposition is the most directly transferable |
| [NAppGUI](https://nappgui.com/en/home/web/home.html) | **ANSI-C** | An SDK, not just a library: a coherent cross-platform application framework in plain C, with an opinionated object/lifecycle model worth borrowing |
| [Freya](https://github.com/marc2332/freya) | Rust (Skia) | Declarative, component-oriented, non-web GUI — the *ergonomics* target, even though the language differs |
| [LVGL](https://github.com/lvgl/lvgl) | **C** | MIT, ★24.6k, actively developed. Not an engine alternative — it has no CSS parser, only its own style API — but the **most mature pure-C GUI codebase available**, and a reference for three specific open problems (below) |

**LVGL is worth singling out** because it answers open items rather than just
modelling an API:

| Our gap | What LVGL has |
|---|---|
| `display: grid` (PLAN §7) | **`src/layouts/grid`** — a pure-C CSS-grid-style layout, alongside `src/layouts/flex`. If the flexbox shim proves insufficient, this is a portable MIT reference implementation |
| Text input (§15.3) | `src/widgets/textarea`, `src/widgets/ime`, `src/widgets/keyboard` — a second reference beside LCUI |
| Fonts | `src/font/` with FreeType binding, a font manager, subpixel rendering and binary font loading |

Taken together these point at: a **C SDK** (not a bare rendering library),
stylesheet-driven, with a declarative surface over the retained DOM that
litehtml already maintains — rather than exposing `document_container` or
Nuklear primitives to callers.

Open questions deferred to that later elaboration: whether markup is authored
as HTML/XML or constructed through C calls; how application state binds to
the DOM; and where the Zag-derived behaviour machines (§7) surface in the
public API.

### 14.1 Developer tools — inspect the live tree

**Wanted, modelled on Freya's devtools** ("examine the component tree in
real-time"): a panel that shows the running UI's element tree and lets you
select a node and see what the engine actually computed for it.

This is a natural fit here, and worth noting *why*: litehtml maintains a
**retained DOM with computed styles**, so the tree already exists and is
already annotated. An immediate-mode UI has no such structure to inspect —
the widget tree is gone by the end of the frame. Choosing an HTML/CSS engine
buys inspectability essentially for free.

What it can surface, all from data litehtml already holds:

| Panel | Source |
|---|---|
| Element tree | the litehtml DOM |
| Computed styles per node | `css_properties` after the cascade |
| Box model (margin / border / padding / content) | the render tree's boxes |
| Which rule won, and from which stylesheet | the cascade |
| Highlight-on-hover overlay | box rects → Nuklear canvas |

**Notable consequence for §2.1.** The rule "no Nuklear widgets" applies to
*application content*, where CSS layout and immediate-mode layout conflict.
It does **not** apply to devtools: a tool panel is exactly what Nuklear's own
widgets are good at (`nk_tree_push` for the DOM tree, property rows for
computed values, a splitter for the layout). So devtools can be built quickly
with plain Nuklear while content goes through litehtml — the two coexist
cleanly because they occupy different windows.

Delivery options, to be decided later: a second SDL window (closest to
Freya's separate devtools app), or an overlay toggled inside the main window.
Either way it should be compile-time gated so release builds carry none of it.

---

## 15. Resolutions for the four open objections

Researched rather than assumed; each claim verified against source.

### 15.1 #5 Accessibility — what it actually means

The concern is not "no keyboard support" — that we can write. It is that
**assistive technology cannot see the UI at all.**

A screen reader does not read pixels. It queries an OS accessibility API —
NSAccessibility (macOS), UI Automation (Windows), AT-SPI over D-Bus
(Linux/BSD) — asking "what is on screen?". A native button answers because
the OS widget registers itself in that tree. **Everything Curie draws is a
rectangle on a canvas.** To the OS there is one opaque window with nothing
inside it.

| | Native toolkit | Curie today |
|---|---|---|
| Screen reader announces a button | ✅ automatic | ❌ silent |
| Focus ring visible to AT | ✅ | ❌ |
| OS zoom / high-contrast modes | ✅ honoured | ❌ ignored |
| UI automation (Appium, WinAppDriver) | ✅ can drive it | ❌ finds no elements |
| Dictation, switch access, braille | ✅ | ❌ |

**Why it is architectural rather than a component choice.** No swap fixes it:
SDL3 exposes no accessibility API, Nuklear has no widget identity to publish,
and litehtml computes ARIA-relevant semantics but publishes them nowhere. Any
GUI that paints its own pixels inherits this — Dear ImGui, Nuklear, Freya and
essentially every game UI are in the same position. It is the standing trade
for total visual control.

**One mitigation worth knowing.** We are better placed than a typical
immediate-mode UI, because litehtml keeps a **retained DOM with roles and
semantics already in it**, so a bridge is *possible*: walk the DOM and publish
it to UIA / AT-SPI / NSAccessibility. But that is one provider per platform,
and a body of work comparable to the rest of the app.

**The decision:** if assistive-technology support or accessibility-driven UI
automation is a real requirement, this architecture is the wrong one and
native widgets (NAppGUI's approach) is the honest answer. If the target is a
self-contained visual tool, this is the normal accepted trade — but record it
deliberately rather than discover it late.

### 15.2 #2 Complex-script shaping — use HarfBuzz, do not port rustybuzz

The premise that HarfBuzz fails the licence filter was **wrong**:

| Option | Licence | Language | Verdict |
|---|---|---|---|
| **HarfBuzz** | **"Old MIT"** — MIT-family | C++ with a **pure C API** (`hb.h`) | ✅ **Recommended** |
| libraqm | MIT, C | wraps HarfBuzz + FriBidi | convenience layer, not an alternative |
| SheenFigure | **Apache-2.0**, C | OpenType shaping, ★34 | fails filter; tiny community |
| SheenBidi | **Apache-2.0**, C | bidi only | fails filter |
| FriBidi | **LGPL-2.1**, C | bidi only | copyleft — excluded |
| rustybuzz | MIT, Rust | a *port of* HarfBuzz | see below |

**The rustybuzz port was measured: 74 files, 29,232 lines of Rust** — and the
bulk is exactly what cannot be skipped: `unicode_norm.rs` 3,063 lines,
`tag_table.rs` 2,483, `buffer.rs` 1,894, `ot_shaper_indic.rs` 1,883,
`ot_layout_gsubgpos.rs` 1,360. It would also be a Rust→C reimplementation *of
HarfBuzz*, undertaken to avoid depending on HarfBuzz — which is already MIT
and already exposes a C API.

Decisive: **the C++ concession was already made for litehtml**, so HarfBuzz
adds no new category of dependency. There is no pure-C, MIT, production-grade
shaper; SheenFigure is the only real candidate and it is Apache-2.0 with 34
stars.

**Plan:** ship without shaping — Latin and CJK are correct without it — and
add HarfBuzz behind the same `document_container` text seam if a complex
script is ever required. Nothing in the current design forecloses it.

#### Candidates that are not shapers

Three further suggestions were checked. **All three solve rasterization, not
shaping** — the next stage down the pipeline, and not a substitute:

- **Shaping** (HarfBuzz): Unicode + font tables (GSUB/GPOS) to positioned
  glyph IDs — ligatures, contextual forms, mark positioning, Arabic joining,
  Indic reordering.
- **Rasterization** (these three): a glyph outline to pixels.

| Library | Licence | What it is | Relationship to HarfBuzz |
|---|---|---|---|
| **VEFontCache** | MIT, C | single-header **GPU glyph atlas / renderer** (stb_truetype, utf8.h, rectpack2D) | *Consumer.* README: "cached text shaping **with HarfBuzz** with simple Latin-style fallback", and "**TODO: HarfBuzz not supported yet, coming soon!!**" — only a Latin fallback shaper exists today |
| **Sluggish** | Unlicense, C | **toy/experimental** implementation of Eric Lengyel's Slug rendering algorithm; author calls it "a quick prototype … a useful learning resource" | None. Also **Windows x64 only**, requiring OpenGL 3.2 + GLEW + SDL2. Carries a patent history (README states it no longer applies) |
| **Tehreer-Android** | Apache-2.0, Java/Android | Android text engine | *Consumer.* README: "a wrapper over C libraries, FreeType, SheenBidi and **HarfBuzz**" |

This **reinforces** the recommendation rather than challenging it: two of the
three depend on HarfBuzz, and the third is a single-platform prototype. Most
telling, Tehreer-Android is by the **SheenFigure author**, who nonetheless
chose HarfBuzz over their own shaper for a production Android engine.

**Separate, genuinely useful takeaway:** VEFontCache is a credible option for
a *different* problem — GPU glyph caching — if text rasterization ever becomes
a bottleneck. It is not needed now (plutovg already renders TTF via
stb_truetype), and a GPU atlas would cut across `SDL_Renderer`'s abstraction,
so it is noted rather than adopted.

Three further candidates were checked, with the same result:

| Library | Licence | Language | What it actually is |
|---|---|---|---|
| **trex** | MIT | C++, ★38 | Rasterizer + atlas + shaping — but by its own README it "uses **FreeType**" and "**integrates HarfBuzz** to shape text". A convenience wrapper over both |
| **parley** | **Apache-2.0 OR MIT** (dual — it does pass the filter) | Rust, ★723 | Rich-text **layout**. Its four dependencies are Fontique, **HarfRust**, Skrifa and ICU4X; HarfRust is "a Rust port of HarfBuzz" |
| **crowbar** | Apache-2.0 | TypeScript, ★48 | Not a library at all — a **shaping debugger** that visualises how GSUB/GPOS lookups transform a string |

### 15.2.1 The options, stated plainly

Across all six suggestions, every one either **wraps** HarfBuzz, **ports**
HarfBuzz, **debugs** HarfBuzz's output, or solves rasterization instead. That
is the finding: there is no production-grade pure-C shaping alternative.

| # | Option | Verdict |
|---|---|---|
| **A** | **Ship without shaping** | ✅ **Recommended now.** Latin and CJK are already correct. Adds nothing; forecloses nothing |
| **B** | **HarfBuzz directly** | ✅ Old MIT, C API; the C++ concession is already made. Everything else in this table wraps it. The mature choice |
| **B2** | **hamza** — see below | ✅ **The pure-C MIT option, and it fits this project's style** |
| C | trex | ❌ Pulls in FreeType **and** HarfBuzz, duplicating plutovg's rasterization. ★38 |
| D | VEFontCache | ➖ Rasterization, not shaping; its HarfBuzz support is an unimplemented TODO |
| E | Port rustybuzz / HarfRust to C | ❌ 29,232 LOC of Rust, to reimplement HarfBuzz and avoid HarfBuzz |
| F | SheenFigure | ➖ The only true pure-C shaper, and Apache-2.0 *is* permissive — but **Arabic only**, and dormant. See below |
| G | parley | ❌ Right licence, wrong layer and language: Rust + FFI, and it duplicates litehtml's layout role |

**Bookmark:** crowbar is worth remembering as a *diagnostic* if shaping ever
misbehaves — it shows lookup-by-lookup what a shaper does to a string. A tool
to reach for, never a dependency.

#### Correction: there *is* a pure-C **MIT** shaper — hamza

Stated twice above that none exists. That was wrong.
[hamza](https://github.com/saidwho12/hamza) is a **header-only C99
Unicode/OpenType shaping library under MIT** — `#define HZ_IMPLEMENTATION`,
then include `hz.h`. It vendors exactly like `nuklear.h`, which is precisely
this project's preferred shape.

| | hamza | HarfBuzz |
|---|---|---|
| Licence | **MIT** | Old MIT |
| Language | **C99, header-only** | C++ with a C API |
| Integration | drop one header into `third_party/` | CMake/meson subproject |
| Optional | `HZ_NO_STDLIB` | — |
| Maturity | ★53, one author, last pushed **2025-03** | every browser and OS ships it |
| Script coverage | Arabic-focused; broader claims unverified | complete |
| Unicode data | generated UCD headers, regenerable from unicode.org | bundled |

**So the real trade is maturity versus fit**, not licence or language. hamza
matches the project's pure-C / MIT / header-only instincts better than
anything else found; HarfBuzz is the one every shipping product relies on.

Nothing forces the choice yet — option A (ship without shaping) stands, and
both plug in behind the same `document_container` text seam. Revisit when a
complex script becomes a requirement, and prefer hamza if the target scripts
are within its coverage.

#### Correction: there *is* one pure-C permissive shaper

Stated too broadly above. **SheenFigure is pure C and Apache-2.0 — which is a
permissive licence**, in the same family as the BSD-3 and Zlib dependencies
already accepted here. So the accurate claim is narrower: there is no pure-C
**MIT** shaper, and only one pure-C permissive one. What actually disqualifies
it is capability and maintenance, not licence:

| Property | SheenFigure |
|---|---|
| Language / deps | pure C; only SheenBidi plus `stddef/stdint/stdlib/string.h` — genuinely minimal |
| Licence | Apache-2.0 (permissive) |
| Script coverage | **Arabic only** — its README: "Currently, it only supports Arabic script and a subset of GDEF, GSUB and GPOS tables" |
| Last commit | **2023-11-17** (~3 years stale); CI still points at the defunct Travis |
| Community | ★34, 4 open issues |

**So it depends what "complex script" means.** If the requirement were
*specifically Arabic*, SheenFigure is a legitimate pure-C, permissive,
dependency-light answer and worth reconsidering. For Indic, Thai, Khmer or
anything broader it does not implement the shaping engines at all, and
HarfBuzz remains the only option.

### 15.3 #4 Text input — the plan, and why no engine swap helps

**First, is there a better engine? Checked, and no.**

| Engine | Text editing built in? | Renders pico.css? |
|---|---|---|
| litehtml | ❌ none — an `<input>` is just a box | ✅ yes |
| **LCUI** | ✅ `textinput` + `textcaret` + IME | ❌ **no** — see below |
| libcss / htmlcss | ❌ | ❌ |

LCUI was genuinely promising: MIT, pure C, with `textinput.h`, `textcaret.h`
and a per-platform IME layer. But its CSS engine cannot render pico.css.
Grepping `lib/css/` for custom properties returns **three matches that are all
C decrement operators** (`--i`, `--right`), and the three `calc` hits are a
**comment stating calc() is deliberately unimplemented**. That is the same
blocker that eliminated libcss (§3). **Swapping engines would trade a working
renderer for a text widget — a clear regression.**

**So the model is ours, and LCUI shows the right shape.** Its platform seam is
a five-function vtable (`prockey`, `totext`, `open`, `close`, `setcaret`) plus
`ptk_ime_commit(wchar_t*, len)` and `ptk_ime_set_caret(x, y)`. **We do not
need that seam** — SDL3 already is it — so only the consumer side is ours:

| Piece | Approach | Rough size |
|---|---|---|
| Focus registry | which DOM node has focus; litehtml supplies hit-testing | ~50 LOC |
| Edit model | per editable node: UTF-8 buffer, caret index, selection anchor/extent | ~250 LOC |
| Key handling | arrows, Home/End, word jumps, Backspace/Delete, shift-select | ~150 LOC |
| Clipboard | `SDL_GetClipboardText` / `SDL_SetClipboardText` on Ctrl+C/V/X | ~40 LOC |
| IME pre-edit | `SDL_EVENT_TEXT_EDITING` draws composition underlined; `SDL_EVENT_TEXT_INPUT` commits | ~100 LOC |
| Caret follow | `SDL_SetTextInputArea` so the candidate window tracks the caret | ~20 LOC |
| Hit-test x to caret index | **must reuse the `get_text_width` measure path** | ~60 LOC |

**That estimate is pessimistic — most of it already exists.** Nuklear embeds a
**stb_textedit-derived state machine**: `nk_textedit_init` / `_text` /
`_delete` / `_delete_selection` / `_cut` / `_paste` / `_select_all`,
**`nk_textedit_undo` and `_redo`** backed by `NK_TEXTEDIT_UNDOSTATECOUNT`,
selection state (`select_start`, `select_end`, `has_preferred_x`), and
`nk_plugin_copy` / `nk_plugin_paste` hooks. Caret motion, word boundaries,
selection and undo — the parts that are genuinely hard to get right — are
already in the tree, already tested, and under Nuklear's MIT/public-domain
terms. What remains is glue: supply a measurement callback, draw the caret and
selection into litehtml's box, and route SDL events in.

#### The three options

| | Approach | Cost | Gets you |
|---|---|---|---|
| **A** ⭐ | **Drive `nk_textedit_*` with our own layout.** Reuse the state machine; supply measurement, drawing and event routing | **~250–300 LOC glue** | Caret, selection, word motion, undo/redo, cut/paste free. No new dependency |
| **B** | **Native overlay widget.** On focus, position a real OS control over the box — Win32 `EDIT`, `NSTextField`, GTK `Entry`, and on WASM a real DOM `<input>` | 4 platform implementations | IME, clipboard, undo, spellcheck **and accessibility** for text, all free and locale-correct |
| **C** | **Full in-house editor.** Write the model, motion, selection and undo from scratch | ~650+ LOC | Total control; needed only for bidi-aware editing stb_textedit cannot express |

**Recommended: A now, B as the accessibility lever later.**

A is clearly cheapest and adds nothing to the dependency set. Its limit is
that stb_textedit assumes a single-font, single-direction row model — fine for
`<input>` and simple `<textarea>`, strained by bidi or mixed fonts.

B deserves a second look for one reason: **it is the only option that yields
any accessibility at all** (§15.1). A native text control registers itself
with the platform's accessibility tree, so screen readers can read and edit
that field even though the rest of the UI is opaque pixels. On WASM it is also
the *easiest* option, since overlaying a real DOM `<input>` is trivial. If
partial accessibility ever becomes a goal, focused text fields are the highest
-value place to start, and B is the mechanism.

C is not justified unless complex-script editing becomes a requirement.

**Two design rules matter more than the line count:**

1. **Keep the edit buffer outside litehtml.** Edit in our own model, write the
   value back to the node and re-layout on change. This is what browsers do —
   an input's shadow tree is not the document — and it avoids re-entering
   layout on every keystroke.
2. **One measurement path.** Caret placement, hit-testing and drawing must all
   call the same measure function. Two paths means the caret drifts from the
   glyphs, the classic failure in hand-rolled editors.

### 15.4 #6 HiDPI — LCUI's model, and it really is small

LCUI's `lib/ui/include/ui/metrics.h` is the whole idea:

```c
float ui_get_actual_scale(void);
int   ui_compute(float value);            /* (int)(scale * value) */
void  ui_compute_rect(pd_rect_t *actual, const ui_rect_t *logical);
```

**The principle: lay out in logical (CSS) pixels and apply scale only at the
paint and input boundary.** Never scale the layout itself. Freya does the same
through Skia's scale factor. NAppGUI delegates to native widgets, so its
approach does not transfer.

This maps onto our architecture with very little code, precisely because
`document_container` is *already* the single boundary between logical layout
and device pixels:

| Step | Change |
|---|---|
| Read the scale | `SDL_GetWindowDisplayScale()` into one global; refresh on `SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED` |
| Paint | multiply rects and coords by scale inside the `draw_*` callbacks — one helper, ~10 call sites |
| Fonts | `create_font` rasterises at `size * scale` but reports **unscaled** metrics back to litehtml |
| Input | divide SDL mouse coordinates by scale before `on_mouse_event` |
| Viewport | report `get_viewport` in logical px (`pixels / scale`) |

Total: one inline helper, one global, ~10 call sites and two event cases —
small only because everything already funnels through one interface.

**Redraw gating is separate and simpler.** Nuklear repaints every frame by
default; replace the polling loop with `SDL_WaitEvent` and repaint only when
an event arrives or an animation is pending. A loop change, not an
architectural one, and it is what takes idle CPU to zero.

---

## 16. Open question: is Nuklear still earning its place?

Recorded because the answer changed as the architecture settled, and it should
be decided deliberately rather than by inertia.

Nuklear was hired as the whole UI. Two later decisions took most of that away:
**litehtml** now owns layout, and **SDL3** now owns drawing. `SDL_Renderer`
already provides `SDL_RenderFillRect`, `SDL_RenderLine`, `SDL_RenderTexture`
and `SDL_RenderGeometry`, so for application content there are now two
abstractions between a CSS box and a pixel:

```
litehtml -> nk_fill_rect -> nk vertex buffer -> SDL_RenderGeometry
                                 ^ could paint straight here
```

What Nuklear still uniquely supplies:

| Capability | Still unique to Nuklear? |
|---|---|
| `nk_textedit_*` state machine (§15.3 option A) | ❌ it is stb_textedit, which is public-domain and usable standalone |
| Font atlas / stb_truetype baking | ❌ SDL_ttf or plutovg's text API also cover it |
| **A widget set for the devtools panel (§14.1)** | ✅ **yes** — `nk_tree_push`, property rows, splitters |
| Draw-command buffering and batching | ➖ useful, but `SDL_Renderer` batches already |

**So its remaining unique value is the devtools panel.**

**Recommendation: keep it through M2, then decide.** It is a single
MIT/public-domain header costing essentially nothing, and removing it before
litehtml is integrated would be optimising an architecture that has not been
validated. But if `document_container` ends up painting directly to
`SDL_Renderer` — which is the simpler design — then Nuklear becomes "the
toolkit the devtools are built in". That is a legitimate reason to keep it,
just not the job it was originally brought in for.

**Decide after M2**, when the paint path is real and the cost of each option
is measurable rather than predicted.
