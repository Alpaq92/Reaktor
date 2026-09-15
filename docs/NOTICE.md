# Third-party notices

Reaktor is MIT ([LICENSE](../LICENSE)). Everything it is built on is a git
submodule under `external/`, used as it ships. Each license below was read
from that project's own license file at the pinned revision, not from its
README. `git submodule status` prints the current pins.

| Component | Used for | License |
| --- | --- | --- |
| [SDL](https://github.com/libsdl-org/SDL) | Window, input, renderer, system theme, file dialog | zlib |
| [Nuklear](https://github.com/Immediate-Mode-UI/Nuklear) | Immediate-mode widgets, font baking, the SDL3 backend, and the stb_truetype the Text module rasterizes with | MIT **or** public domain, your choice |
| [LCUI](https://github.com/lc-soft/LCUI) | `libcss` (parse, match, cascade) and `yutil` | MIT |
| [Onlay](https://github.com/Alpaq92/Onlay) | Computing the rectangles — a fork of [randrew/layout](https://github.com/randrew/layout) | MIT |
| [tiny.css](https://github.com/ihsan6133/tiny.css) | The stylesheet the whole look comes from | MIT |
| [simple.css](https://github.com/kevquirk/simple.css) | The override sheet the Styling page swaps in | MIT |
| [Ionicons](https://github.com/ionic-team/ionicons) | Every icon, read as SVG at runtime | MIT |
| [plutosvg](https://github.com/sammycage/plutosvg) | Rasterizing those SVGs | MIT |
| [plutovg](https://github.com/sammycage/plutovg) | The canvas plutosvg draws on (nested submodule) | MIT |
| [mojibake](https://github.com/zaerl/mojibake) | Unicode's bidirectional algorithm (UAX #9) and line breaking rules (UAX #14), for the Text module | MIT |
| [kb_text_shape](https://github.com/JimmyLefevre/kb) | OpenType shaping, script segmentation and font coverage, for the Text module | zlib |

Only two of LCUI's libraries are built — `libcss` and `yutil` — not the
toolkit. SDL is built static, with audio, joystick, haptic, HID, sensor,
camera, power, GPU, tray and offscreen disabled, along with the render backends
this application can never select. mojibake is built with its own collation,
IDNA, security and character-name options off, since nothing here calls them.
kb_text_shape is a single header, compiled once in `core/text/kb.c`. It shapes
and does not rasterize, so the Text module draws its glyphs with the
stb_truetype Nuklear already compiles.

The shaper was to have been [hamza](https://github.com/saidwho12/hamza), which
is not used, so it has no row above. Its license file is MIT, but its sources
still carry the LGPL headers that file replaced; its feature tables stop at
Arabic, Buginese, Hangul and Hebrew, with none for the Indic scripts; and MSVC's
C compiler cannot build it, since it declares an empty struct and uses GNU's
range designators. kb_text_shape has none of those problems.

## The one dependency that is not a submodule

`libdbus`, on Linux and the BSDs, and only if it is there: the AT-SPI bridge
speaks D-Bus, and D-Bus on a freedesktop desktop is a system library every
application already links. CMake asks pkg-config for `dbus-1`; if it is missing
the bridge is not built and the app is not served — the same thing that happens
on a desktop with no accessibility bus running.

| Component | License |
| --- | --- |
| [libdbus](https://gitlab.freedesktop.org/dbus/dbus) | **AFL-2.1** or GPL-2.0-or-later, your choice — taken here under AFL-2.1 |

AFL-2.1 is an MIT-shaped permissive license with an attribution requirement,
which this table satisfies, and a patent-termination clause. It is not
GPL-compatible, which does not matter to an MIT project and would matter to a
GPL downstream. The alternative was ATK, which is LGPL and is being retired in
favour of speaking AT-SPI directly — so the license question and the
maintenance question pointed the same way.

## The one vendored source file

`core/render/nk_sdl3_renderer.h` is Nuklear's SDL3 backend, copied from
`external/nuklear/demo/sdl3_renderer/` and covered by Nuklear's license
above. Every deviation from upstream is marked `REAKTOR`: the atlas is baked
and uploaded 8-bit indexed rather than RGBA32; untextured geometry samples a
1×1 white texture of its own; vertices are put on the pixel grid;
`nk_sdl_render_ex` feathers fills and strokes independently; and the atlas's
palette is made public, for the Text module's glyph textures to share.

It could not stay an include because the first of those is chosen inside
`nk_sdl_font_stash_end`, and this project does not edit submodules. The cost is
that it no longer tracks upstream: when Nuklear's backend changes it has to be
re-vendored and the hunks re-applied.

## Fonts

Fonts are the one exception to the submodule rule, and the reason is a license.
The UI face used to be Karla, taken from inside the Nuklear submodule — which
ships that `.ttf` with no license anywhere near it, so its terms could only be
read off a different project. A dependency whose license has to be inferred is
not one to build a distributable on.

| Font | Role | License | Terms |
| --- | --- | --- | --- |
| **Aileron** | The UI face — Regular and Bold, each at eight sizes | CC0 1.0 | `assets/fonts/Aileron-Notice.txt` |
| **M PLUS 1p** | Japanese, for the characters Aileron lacks, at whatever size they are drawn | OFL 1.1 | `assets/fonts/MPLUS1p-Notice.txt` |

Aileron is by Sora Sagano of DOT COLON. Its download ships no license file
either, so the terms were traced to their source: the font's own `name` table
carries `copyright: No Rights Reserved.` beside the designer and foundry, and
dotcolon.net/font/aileron says the same next to a CC0 link. Metadata inside the
file the designer built is better provenance than a text file beside it, and
`Aileron-Notice.txt` records both, says plainly that this project assembled it,
and reproduces the CC0 legal code.

It is an OTF; Nuklear bakes through stb_truetype, which reads CFF outlines, so
the format costs nothing. Switching face is one line — `FONT_FILE` in
`core/render/draw.c`.

M PLUS 1p is by The M+ Project Authors. `MPLUS1p-Regular.ttf` is the file as
Google Fonts ships it, byte for byte, and `MPLUS1p-Notice.txt` names the commit
it came from and its SHA-256, then reproduces the Open Font License unchanged.
The OFL lets a font be bundled with software that is not itself under the OFL,
as long as the font is not sold on its own; it carries no Reserved Font Name.
The showcase hands it to the Text module, which opens it only once it needs a
character Aileron lacks — the Japanese catalog's; see
[the texts](#the-texts-on-the-translations-page).

## The texts on the Translations page

The showcase's catalogs, `assets/locale/`, carry the opening of Hans Christian
Andersen's *The Ugly Duckling* (1843) in three translations, chosen because
each was published before 1931 by a translator who died before 1956:

| Language | Translation | Translator died | Text from |
| --- | --- | --- | --- |
| English | Mrs. H. B. Paull, *Hans Andersen's Fairy Tales*, Frederick Warne and Co., 1888 | 1888 | Wikisource |
| Polish | Cecylia Niewiadomska, *Baśnie*, Gebethner i Wolff, Warsaw, 1899 | 1925 | Polish Wikisource, from the National Library's scan |
| Japanese | 菊池寛 (Kikuchi Kan), 小学生全集 第五巻『アンデルゼン童話集』, 興文社・文藝春秋社, 1928 | 1948 | Aozora Bunko, in modern spelling |

So all three are public domain in the United States, in every country that
counts seventy years from the translator's death, and in Japan. The few that
count longer - Mexico a hundred years, Colombia eighty - still protect the
Japanese one; no Japanese translation of an Andersen tale clears them with a
translator whose dates can be checked. Each catalog repeats its own
attribution, and the page prints it under the story.

## The mark

`assets/icons/` is original work under this project's MIT license: the icon in its
one ink, as SVG, PNG and `.ico`. Its colors are USWDS system tokens, which are
public domain as a U.S. Government work; the shapes are not taken from
anywhere.

Nothing in `benchmarks/` is on this list. Those four projects are the same
workload written in Flutter, Electron, Compose Multiplatform and Shaft, and
none of their frameworks is vendored here, linked into anything or shipped: the
directory holds source and build files, and each toolchain fetches its own when
asked to build one. Nothing in that directory is part of a Reaktor build.

## What is redistributed

A native build links SDL, libcss, yutil, plutosvg and plutovg statically, and
mojibake and kb_text_shape when it takes the Text module, and compiles Nuklear
and Onlay into the binary, so it carries the zlib and MIT terms above — all
satisfied by shipping this file with it.

The application does **not** embed the stylesheets, the icons, the mark, the
fonts or the catalogs — the catalogs only when built with
`-DREAKTOR_LOCALE_EMBED=ON`. It opens them at runtime from the submodule
checkout, resolved against the `.reaktor-root` marker, so a distributable build
ships those files beside the binary. The WebAssembly build is the exception:
Emscripten packages the files actually referenced into the `.data` bundle,
which is why it needs no checkout beside it.
