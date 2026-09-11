# Reaktor

A Nuklear-powered (GUI) engine for C.

![The Reaktor showcase, split along the diagonal: the same window in the light
scheme above the line and the dark scheme below it, showing a centered login
card, a tab strip across the top and a scheme switch beside the window
controls](assets/rest/screenshot.png)

Tired of C having no cross-platform GUI — nothing like Avalonia, Shaft or
Freya — and of leaning on toolkits that want hundreds of megabytes just to put
a dialog on screen? No? I was. So I wrote Reaktor: a complete GUI library,
kept as small as complete allows. A 2.5 MB binary, about 10 MB of RAM, and no
GPU required.

> ⚠️ **Reaktor is in active development**, and breaking changes might occur.

## What it is made of

- **[Nuklear](https://github.com/Immediate-Mode-UI/Nuklear)** draws the
  widgets. **SDL3** supplies the window, input and renderer. **libcss**, taken
  from LCUI, parses the stylesheet.
- **[Onlay](https://github.com/Alpaq92/Onlay)** computes the rectangles — a
  fork of randrew/layout with weighted tracks, gaps and padding.
- Every dependency is a submodule, read at runtime as it ships. No values are
  copied out of one and into this tree.
- For the complete list, check [docs/NOTICE.md](docs/NOTICE.md).

## Features

- **Widgets** — 15 declarative specs, with all of Nuklear underneath.
- **Styling from CSS** — colors, type, borders, radii, padding, margins,
  `:hover` / `:active`. Read at runtime; swap the sheet, the app changes.
- **Animation** — 31 easing curves, keyed per widget.
- **Accessibility** — automatic. UI Automation, NSAccessibility, AT-SPI, and a
  DOM subtree on the web. Keyboard focus and shortcuts included.
- **HiDPI** — layout in logical pixels, atlas baked at the display's size.
- **Light and dark** — follows the system, or pick one.
- **Windows, macOS, Linux, the BSDs and WebAssembly** — one set of sources, a
  software rasterizer, no GPU.
- **Nothing drawn at rest** — 0% of a core while it sits there.

Not yet: **localization**. The font atlas bakes Latin-1 only and there is no
text shaping or bidi, so anything past Western European scripts does not
render — see [docs/DOCUMENTATION.md](docs/DOCUMENTATION.md#localization).

## Build

Needs a C99 compiler, CMake and the submodules.

```bash
git clone --recursive https://github.com/Alpaq92/Reaktor
cd Reaktor
./build.sh          # build.ps1 on Windows
./build/showcase
```

For the browser, `./build-wasm.sh` (needs [emsdk](https://emscripten.org))
builds all three to WebAssembly, then serve `build-wasm/` over HTTP.

## Usage

```c
static char name[64];
static int  len;

REAKTOR_COLUMN(.gap = 10) {
    reaktor_label(&(reaktor_label_spec){ .text = "What is your name?" });

    reaktor_field(&(reaktor_field_spec){
        .buf = name, .len = &len, .cap = sizeof name, .hint = "Ada" });

    if (reaktor_button(&(reaktor_button_spec){ .label = "Say hello" }))
        printf("Hello, %s!\n", name);
}
```

There are no colors in that code, no corner radii, no padding, no font sizes.
They come from a stylesheet that is read when the program starts, so changing
the sheet changes the application. Screen readers are covered too, with
nothing written for them here — each widget reports its own name, value and
position.

## Documentation

- [docs/DOCUMENTATION.md](docs/DOCUMENTATION.md) — the whole of it: the
  declarative API, layout, styling, accessibility and animation, then the tree,
  the tests and the command-line flags.
- [docs/DEMO.md](docs/DEMO.md) — the four applications in this repository and
  what each one is for.
- [docs/PERFORMANCE.md](docs/PERFORMANCE.md) — what it costs, measured on four
  platforms.
- [docs/NOTICE.md](docs/NOTICE.md) — what it depends on, and under what
  license.

## License

MIT. The dependencies keep their own — see [docs/NOTICE.md](docs/NOTICE.md).
