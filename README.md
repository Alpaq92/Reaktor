# Reaktor

A C99 GUI library where the widgets are declared and the look comes from CSS.

![The Reaktor showcase, split along the diagonal: the same window in the light
scheme above the line and the dark scheme below it, showing a centred login
card, a tab strip across the top and a scheme switch beside the window
controls](assets/rest/screenshot.png)

```c
REAKTOR_COLUMN(.gap = 10) {
    REAKTOR_ROW(.gap = 6) {
        if (reaktor_button(&(reaktor_button_spec){ .label = "Save" }))
            save(&doc);
        if (reaktor_button(&(reaktor_button_spec){ .label = "Run" }))
            run(&doc);
    }
    reaktor_field(&(reaktor_field_spec){
        .buf = doc.body, .len = &doc.len, .cap = sizeof doc.body,
        .multiline = 1,
        .box = { .flags = REAKTOR_LAY_FILL_X | REAKTOR_LAY_FILL_Y } });
}
```

No colour, no radius, no padding and no font size appears there. Those live in
a stylesheet, parsed at runtime, and swapping the sheet changes the
application. There is no accessibility code there either: every declared
widget reports its own name, value and bounds.

## What it is made of

- **Nuklear** draws the widgets. **SDL3** supplies the window, input and
  renderer. **libcss**, taken from LCUI, parses the stylesheet.
- **[Onlay](https://github.com/Alpaq92/Onlay)** computes the rectangles — a
  fork of randrew/layout with weighted tracks, gaps and padding.
- Every dependency is a submodule, read at runtime as it ships. No values are
  copied out of one and into this tree.

## Three applications, one runtime

| | | |
| --- | --- | --- |
| `showcase` | 9 pages | every widget, both schemes, and how far the CSS seam reaches |
| `notepad` | ~400 lines | a text editor that opens, edits and saves a file |
| `simple` | 60 lines | one button that closes the window |

They share `runtime/` and `core/` byte for byte. An application is whatever
implements the six functions in [`samples/sample.h`](samples/sample.h) — and
`simple` writes one of them and returns a constant from the other five.

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

## Documentation

- [docs/DOCUMENTATION.md](docs/DOCUMENTATION.md) — the whole of it: the
  declarative API, layout, styling, accessibility and animation, then the tree,
  the tests and the environment variables.
- [docs/PERFORMANCE.md](docs/PERFORMANCE.md) — what it costs, measured on four
  platforms.
- [docs/NOTICE.md](docs/NOTICE.md) — what it depends on, and under what
  licence.

## Licence

MIT. The dependencies keep their own — see `docs/NOTICE.md`.
