# Curie

A desktop application in C where none of the styling is compiled in.

- **Nuklear** draws every widget and lays out every row.
- **libcss**, taken from LCUI, parses a stylesheet at runtime; its computed
  values are pushed into `nk_style` around each widget call.
- **SDL3** supplies the window, the input and the renderer.

The stylesheet is [tiny.css](https://github.com/ihsan6133/tiny.css), which is
classless and written for HTML. Curie reads the rules that have a counterpart
in Nuklear — `button`, `input`, `select`, `details`, `summary`, `dialog` — and
the palette in `:root`, and maps them onto the widget styles. Edit a colour in
`third_party/tinycss/src/`, restart, and the app follows: nothing under `src/`
holds a colour of its own.

The same sources build a native binary and a WebAssembly page.

## What is in it

Seven tabs. The first is an ordinary login form; the rest exist to show every
control Nuklear has, and what each one looks like once the stylesheet has been
applied to it.

| Tab | What it holds |
| --- | --- |
| Login | A form, with a themed card, field focus and a contact panel |
| Buttons | Labels, symbols, images, repeaters, checkboxes, radios, selectables |
| Inputs | Text fields, sliders, progress, knobs, colour picker, property steppers |
| Display | Labels, images, charts, tables, trees, list views |
| Layout | Static, dynamic and template rows, spaces, groups, splitters |
| Popups | Menus, context menus, combos, tooltips, modal dialogs |
| Diagnostics | What the renderer chose, where a frame goes, where the memory went |

At rest the app draws nothing at all and uses no measurable CPU: SDL is told
to wait for events, and a frame is built only when something has actually
changed.

## Building

The dependencies are submodules, so first:

```bash
git submodule update --init --recursive
```

### Windows

```powershell
.\build.ps1
```

Needs Visual Studio Build Tools with "Desktop development with C++". The
script locates MSVC, CMake and Ninja inside that install, so nothing has to be
on `PATH`.

### macOS, Linux, the BSDs

```bash
cmake -S . -B build && cmake --build build --parallel
```

### The web

```powershell
.\build-wasm.ps1
```

or, with an activated emsdk on any platform:

```bash
emcmake cmake -S . -B build-wasm && cmake --build build-wasm --parallel
```

A `file://` page cannot fetch the `.wasm`, so serve the output directory:
`python -m http.server -d build-wasm 8000`.

## Where things are

| Path | What it is |
| --- | --- |
| `src/main.c` | The shell: window, title bar, tab strip, and the CSS-to-`nk_style` mapping |
| `src/showcase.c` | The six showcase pages |
| `src/style.c` | libcss: parse the sheets, resolve a selector, hand back computed values |
| `src/cssflat.c` | Narrows CSS to what libcss implements, and resolves `var()` first |
| `src/appicon.c` | An Ionicons SVG, recoloured and rasterised at runtime |
| `src/metrics.c` | The display scale, in one place |
| `src/theme.c` | Which colour scheme the desktop is using |
| `src/util.c` | Paths, whole-file reads, the resident set |
| `tools/mkicon.c` | Bakes the window icon during the build |
| `assets/fonts/` | The UI typeface, vendored with its licence — the one exception to the submodule rule |

## Documentation

- [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) — how the pieces fit, and what to
  know before changing them
- [docs/PERFORMANCE.md](docs/PERFORMANCE.md) — size, memory, CPU, and what it
  needs to run
- [docs/NOTICE.md](docs/NOTICE.md) — every dependency and its licence
- [CONTRIBUTING.md](CONTRIBUTING.md)

## Licence

MIT — see [LICENSE](LICENSE). Third-party licences are listed in
[docs/NOTICE.md](docs/NOTICE.md).
