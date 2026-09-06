# Third-party notices

Curie itself is MIT (see [LICENSE](../LICENSE)). Everything it is built on is
a git submodule under `third_party/`, used as it ships. Each licence below was
read from that project's own licence file in the pinned revision, not from its
README or its website.

Refresh the pinned revisions with `git submodule status`.

| Component | Used for | Licence | Pinned at |
| --- | --- | --- | --- |
| [SDL](https://github.com/libsdl-org/SDL) | Window, input, renderer, system theme, file dialog | zlib | `release-3.4.16` |
| [Nuklear](https://github.com/Immediate-Mode-UI/Nuklear) | Immediate-mode widgets, layout, font baking, SDL3 backend | MIT **or** public domain (Unlicense), your choice | `master` |
| [LCUI](https://github.com/lc-soft/LCUI) | `libcss` (parser, selector matching, cascade) and `yutil` (containers, strings) | MIT | `develop` |
| [tiny.css](https://github.com/ihsan6133/tiny.css) | The stylesheet the whole look comes from | MIT | `main` |
| [Ionicons](https://github.com/ionic-team/ionicons) | Every icon in the app, read as SVG at runtime | MIT | `v8.1.0` |
| [plutosvg](https://github.com/sammycage/plutosvg) | Rasterising those SVGs | MIT | `v0.0.8` |
| [plutovg](https://github.com/sammycage/plutovg) | The 2D canvas plutosvg draws on (nested submodule) | MIT | tracked by plutosvg |

Only two of LCUI's libraries are built — `libcss` and `yutil` — not the
toolkit. SDL is built static, with the audio, joystick, haptic, HID, sensor,
camera, power, GPU, tray and offscreen subsystems disabled, along with the
render backends this application can never select.

## Fonts

Fonts are the one exception to this project's submodule rule: the faces below are
vendored as files under `assets/fonts/`. That is deliberate. The UI face used to be
Karla, taken from `third_party/nuklear/extra_font/` — and the Nuklear submodule
ships that `.ttf` with no licence anywhere near it, so its terms could only be read
off a different project. A dependency whose licence has to be inferred is not one
to build a distributable on.

| Font | Role | Licence | Where the terms are |
| --- | --- | --- | --- |
| **Aileron** | **The UI face.** Regular is baked; Bold is vendored but not baked. | CC0 1.0 | `assets/fonts/Aileron-Notice.txt` |
| Zerove | Not loaded. Unicase — `a` and `A` are the same outline at 1434 units — so a wordmark face, not a UI one. | CC0 1.0 | `assets/fonts/Zerove-License.txt` |

Aileron is by Sora Sagano of DOT COLON. Its download ships no licence file either,
so rather than repeat Karla's problem the terms were traced to their source and
written down: the font's own `name` table carries `copyright: No Rights Reserved.`
alongside the designer and foundry, and dotcolon.net/font/aileron states the same
beside a CC0 link. Metadata inside the file the designer built is better provenance
than a text file next to it, and `Aileron-Notice.txt` records both, says plainly
that the project assembled it, and reproduces the CC0 legal code it refers to.

Aileron is an OTF. Nuklear bakes through stb_truetype, which reads CFF outlines as
well as TrueType ones, so the format costs nothing. Switching face is one line —
`FONT_FILE` in `src/main.c`.

Seven other faces were built and looked at before this one: Jupiteroid, Liber
Struct, Vegur, Tenderness, Seshat and Medio (all CC0), plus Karla, Public Sans and
IBM Plex Sans (all OFL). None is in the tree — everything shipped here is CC0.

## What is redistributed

A native build links SDL, libcss, yutil, plutosvg and plutovg statically, and
compiles Nuklear into the binary. That binary therefore carries the zlib and
MIT terms above, all of which are satisfied by shipping this file with it.

The application does **not** embed the stylesheet, the icons or the font: it
opens them at runtime from the submodule checkout, resolved relative to the
`.curie-root` marker. A distributable build has to ship those files alongside
the binary. The WebAssembly build is the exception — Emscripten packages the
handful of files actually referenced into `curie.data`, which is why that
bundle needs no checkout beside it.
