# The three applications

This repository builds three programs. They share `runtime/` and `core/` byte
for byte — the whole of the difference between them is in `samples/`.

| | Size | What it is for |
| --- | --- | --- |
| `showcase` | ~2,900 lines | Every widget, both schemes, and how far the CSS seam reaches |
| `notepad` | ~290 lines | A text editor that opens, edits and saves a file |
| `simple` | ~60 lines | One button that closes the window |

```bash
./build/showcase
./build/notepad somefile.txt
./build/simple
```

## What an application is

Whatever implements the six functions in
[`samples/sample.h`](../samples/sample.h). The runtime owns the window, the
event loop, the stylesheets, the font atlas and the accessibility tree; the
application owns what is drawn and what the keys mean.

`simple` writes one of the six and returns a constant from the other five,
which is the honest measure of how much of an application the runtime is
already doing.

## showcase

Nine pages — Login, Buttons, Inputs, Display, Layout, Popups, Animation,
Styling, Diagnostics. It is the reference for what the library can draw, and
it is also where the project's claims are tested rather than asserted:

- **Styling** reads simple.css over the top of tiny.css at the flick of a
  checkbox, and prints what each rule resolved to beside the widgets it
  painted — so the page cannot say one thing while the screen shows another.
- **Diagnostics** reports the renderer, where each frame's milliseconds went
  and where each megabyte went, live.
- **Animation** draws all 31 easing curves from the same function the widgets
  animate through.

It draws its own titlebar, which is why it carries a tab strip and window
controls that the other two do not.

## notepad

A real text editor in one file: a menu bar, a multiline field, a status line,
open and save through the platform's own file dialog, and five keyboard
shortcuts. It exists because a showcase can hide a lot — a page of widgets
never has to answer what happens when a document is modified, or where the
menu's last item lands when the popup is a pixel too short.

## simple

One centered button that closes the window, in sixty lines including the
five functions it does not use. It is the smallest thing that is still an
application, and it is what to copy when starting one.

## Running one the same way twice

Both of the verification hooks are command-line flags — Reaktor reads no
environment variables:

```bash
./build/showcase --tab 7 --theme dark --shot styling.bmp --a11y-dump styling.txt
```

`--shot` writes the window once the frame settles and quits; `--a11y-dump`
writes the accessibility tree, one line per node with its role, name, value,
state and rectangle. Diff two of those and a change that moved something by a
pixel says so. See [DOCUMENTATION.md](DOCUMENTATION.md#command-line-flags).
