# Third-party notices

Reaktor is MIT ([LICENSE](../LICENSE)). Everything it is built on is a git
submodule under `external/`, used as it ships; each license below was read
from the project's own license file at the pinned revision.
`git submodule status` prints the pins.

| Component | Used for | License |
| --- | --- | --- |
| [SDL](https://github.com/libsdl-org/SDL) | Window, input, renderer, system theme, file dialog, tray | zlib |
| [Nuklear](https://github.com/Immediate-Mode-UI/Nuklear) | Widgets, font baking, the SDL3 backend, and the stb_truetype the Text module rasterizes with | MIT **or** public domain |
| [NukAnim](https://github.com/Alpaq92/NukAnim) | Every animation, and `nukanim.h` for applications — a port of [ImAnim](https://github.com/soufianekhiat/ImAnim) with [ImAnimate](https://github.com/RaidcoreGG/ImAnimate)'s `animate` | MIT |
| [LCUI](https://github.com/lc-soft/LCUI) | `libcss` (parse, match, cascade) and `yutil` | MIT |
| [Onlay](https://github.com/Alpaq92/Onlay) | The rectangles — a fork of [randrew/layout](https://github.com/randrew/layout) | MIT |
| [tiny.css](https://github.com/ihsan6133/tiny.css) | The stylesheet the look comes from, compiled in | MIT |
| [simple.css](https://github.com/kevquirk/simple.css) | The sheet the Styling page swaps in | MIT |
| [Ionicons](https://github.com/ionic-team/ionicons) | Every icon, as SVG; the seven the widgets draw are compiled in | MIT |
| [plutosvg](https://github.com/sammycage/plutosvg) | Rasterizing the SVGs | MIT |
| [plutovg](https://github.com/sammycage/plutovg) | plutosvg's canvas (nested submodule) | MIT |
| [mojibake](https://github.com/zaerl/mojibake) | Bidirectional text (UAX #9) and line breaking (UAX #14), for the Text module | MIT |
| [kb_text_shape](https://github.com/JimmyLefevre/kb) | OpenType shaping, script segmentation and font coverage, for the Text module | zlib |

Only LCUI's `libcss` and `yutil` are built, not the toolkit. SDL is built
static, without audio, joystick, haptic, HID, sensor, camera, power, GPU,
offscreen or the render backends Reaktor never selects. Its tray is built: on
Linux and the BSDs it loads AppIndicator and GTK 3 when a tray opens, and
nothing links them. mojibake is built without its collation, IDNA, security
and character-name tables. NukAnim is one header, compiled in
`core/anim/anim.c`; its own Nuklear submodule, which only its tests use, comes
with a recursive checkout and is not built. kb_text_shape is one header, compiled in
`core/text/kb.c`; it shapes, and the Text module rasterizes with Nuklear's
stb_truetype.

[hamza](https://github.com/saidwho12/hamza) was the first choice of shaper
and is not used: its sources still carry the LGPL headers its MIT license file
replaced, its tables stop short of the Indic scripts, and MSVC cannot compile
it (an empty struct, GNU range designators).

## The one dependency that is not a submodule

`libdbus`, on Linux and the BSDs, when present: the AT-SPI bridge speaks
D-Bus. CMake asks pkg-config for `dbus-1`; without it the bridge is not built,
as on a desktop with no accessibility bus.

| Component | License |
| --- | --- |
| [libdbus](https://gitlab.freedesktop.org/dbus/dbus) | **AFL-2.1** or GPL-2.0-or-later — taken under AFL-2.1 |

AFL-2.1 is a permissive license with an attribution requirement, which this
table meets, and a patent-termination clause. It is not GPL-compatible, which
matters to a GPL downstream, not to an MIT project. The alternative was ATK:
LGPL, and being retired in favor of speaking AT-SPI directly.

## The one vendored source file

`core/render/nk_sdl3_renderer.h` is Nuklear's SDL3 backend, copied from
`external/nuklear/demo/sdl3_renderer/` under Nuklear's license. Every deviation
is marked `REAKTOR`: an 8-bit indexed atlas, a 1×1 white texture for
untextured geometry, pixel snapping, fills and strokes feathered apart, the
atlas palette shared with the Text module, rectangles drawn from device pixels
by `nk_sdl_quad` on the software renderer, `tex_null` repointed after every
bake, a key-up dropped for a key Nuklear does not hold, text input of any
length fed one rune at a time, and textures clamped rather than tested for
wrapping.

It cannot stay an include because the first change is inside
`nk_sdl_font_stash_end`, and submodules are not edited. The cost: when
Nuklear's backend changes, it is re-vendored and the hunks re-applied.

## Fonts

Fonts are the exception to the submodule rule, for a license's sake: the old
UI face, Karla, came from inside Nuklear with no license near it.

| Font | Role | License | Terms |
| --- | --- | --- | --- |
| **Aileron** | The UI face, Regular and Bold | CC0 1.0 | `assets/fonts/Aileron-Notice.txt` |
| **M PLUS 1p** | Japanese, for what Aileron lacks | OFL 1.1 | `assets/fonts/MPLUS1p-Notice.txt` |

Aileron is by Sora Sagano of DOT COLON. Its download has no license file, so
the terms were traced: the font's own `name` table says `copyright: No Rights
Reserved.`, and dotcolon.net/font/aileron links CC0 beside it.
`Aileron-Notice.txt` records both, says this project assembled it, and
reproduces the CC0 legal code. It is an OTF with CFF outlines, which
stb_truetype reads.

M PLUS 1p is by The M+ Project Authors. `MPLUS1p-Regular.ttf` is the file as
Google Fonts ships it; `MPLUS1p-Notice.txt` names its commit and SHA-256 and
reproduces the OFL, which allows bundling with software under another license
as long as the font is not sold alone. It has no Reserved Font Name. The
showcase opens it only for a character Aileron lacks.

## The texts on the Translations page

`assets/locale/` carries the opening of Hans Christian Andersen's *The Ugly
Duckling* (1843) in three translations, each published before 1931 by a
translator who died before 1956:

| Language | Translation | Translator died | Text from |
| --- | --- | --- | --- |
| English | Mrs. H. B. Paull, *Hans Andersen's Fairy Tales*, Frederick Warne and Co., 1888 | 1888 | Wikisource |
| Polish | Cecylia Niewiadomska, *Baśnie*, Gebethner i Wolff, Warsaw, 1899 | 1925 | Polish Wikisource, from the National Library's scan |
| Japanese | 菊池寛 (Kikuchi Kan), 小学生全集 第五巻『アンデルゼン童話集』, 興文社・文藝春秋社, 1928 | 1948 | Aozora Bunko, in modern spelling |

All three are public domain in the United States, in Japan, and wherever
protection ends seventy years after the translator's death. Mexico's hundred
years and Colombia's eighty still cover the Japanese one, and no Japanese
translation with checkable dates clears them. Each catalog repeats its
attribution, and the page prints it under the story.

## The mark

`assets/icons/` is original work under this project's MIT license, as SVG, PNG
and `.ico`. Its colors are USWDS system tokens, public domain as a U.S.
Government work.

## Benchmarks

Nothing in `benchmarks/` is listed above. Its four projects — Flutter,
Electron, Compose Multiplatform and Shaft — hold source and build files only;
each toolchain fetches its own framework, and none is part of a Reaktor build.

## What is redistributed

A native build links SDL, libcss, yutil, plutosvg and plutovg statically, plus
mojibake and kb_text_shape with the Text module, and compiles in Nuklear,
NukAnim and Onlay, so it carries the zlib and MIT terms above. Each license is copied
unchanged into `licenses/` beside the programs: ship that folder, and this
file, with a binary.

The binary also carries the default assets in `REAKTOR_BUILTINS`, byte for
byte: tiny.css's `core.css`, `variables-light.css` and `variables-dark.css`
(MIT); the seven Ionicons `chevron-down-outline`, `chevron-back-outline`,
`chevron-forward-outline`, `ellipse`, `ellipse-outline`, `radio-button-off`
and `close-outline` (MIT); Aileron Regular and Bold (CC0); and the mark and
`assets/rest/reaktor.css` (MIT). Their licenses are in `licenses/` too.

The rest of Ionicons, simple.css, the Japanese font and the catalogs are not
embedded (the catalogs are with `-DREAKTOR_LOCALE_EMBED=ON`); a build that uses
them ships them beside the binary, under the `.reaktor-root` marker. The
WebAssembly build packs the ones it uses into its `.data` bundle.
