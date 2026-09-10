# Third-party notices

Reaktor is MIT ([LICENSE](../LICENSE)). Everything it is built on is a git
submodule under `third_party/`, used as it ships. Each licence below was read
from that project's own licence file at the pinned revision, not from its
README. `git submodule status` prints the current pins.

| Component | Used for | Licence |
| --- | --- | --- |
| [SDL](https://github.com/libsdl-org/SDL) | Window, input, renderer, system theme, file dialog | zlib |
| [Nuklear](https://github.com/Immediate-Mode-UI/Nuklear) | Immediate-mode widgets, font baking, the SDL3 backend | MIT **or** public domain, your choice |
| [LCUI](https://github.com/lc-soft/LCUI) | `libcss` (parse, match, cascade) and `yutil` | MIT |
| [Onlay](https://github.com/Alpaq92/Onlay) | Computing the rectangles — a fork of [randrew/layout](https://github.com/randrew/layout) | MIT |
| [tiny.css](https://github.com/ihsan6133/tiny.css) | The stylesheet the whole look comes from | MIT |
| [simple.css](https://github.com/kevquirk/simple.css) | The override sheet the Styling page swaps in | MIT |
| [Ionicons](https://github.com/ionic-team/ionicons) | Every icon, read as SVG at runtime | MIT |
| [plutosvg](https://github.com/sammycage/plutosvg) | Rasterising those SVGs | MIT |
| [plutovg](https://github.com/sammycage/plutovg) | The canvas plutosvg draws on (nested submodule) | MIT |

Only two of LCUI's libraries are built — `libcss` and `yutil` — not the
toolkit. SDL is built static, with audio, joystick, haptic, HID, sensor,
camera, power, GPU, tray and offscreen disabled, along with the render backends
this application can never select.

## The one dependency that is not a submodule

`libdbus`, on Linux and the BSDs, and only if it is there: the AT-SPI bridge
speaks D-Bus, and D-Bus on a freedesktop desktop is a system library every
application already links. CMake asks pkg-config for `dbus-1`; if it is missing
the bridge is not built and the app is not served — the same thing that happens
on a desktop with no accessibility bus running.

| Component | Licence |
| --- | --- |
| [libdbus](https://gitlab.freedesktop.org/dbus/dbus) | **AFL-2.1** or GPL-2.0-or-later, your choice — taken here under AFL-2.1 |

AFL-2.1 is an MIT-shaped permissive licence with an attribution requirement,
which this table satisfies, and a patent-termination clause. It is not
GPL-compatible, which does not matter to an MIT project and would matter to a
GPL downstream. The alternative was ATK, which is LGPL and is being retired in
favour of speaking AT-SPI directly — so the licence question and the
maintenance question pointed the same way.

## The one vendored source file

`core/render/nk_sdl3_renderer.h` is Nuklear's SDL3 backend, copied from
`third_party/nuklear/demo/sdl3_renderer/` and covered by Nuklear's licence
above. Every deviation from upstream is marked `REAKTOR`: the atlas is baked
and uploaded 8-bit indexed rather than RGBA32; untextured geometry samples a
1×1 white texture of its own; vertices are put on the pixel grid; and
`nk_sdl_render_ex` feathers fills and strokes independently.

It could not stay an include because the first of those is chosen inside
`nk_sdl_font_stash_end`, and this project does not edit submodules. The cost is
that it no longer tracks upstream: when Nuklear's backend changes it has to be
re-vendored and the hunks re-applied.

## Fonts

Fonts are the one exception to the submodule rule, and the reason is a licence.
The UI face used to be Karla, taken from inside the Nuklear submodule — which
ships that `.ttf` with no licence anywhere near it, so its terms could only be
read off a different project. A dependency whose licence has to be inferred is
not one to build a distributable on.

| Font | Role | Licence | Terms |
| --- | --- | --- | --- |
| **Aileron** | The UI face — Regular at five sizes, Bold for the title | CC0 1.0 | `assets/fonts/Aileron-Notice.txt` |

Aileron is by Sora Sagano of DOT COLON. Its download ships no licence file
either, so the terms were traced to their source: the font's own `name` table
carries `copyright: No Rights Reserved.` beside the designer and foundry, and
dotcolon.net/font/aileron says the same next to a CC0 link. Metadata inside the
file the designer built is better provenance than a text file beside it, and
`Aileron-Notice.txt` records both, says plainly that this project assembled it,
and reproduces the CC0 legal code.

It is an OTF; Nuklear bakes through stb_truetype, which reads CFF outlines, so
the format costs nothing. Switching face is one line — `FONT_FILE` in
`core/render/draw.c`.

## The mark

`assets/icons/` is original work under this project's MIT licence: the icon in its
one ink, as SVG, PNG and `.ico`. Its colours are USWDS system tokens, which are
public domain as a U.S. Government work; the shapes are not taken from
anywhere.

## What is redistributed

A native build links SDL, libcss, yutil, plutosvg and plutovg statically and
compiles Nuklear and Onlay into the binary, so it carries the zlib and MIT
terms above — all satisfied by shipping this file with it.

The application does **not** embed the stylesheets, the icons, the mark or the
font. It opens them at runtime from the submodule checkout, resolved against
the `.reaktor-root` marker, so a distributable build ships those files beside
the binary. The WebAssembly build is the exception: Emscripten packages the
files actually referenced into the `.data` bundle, which is why it needs no
checkout beside it.
