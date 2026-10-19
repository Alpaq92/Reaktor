# Frequently asked questions

> Short answers, each linking to the part of
> [DOCUMENTATION.md](DOCUMENTATION.md) that covers it in full.
> Something missing? [Open an issue](https://github.com/Alpaq92/Reaktor/issues).

**What and why**

- [What is Reaktor, and how is it different from Nuklear?](#what-is-reaktor-and-how-is-it-different-from-nuklear)
- [Why C99 and an immediate-mode core instead of a retained toolkit like GTK or Qt?](#why-c99-and-an-immediate-mode-core-instead-of-a-retained-toolkit-like-gtk-or-qt)
- [Which platforms and architectures are supported?](#which-platforms-and-architectures-are-supported)
- [What license is it under?](#what-license-is-it-under)

**Styling**

- [Why doesn't my CSS rule change a widget's size?](#why-doesnt-my-css-rule-change-a-widgets-size)
- [Why can't I use `em` or `rem`, and how far do `var()` and `color-mix()` go?](#why-cant-i-use-em-or-rem-and-how-far-do-var-and-color-mix-go)
- [How do I ship my own stylesheet or theme?](#how-do-i-ship-my-own-stylesheet-or-theme)

**Layout and widgets**

- [Why does a widget have no rectangle, or the declared tree not settle?](#why-does-a-widget-have-no-rectangle-or-the-declared-tree-not-settle)
- [Can I still call Nuklear directly?](#can-i-still-call-nuklear-directly)
- [How do I write a widget of my own?](#how-do-i-write-a-widget-of-my-own)

**Apps**

- [Where are settings stored, and when are they saved?](#where-are-settings-stored-and-when-are-they-saved)
- [How do settings work on the web?](#how-do-settings-work-on-the-web)
- [How do I make a splash screen, a frameless window or a tray icon?](#how-do-i-make-a-splash-screen-a-frameless-window-or-a-tray-icon)
- [How do I open files, ask a question or post work from another thread?](#how-do-i-open-files-ask-a-question-or-post-work-from-another-thread)
- [How do I add undo, notifications or printing?](#how-do-i-add-undo-notifications-or-printing)

**Build and ship**

- [How do I build without CMake on PATH, or for x86 or ARM64?](#how-do-i-build-without-cmake-on-path-or-for-x86-or-arm64)
- [How do I turn off accessibility, localization or text shaping?](#how-do-i-turn-off-accessibility-localization-or-text-shaping)
- [How do I install libreaktor and link against it?](#how-do-i-install-libreaktor-and-link-against-it)
- [How do I build and host the WebAssembly version?](#how-do-i-build-and-host-the-webassembly-version)
- [Why is the web page blank in a background tab?](#why-is-the-web-page-blank-in-a-background-tab)

**Debugging**

- [How do I capture a screenshot or accessibility dump for a bug report?](#how-do-i-capture-a-screenshot-or-accessibility-dump-for-a-bug-report)
- [Why is the frame time high on a machine with no GPU?](#why-is-the-frame-time-high-on-a-machine-with-no-gpu)
- [Why does Japanese or Arabic text draw differently from Latin?](#why-does-japanese-or-arabic-text-draw-differently-from-latin)
- [How do I report a bug?](#how-do-i-report-a-bug)

---

## What and why

### What is Reaktor, and how is it different from Nuklear?

Reaktor is a toolkit for building desktop and web apps in C. Nuklear, which it
is built on, gives you widgets and leaves everything else to
you. Reaktor fills in the rest:

- **The whole stack, ready to use.** Nuklear only draws; the window, input,
  fonts and rendering are yours to wire up. Reaktor brings them, built on
  SDL, so there is nothing to connect yourself.
- **A finished app, not a drawing kit.** One call opens a window and runs
  it; you describe what's on screen and Reaktor lays it out.
- **A look you change without recompiling.** Colors, fonts, spacing and
  rounded corners come from ordinary CSS files, with light and dark themes.
- **Everyone can use it.** Screen readers can read it, and every control
  works from the keyboard.
- **Any language.** Translations, number and date formats, and scripts such
  as Japanese and Arabic.
- **What real apps need.** Menus, file pickers, dialogs, notifications,
  undo, printing, saved settings, extra windows and a tray icon.
- **One codebase everywhere.** Windows, macOS, Linux, the BSDs and the
  browser.

Nuklear is still there underneath when you need something it offers
directly. See [An application](DOCUMENTATION.md#an-application).

### Why C99 and an immediate-mode core instead of a retained toolkit like GTK or Qt?

To stay small: a 2.3 MB binary, about 10 MB of RAM, no GPU, and nothing to
install beside it. A frame is drawn only when something changes, so the app
idles at 0% of a core. See [The frame loop](DOCUMENTATION.md#the-frame-loop)
and [PERFORMANCE.md](PERFORMANCE.md).

### Which platforms and architectures are supported?

Windows, macOS, Linux, FreeBSD, OpenBSD, NetBSD and WebAssembly, on x86 and
ARM, plus ARMv7 and RISC-V on Linux. Every pull request builds them all; the
BSDs' ARM64 runs under emulation and may fail without failing the run. See
[Platforms](DOCUMENTATION.md#platforms).

### What license is it under?

MIT. Each dependency keeps its own license, all permissive; a release ships
them in `licenses/` with [NOTICE.md](NOTICE.md), and your app should carry
them too.

---

## Styling

### Why doesn't my CSS rule change a widget's size?

A size set in code beats the sheet: `.box = { .h = N }` locks CSS out of that
axis. Leave the axis unset and the rule applies. See
[Styling](DOCUMENTATION.md#styling).

### Why can't I use `em` or `rem`, and how far do `var()` and `color-mix()` go?

You can: the engine underneath, libcss, has neither, so `cssflat.c` turns
them into pixels before it reads the sheet. It also resolves `var()`, works
out `color-mix(in srgb, …)` and `calc()` over pixels, and keeps the
`prefers-color-scheme` branch for the active scheme. Operator selectors such
as `[href^="http"]` are dropped. See [Styling](DOCUMENTATION.md#styling).

### How do I ship my own stylesheet or theme?

Pass up to four sheets in `.css`; they are read after Water.css and
reaktor.css, and the last rule wins. `reaktor_set_css` swaps them at run time,
and `--css` adds one on top. `.no_reaktor_css` drops reaktor.css. See
[Services](DOCUMENTATION.md#services) and [Styling](DOCUMENTATION.md#styling).

---

## Layout and widgets

### Why does a widget have no rectangle, or the declared tree not settle?

A box is placed one frame late and found again by its name. A widget whose
label changes every frame needs a stable `.name`, and a node declared only on
some frames renumbers everything after it. A loose label after a row belongs
inside a named `REAKTOR_COLUMN`. See [Layout](DOCUMENTATION.md#layout).

### Can I still call Nuklear directly?

Yes. Every Nuklear type and call has a `reaktor_` alias, and `nk_*` stays
available. A declared tree and hand-laid `reaktor_layout_row_*` cannot share
a panel, but a group or popup is its own panel. See
[Beyond the specs](DOCUMENTATION.md#beyond-the-specs).

### How do I write a widget of my own?

Lay it out by hand in a group, from the pieces in `ui.h`: colors from the
sheet, rounded fills and borders, a hot rectangle so hover redraws, and a
`reaktor_note` so screen readers and keyboard focus see it. See
[Beyond the specs](DOCUMENTATION.md#beyond-the-specs).

---

## Apps

### Where are settings stored, and when are they saved?

In `settings.conf`, an INI file in SDL's preference folder for the app.
`.save_settings` picks when: never (the default), on quit, on every change,
or `.save_ms` after a change. `reaktor_settings_save` writes now and
`reaktor_settings_clear` empties them and deletes the file. See
[Services](DOCUMENTATION.md#services).

### How do settings work on the web?

The same `settings.conf` lives in the browser's IndexedDB, loaded before
`main` and synced after each write; saving on quit means saving on change
there. Clearing site data or closing a private window loses it.

### How do I make a splash screen, a frameless window or a tray icon?

A window takes `.borderless`, `.centered` and `.skip_taskbar`; a splash
screen is those three on a window opened beside a `.hidden` main window.
`reaktor_tray_open` adds a tray icon with a menu. See
[Windows](DOCUMENTATION.md#windows) and [Tray](DOCUMENTATION.md#tray).

### How do I open files, ask a question or post work from another thread?

`reaktor_file_dialog_open` for the system's open, save and folder pickers,
`reaktor_ask` for a modal question, `reaktor_timer` for later, and
`reaktor_post` from any thread. Every callback runs on the main thread and is
followed by a frame. See [Services](DOCUMENTATION.md#services).

### How do I add undo, notifications or printing?

Push a step with an undo and a redo callback after each change, and Ctrl+Z
and Ctrl+Y take it back and forth, merging typing into one step if asked.
`reaktor_notify` shows a message outside the window. `reaktor_print` and
`reaktor_print_pdf` lay pages out as the window does and print them or write
a PDF. See [Services](DOCUMENTATION.md#services).

---

## Build and ship

### How do I build without CMake on PATH, or for x86 or ARM64?

`./build.ps1` on Windows finds MSVC's CMake; `./build.sh` elsewhere.
`./build.ps1 -Arch x86` or `-Arch arm64` builds that architecture into
`build-<arch>`; ARM64 needs Visual Studio's ARM64 tools. See
[Platforms](DOCUMENTATION.md#platforms).

### How do I turn off accessibility, localization or text shaping?

`-DREAKTOR_A11Y=OFF`, `-DREAKTOR_LOCALE=OFF` or `-DREAKTOR_TEXT=OFF`. Each
swaps in a stub, and pixels and `--a11y-dump` stay identical apart from what
the module drew. CMake keeps the option until you pass `=ON`. See
[Build options](DOCUMENTATION.md#build-options).

### How do I install libreaktor and link against it?

Take the release's archive or the `.deb` or `.rpm`, or run
`sh tools/libreaktor-pkg.sh -i`. Then `find_package(reaktor 0.7 REQUIRED)`
and link `reaktor::reaktor`, or use `pkg-config --cflags --libs reaktor`. To
build it into your tree instead, `add_subdirectory` the checkout and call
`reaktor_add_app`. See [In your own project](DOCUMENTATION.md#in-your-own-project).

### How do I build and host the WebAssembly version?

`./build-wasm.sh` (or `.ps1`) with emsdk writes the pages into `build-wasm/`;
serve that folder over HTTP, since opening the file directly will not load.
See [The web build](DOCUMENTATION.md#the-web-build).

### Why is the web page blank in a background tab?

The loop waits on `requestAnimationFrame`, which a hidden tab, a minimized
window or an off-screen pane never fires. It draws once it is visible.

---

## Debugging

### How do I capture a screenshot or accessibility dump for a bug report?

```bash
./build/showcase --tab 7 --theme dark --shot a.bmp --a11y-dump a.txt
```

Any app takes `--shot` and `--a11y-dump`; the dump lists every node with its
bounds, so two dumps diff to the pixel. Settings are not read under either.
See [Command-line flags](DOCUMENTATION.md#command-line-flags).

### Why is the frame time high on a machine with no GPU?

Most of it is usually `SDL_RenderPresent`, copying the frame to the screen,
not drawing it. The Diagnostics page and `bench --no-vsync` split build,
render and present. See [The renderer](DOCUMENTATION.md#the-renderer).

### Why does Japanese or Arabic text draw differently from Latin?

A string with a character the font atlas lacks is drawn by the Text module,
which orders, shapes and takes glyphs from fallback fonts; the rest is
Nuklear's. Arabic, Hebrew or an Indic script needs a font that has it, passed
with `--font-fallback` or `.font_fallbacks`. See [Text](DOCUMENTATION.md#text).

### How do I report a bug?

Open an issue at [github.com/Alpaq92/Reaktor/issues](https://github.com/Alpaq92/Reaktor/issues)
with the Reaktor version (`reaktor_version()`), the system and architecture,
the steps that show it, and what you expected. For anything drawn wrong,
attach a `--shot` and an `--a11y-dump` of it; for a crash, what the
`--console debug` log printed.
