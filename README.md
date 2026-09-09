# Reaktor - a Nuklear-powered (graphical) engine

A desktop application in C where none of the styling is compiled in.

![The Reaktor login screen, split along the diagonal: the same window in the
light scheme above the line and the dark scheme below it, showing a centred
card, a tab strip across the top and a scheme switch beside the window
controls](branding/screenshot.png)

- **Nuklear** draws every widget and lays out every row.
- **libcss**, taken from LCUI, parses a stylesheet at runtime; its computed
  values are pushed into `nk_style` around each widget call.
- **SDL3** supplies the window, the input and the renderer.

The stylesheet is [tiny.css](https://github.com/ihsan6133/tiny.css), which is
classless and written for HTML. Reaktor reads the rules that have a counterpart
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
./build.sh
```

The counterpart of `build.ps1`: it checks the submodules are there, prefers
Ninja when it is installed, and hands everything else to CMake. `--target NAME`
builds one target, `--debug` switches the build type. Or drive CMake yourself:

```bash
cmake -S . -B build && cmake --build build --parallel
```

On macOS the build also assembles `build/reaktor.app` around the plain
binary — same executable file, plus an `Info.plist`, the assets under
`Contents/Resources`, and a real `reaktor.icns` rasterised from the brand
PNG at ten sizes. Both work: `./build/reaktor` from a shell and
`open build/reaktor.app` from Finder. The bundle is self-contained and
can be moved to `/Applications`.

If the build fails against `MacOSX12.1.sdk` with a `NSBundle.h` error, the
default CommandLineTools SDK on that machine is newer than the compiler
it ships with. Configure once with `-DCMAKE_OSX_SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX11.3.sdk`
and the fix sticks. Full context lives in [docs/PERFORMANCE.md](docs/PERFORMANCE.md).

### The web

```powershell
.\build-wasm.ps1
```

```bash
./build-wasm.sh
```

The shell script looks for emsdk in `$EMSDK`, then beside an `emcc` already on
`PATH`, then in `~/emsdk`. `--serve` builds and then serves the result, which
is worth knowing because a `file://` page cannot fetch the `.wasm`:

```bash
python3 -m http.server -d build-wasm 8000
```

Or, with an activated emsdk on any platform:

```bash
emcmake cmake -S . -B build-wasm && cmake --build build-wasm --parallel
```

## Where things are

| Path | What it is |
| --- | --- |
| `src/main.c` | The shell: window, title bar, tab strip, and the CSS-to-`nk_style` mapping |
| `src/showcase.c` | The six showcase pages |
| `src/style.c` | libcss: parse the sheets, resolve a selector, hand back computed values |
| `src/cssflat.c` | Narrows CSS to what libcss implements, and resolves `var()` first |
| `src/appicon.c` | An Ionicons SVG, recoloured and rasterised at runtime |
| `src/nk_sdl3_renderer.h` | Nuklear's SDL3 backend, vendored: 8-bit atlas, glyphs on the pixel grid, fills and strokes feathered separately — see [DEVELOPMENT.md](docs/DEVELOPMENT.md) |
| `src/metrics.c` | The display scale, in one place |
| `src/theme.c` | Which colour scheme the desktop is using |
| `src/util.c` | Paths, whole-file reads, the resident set |
| `build.sh` / `build-wasm.sh` | The Unix side of `build.ps1` and `build-wasm.ps1`; all the build logic is in `CMakeLists.txt` either way |
| `tools/Info.plist.in` / `tools/make-bundle.sh` | The `.app` bundle: the `Info.plist` template, and the POST_BUILD step that copies the binary, the assets and a generated `.icns` around it |
| `branding/` | The project's mark, as SVG, PNG and `.ico` |
| `assets/fonts/` | Aileron, the CC0 typeface the UI is set in — vendored rather than submoduled, the one exception to the rule |

## Documentation

- [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) — how the pieces fit, and what to
  know before changing them
- [docs/PERFORMANCE.md](docs/PERFORMANCE.md) — size, memory, CPU, and what it
  needs to run
- [docs/NOTICE.md](docs/NOTICE.md) — every dependency and its licence
- [docs/ACCESSIBILITY.md](docs/ACCESSIBILITY.md) — what a screen reader gets
  today, and the plan for the rest
- [CONTRIBUTING.md](CONTRIBUTING.md)

## Licence

MIT — see [LICENSE](LICENSE). Third-party licences are listed in
[docs/NOTICE.md](docs/NOTICE.md).
