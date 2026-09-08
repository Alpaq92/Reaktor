# Accessibility

**Today:** the whole app works from the keyboard. Tab and Shift-Tab walk every
focusable widget in reading order, the arrows move among siblings, Home and End
jump, Enter and Space press, and a ring shows where focus is. The tab strip
also answers Ctrl-Tab, Ctrl-Shift-Tab and Ctrl-1..7, and F1 opens Diagnostics.
**In a browser, a screen reader gets all of it**: the tree is mirrored into
hidden DOM beside the canvas, with ARIA roles, names, states and positions, and
the browser exposes that to every reader on every platform. **On Windows and on
the free desktops a reader gets it natively**, through UI Automation and through
AT-SPI; macOS is written and has never run.

**Phases 1, 2, 3, 4a, 4b and 4c are done.** The model exists, every widget
reports into it, focus lives in it, the web serves it, and two native bridges
serve it; 4d and 5 are ahead.

## Why it is not a small change

A screen reader does not read pixels. It reads an *accessibility tree* through
the platform's API — UI Automation on Windows, AT-SPI on Linux, NSAccessibility
on macOS, the DOM on the web — and asks that tree questions: what are your
children, what is your role, what is your name, are you checked, where are your
bounds, invoke yourself.

Every one of those questions assumes objects that persist between queries. An
immediate-mode UI has none. There are no widget instances, only calls that emit
draw commands and return a result; the button drawn last frame has no identity
this frame, and between two frames nothing exists to answer a question about.
Nuklear also has no focus model to borrow — `NK_KEY_TAB` inserts a tab
character, it does not move focus — so there is no notion of "the focused
widget" to expose or to move.

The gap is therefore not a missing call. It is a missing model.

## The design: a shadow tree

Build a retained model *from* the immediate-mode frame, and serve the platform
from the model rather than from Nuklear.

Every widget helper reports itself as it is drawn. The reports accumulate into a
flat array in draw order, which is also reading order. At end of frame that
array is a complete description of the screen: roles, names, values, bounds,
states, parents. Diff it against the previous frame to get the change events the
platform APIs want, then swap.

The immediate-mode loop stays immediate. The tree is a by-product of it.

```c
typedef struct reaktor_a11y_node {
    unsigned  id;         /* stable across frames - see below */
    unsigned  parent;
    unsigned char role;   /* BUTTON, CHECKBOX, TAB, TABLIST, EDIT, ... */
    unsigned char state;  /* FOCUSED | CHECKED | EXPANDED | DISABLED | ... */
    const char *name;     /* interned; the label a reader speaks */
    const char *value;    /* field contents, slider value as text */
    struct nk_rect bounds;   /* window coordinates, post render scale */
} reaktor_a11y_node;
```

**Identity is the one hard part.** An id must name the same button on
consecutive frames, or the reader hears the whole screen change every frame.
Derive it the way Nuklear derives its own widget state: hash of the enclosing
container's id, the widget's label, and an index for unlabelled or repeated
widgets. That makes the id a function of position-in-structure rather than of
call order, so adding a widget above does not rename everything below it.

## Phase 1 — the model — **done**

Platform-independent, and testable on its own.

- `src/a11y.c` / `a11y.h`: two `reaktor_a11y_node` arenas, front and back, sized
  once (the busiest page draws on the order of a hundred widgets; 512 is
  generous). Names interned into a per-frame string arena so the tree owns no
  heap churn.
- `reaktor_a11y_begin()` / `reaktor_a11y_node()` / `reaktor_a11y_push()` /
  `reaktor_a11y_pop()` / `reaktor_a11y_end()`. Push and pop maintain the parent
  stack for containers.
- `reaktor_a11y_end()` diffs back against front and produces a change list:
  added, removed, name/value changed, state changed, focus moved. Swap.
- Cost control: skip the diff entirely when the frame was not dirty, which is
  most frames — see the frame loop in [DEVELOPMENT.md](DEVELOPMENT.md).

Verification is `tools/a11ytest.c`, built by default and run by hand. It asserts
the dump format, that an identical frame produces **zero** changes, that
switching a tab is two state changes rather than a removal and an addition, that
inserting a node above three others leaves their ids alone, and that overflow is
counted and survived rather than trapped. The id scheme was deliberately broken
once to confirm the test fails when it should.

## Phase 2 — instrumentation — **done**

The mechanical bulk of the work, and the part that had to be got right once.

The pages already funnel some widgets through shell helpers — `reaktor_button`,
`reaktor_button_accent`, `reaktor_button_icon`, `reaktor_field` — and
those are one added call each. The rest of `src/showcase.c` calls Nuklear
directly: roughly **190 distinct `nk_*` entry points**, of which about 60 call
sites are interactive widgets (14 `nk_button_label`, 11 `nk_group_begin`, 7
`nk_checkbox_label_align`, 5 each of `nk_edit_string`, `nk_edit_buffer`,
`nk_property_float`, `nk_progress`, and so on down a long tail).

Two ways to cover them, and the choice matters:

1. **Wrap each in a `reaktor_*` helper** that draws and reports. Verbose, but the
   report cannot be forgotten, and the showcase is already half-way there.
2. **Report inline at each call site.** Less code to add, but a new widget added
   later is silently absent from the tree.

Both were taken, and the seam turned out to already exist. `showcase.c` had a
`hot()` helper called before every widget drawn straight from Nuklear, to
register the pointer cursor — 34 sites, always immediately before the widget. Its
signature now carries role, name and state as well, so one call does both: a
widget worth a cursor is worth a name, and they cannot drift apart. The shell's
own `css_button`, `css_button_accent`, `css_button_icon`, `text_link`,
`reaktor_field` and `css_field` report from inside, so every page gets it free.
Static prose goes through `heading`, `caption` and `api`, which report inline.

Two things came out of doing it that the plan had not foreseen:

- **`nk_window_get_bounds` inside a group answers the enclosing window.** Every
  container came out as 0,0 960x680. Containers now capture `nk_widget_bounds`
  *before* `nk_group_begin`, which is the slot the group will fill.
- **A page scrolls, so much of it is off-window.** Those nodes stay in the tree
  — a reader should be able to find and scroll to them — but `reaktor_a11y_end`
  marks them `offscreen` against the window node's rect, so a magnifier is not
  sent chasing bounds nobody can see.
- **`nk_widget_bounds` answers for the row that is current, not the row the
  widget will get.** For everything that takes the row it is given this is the
  same sentence twice. A tree header is not: `nk_tree_push` resets the row to
  its own height before it draws, so a peek taken before the push carries
  whatever the last layout call set — after `api()`, a 4px spacer. Every tree
  node went into the model 4px tall, and the focus ring drew as a thin bar
  above the label rather than a box around it. The height is Nuklear's own
  expression, `font->height + 2 * tab.padding.y`, taken in one helper so a
  fourth tree cannot get it wrong. Worth keeping in mind because the bug is
  invisible in the drawing — Nuklear laid the header out correctly — and shows
  up only in what the model was told.

Node counts per page run 26 (Login) to 80 (Buttons).

### What describing a frame costs

The first version was quadratic twice over, and neither was obvious. `emit()`
scanned every node already emitted, with a `strcmp` on each, to count
same-named siblings for the id; the diff scanned the old frame for every node
in the new one. Both are tables now, and the string work that remained was cut
down after that. `build/a11ytest --bench` is the measurement, 82 nodes:

| | µs/frame |
| --- | --- |
| linear scans | 28–31 |
| id and diff through hash tables | 7.6 |
| one intern pool across frames | **3.5–4.6** |

The last step is where the string work went. The pool spans frames, so a name
that was there last frame is not copied again; equal strings share a slot, which
turns the diff's `strcmp` into `==`; the name is hashed once and the hash reused
for both the id and the pool lookup; and the role mixes in as an integer instead
of hashing its own name. What is left is roughly one pass over each name.

Two things worth keeping in mind if this is ever revisited:

- **Generation-stamped tables must not start at generation 0.** A zeroed struct
  already reads as generation 0, so every slot looked occupied and the first
  probe walked a full table forever. The test caught it; nothing else would
  have.
- **The app cannot feel this either way.** With the tree on and off, 30 and 34
  samples of the frame build gave medians 1.39 ms and 1.28 ms, standard
  deviations 0.095 and 0.043 — twelve to twenty-seven times the effect. There
  was briefly a flag to switch it off; it was removed, because a knob whose
  effect cannot be measured is not a knob, and accessibility that can be
  switched off gets switched off.

Containers need mapping too, and this is where the roles come from:

| Reaktor | Role |
| --- | --- |
| tab strip / a tab | `tablist` / `tab` |
| `nk_group_begin` | `group` |
| menu bar / menu / item | `menubar` / `menu` / `menuitem` |
| popup, contextual | `dialog`, `menu` |
| tree node | `treeitem`, with expanded state |
| `nk_edit_*` | `textbox`, value = contents |

## Phase 3 — focus and the keyboard — **done**

Independently worth doing: it is real keyboard access whether or not a screen
reader is ever attached, and it is the prerequisite for the platform bridges,
because every one of them asks "what has focus".

- The shadow tree is already in reading order, so the focusable subset of it is
  the tab order. No separate ordering to maintain.
- The shell holds `focus_id`. Tab and Shift-Tab move it; arrows move within a
  composite (tablist, list, menu); Home/End jump.
- Feed it back into Nuklear: when `focus_id` names a button, Enter and Space
  synthesise a press; when it names a field, focus the editor. This is the
  fiddly bit — it means the helper checks focus before drawing and reports
  activation after.
- Draw a focus ring. The node carries bounds, so the shell can stroke it after
  the frame from one place, in the theme's accent, rather than every widget
  growing a focused variant.

How it came out, and where it differs from the sketch above:

- **Focus is a field of the tree, not beside it.** `reaktor_a11y_set_focus`
  names an id; the node is stamped `focused` as it is emitted, so focus moving
  reaches the diff as two ordinary state changes, and the test checks exactly
  that. The shell learns where the focused node landed through the same
  reporting call every widget already makes (`focus_saw`), so no widget knows
  focus exists.
- **A move is applied at the key**, against the tree of the last drawn frame,
  which is complete and still valid between frames; the frame after — which
  the move marks dirty — draws the ring. The first version queued the move for
  the next frame, and keys arrive faster than frames: two Tabs before one frame
  merged into a single move, so twelve presses landed eleven nodes on, and the
  count wandered from run to run with timing. Moving at once has no queue to
  overwrite, and the focused node's rectangle comes with the pick, so Enter a
  moment later presses that node and not the one before it.
- **Activation is a click.** Enter and Space become a press at the node's centre
  and a release the frame after. Nuklear's default button fires on the press
  inside the widget, so this reaches every widget that takes a click — buttons,
  tabs, checkboxes, tree nodes, menu items — and a field, which takes the caret.
  Nothing at any call site changed. The pointer Nuklear sees sits there until
  the next real motion; the one the OS shows never moves.
- **Tab leaves a field.** Nuklear's `NK_KEY_TAB` inserts a character; the shell
  takes the key before Nuklear sees it, ends the edit, and moves on. Tab into a
  field puts the caret in it.
- **The ring follows the `:focus-visible` rule**: a key shows it, a click hides
  it, Escape hides it. Drawn once, from the shell, 2px in `--focus`, a pixel
  outside the node's bounds.
- **The page scrolls to focus.** A node outside the page's visible band is
  still in the tab order; when focus lands on it the shell asks the page group
  to scroll on the next frame, by the node's bounds against the band, with a
  little air. Only nodes inside the page ask — the tab strip is outside the band
  too and must not — which the tree answers by ancestry, since the page reports
  itself as a group. **A reader's focus move asks for it too**, which it did
  not at first: this lived inside the keyboard's move for long enough that UIA's
  `SetFocus`, AT-SPI's `GrabFocus` and the web mirror's focus event all stamped
  a node focused and left the page where it was, so the ring was drawn
  somewhere nobody could see. It is a function both call now.
- **The arrows are the value on a range.** Right and Up step up, Left and Down
  down, on a slider or a spinner; every other role still lets them move focus
  among siblings. The shell cannot apply the step itself — it knows a node's
  value as the text a reader would hear and nothing of its bounds or its grain
  — so it counts the presses and the widget takes them (`reaktor_focus_step`).
  They accumulate, because key repeat outruns the frame rate, and they are
  dropped at the end of a frame whether or not the range was drawn.
- **The ring is clipped to the page's band** when the focused node is in the
  page. It is drawn after the group has ended, so nothing else would clip it,
  and a node scrolled half under the band's edge was getting a whole ring —
  over the tab strip above it, or over the window's edge below. Which nodes are
  in the page the tree answers by ancestry, the same walk the scroll uses.

## Phase 4 — platform bridges

One thin interface, four implementations, each independently shippable:

```c
void reaktor_a11y_platform_init(reaktor_a11y_action activate,
                                reaktor_a11y_action focus, void *user);
void reaktor_a11y_platform_push(const reaktor_a11y *a, unsigned focus_id);
void reaktor_a11y_platform_drain(void);
```

Three things about that shape, none of them what the sketch above it first
said. `init` takes the two callbacks a client's press and a client's focus move
turn into rather than a window — a bridge that needs the window asks SDL for
it. `push` is handed the whole tree and the focused id, not the change list:
every bridge but the web's wanted to re-read the model rather than replay a
diff, and the change list is still there to be asked for. And there is no
`shutdown`. The third call is `drain`, which runs what a client asked for on
the thread that owns the tree; the pump threads are stopped by the process
exiting, which on Linux is clean and takes about 200 ms.

**Web first — done.** Emscripten draws into a canvas, and a canvas is invisible
to assistive technology — but a hidden DOM subtree beside it is not. Mirror the
shadow tree into `div`s with ARIA roles, names and states, absolutely positioned
over the canvas so hit-testing and magnifier tracking follow. The browser then
exposes it to every screen reader on every platform at once. This is what egui
does, and it is by far the best reach per line of code written.

How it came out (`src/a11y_web.c`, the JS inside `EM_JS`, about 200 lines in
all):

- **Rebuilt in tree order, not patched from the change list.** When a frame
  reports any change, every node is visited and appended to its parent in
  order. On an element already in the DOM `appendChild` is a move, not a
  recreation, so identity — and a reader's place — survives, and reading order
  is always draw order. Attributes are compared before they are set, so an
  unchanged node makes no mutation and the reader hears nothing about it. A
  frame with no changes costs one integer read.
- **Focus is announced, not moved.** The canvas keeps DOM focus, or keys would
  stop reaching the app; it carries `role="application"`, `aria-owns` the
  mirror, and `aria-activedescendant` names the focused node. That is what the
  ARIA application pattern is for.
- **A reader's press is a key's press.** Every mirror node is answered by two
  exported functions, `reaktor_a11y_web_activate` and `reaktor_a11y_web_focus`,
  which the shell turns into exactly the click and the focus that Enter and Tab
  make — so a node reached through the platform behaves as one reached through
  the keyboard, and nothing at any call site knows the difference.
- **Tab stays on the canvas.** The browser's default for Tab is to move focus
  off the canvas, after which no key reaches the app at all; `tools/shell.html`
  stops that default and nothing else, so SDL still sees the key and the app
  walks its widgets as it does on the desktop. The "native titlebar" switch is
  not drawn on the web — there is no frame to switch to.
- **Three roles translate.** Reaktor's vocabulary is the one every platform
  shares; in ARIA `progress` is `progressbar`, `listitem` is `option`, and a
  label has no role at all — it is text.
- **Checked in the browser, not in the pixels**: the page is served from
  `build-wasm` (`.claude/launch.json` starts a static server for it) and the
  mirror inspected as DOM — roles, names, states and positions. One thing to
  know when doing that: a hidden tab gets no animation frames, so the app
  draws nothing and the mirror stays empty until the page is visible. And
  SDL takes keys from the canvas element itself — a key dispatched to it as a
  DOM `KeyboardEvent` moves focus and updates `aria-activedescendant`, which
  is how Tab was checked; keys injected by a debugging protocol may not reach
  it, and that is the harness, not the app.

**Windows, UI Automation — the tree is served.** `src/sys/a11y_win32.c`, the
first file in this project that is not portable. A server-side provider:
`IRawElementProviderSimple`, `IRawElementProviderFragment` and
`IRawElementProviderFragmentRoot` on every node, `IInvokeProvider` on the
things that take a press and `IValueProvider` on the things that carry text.

Three things the sketch above got wrong or did not know:

- **A message hook cannot answer `WM_GETOBJECT`.** SDL's hook says only whether
  to keep processing a message; the answer to `WM_GETOBJECT` is the window
  procedure's *return value*, which is what `UiaReturnRawElementProvider` has
  to be handed. So the window is subclassed — `SetWindowLongPtrW`, everything
  else passed to the previous procedure.
- **A client calls in on its own thread**, while this one may be asleep in
  `SDL_WaitEvent`, and the tree it would read is rewritten by every frame. So
  no bridge touches the model: a drawn frame copies it — strings and all, since
  the model's arena is reused — into a snapshot under a lock, and the bridge
  answers from that. A request goes the other way by the same rule: it records
  an id, pushes an SDL event to wake the loop, and `reaktor_a11y_platform_drain`
  runs it on the app's thread before the next frame is built.

  None of that is Windows's problem, so it is not in the Windows file. It is
  `src/sys/a11y_snapshot.c` — portable C, SDL for the lock and the wake — and
  the bridges for 4c and 4d will share it unchanged. **No call in it hands out
  anything that outlives the lock:** a node is copied into the caller's buffer,
  navigation and hit testing answer with an id. A bridge never takes the lock,
  never holds a pointer into the snapshot, and so cannot get either wrong —
  which matters most for the two bridges that will be written on one platform
  and run on another.
- **Hit testing wants the innermost element, not the last drawn.** The tab
  strip has two groups over the same band, so taking the last node containing
  the point answered with the group beside the tabs. Depth first, draw order
  as the tie-break.

Checked with a UIA client rather than by eye: `System.Windows.Automation`
walks the tree, and the whole page comes out with its control types, names,
stable `AutomationId`s (the model's own ids, which survive a redraw) and
screen-space rectangles. Invoking the Popups tab through UIA switches the page;
`SetFocus` on a tab makes it `AutomationElement.FocusedElement`; a point inside
a tab resolves to the tab.

`IToggleProvider`, `ISelectionItemProvider` and `IRangeValueProvider` are
there too. The range one needed the model to carry numbers: a node's `value` is
the text a reader hears, and neither `IRangeValueProvider` nor ARIA's
`valuemin`/`valuemax` can be got out of that, so `reaktor_a11y_set_range` puts
`num`, `lo`, `hi` and `step` on the node the widget just reported. The web
bridge uses them too — it had been running `parseFloat` over the value text,
which happened to work for "40" and would not have for "3 of 8". Verified
through UIA: the Float slider reads 0.65 across 0–1 with a small change of
0.01, the Integer one 40 across 0–100 stepping by 1.

**Activation does not go through a synthetic click any more.** It used to:
Enter, and a screen reader's press, both became a mouse press and release at
the node's centre. That worked on the tab strip and was unreliable for widgets
inside the page group — the click reached Nuklear's input, at the right
coordinates, over the right frames, and the widget did not act on it, while
the identical injection driven by a key did. Five hypotheses were eliminated
(the real pointer's position, window foreground, which frame the click starts
on, press-versus-release semantics, and routing the steps through SDL's event
queue — that last one is *worse*, and breaks the keyboard too).

Rather than keep chasing it, the shell now tells the widget directly. A press
records an id; `reaktor_focus_activated` hands it to the widget that owns it,
once; the widget changes its own state, which it is better placed to do than
anything simulating a pointer. Checkboxes, radios, tabs and buttons take it.
Anything that has not been taught to listen still gets the synthetic click, as
a fallback the frame applies if nobody claimed the activation — so teaching one
more widget is a line, not a migration, and nothing regressed while they were
taught one at a time.

Verified through UIA: `TogglePattern.Toggle()` takes a checkbox from On to Off,
`SelectionItemPattern.Select()` moves a radio group's selection, `Invoke` on a
tab still switches the page, and Enter from the keyboard still toggles a
checkbox with the pointer parked off-window.

`IValueProvider` and `IRangeValueProvider` are both read-only: setting a
field's text or a slider's value means driving Nuklear from outside, which
belongs with `ITextProvider` and the caret.

**AT-SPI — the tree is served.** Linux *and the BSDs*: AT-SPI is a
freedesktop standard rather than a Linux one, `at-spi2-core` and Orca are in
FreeBSD ports, OpenBSD ports and pkgsrc, and nothing in the bridge is
Linux-specific. The build reaches it through `UNIX AND NOT APPLE` and the
source through `REAKTOR_HAVE_ATSPI`, so no `__linux__` appears anywhere in it and
the BSDs are covered by construction rather than by luck.

`src/sys/a11y_atspi.c` speaks the
`org.a11y.atspi.*` interfaces directly over libdbus rather than going through
ATK: ATK is LGPL and is being retired in favour of exactly this, and libdbus is
dual licensed with a permissive arm (AFL-2.1 — see [NOTICE.md](NOTICE.md)), so
the licence question and the maintenance question pointed the same way.

Three things have no analogue on Windows:

- **The accessibility bus is not the session bus.** Its address comes from
  `org.a11y.Bus.GetAddress` on the session bus. A desktop with no accessibility
  stack answers with an error, and the bridge stands down quietly — that is the
  common case, not a failure.
- **You register by being embedded.** `Embed` on the registry takes our root's
  `(bus name, object path)` and answers with the desktop's, which the root then
  reports as its parent, so the tree hangs off the desktop rather than floating.
- **One handler owns the whole subtree.** Nodes are objects at
  `/org/a11y/atspi/accessible/<id>` and there are too many to register one at a
  time when they come and go with every frame, so a fallback handler answers
  from the id in the path.

The connection is pumped by a thread of its own, which is what stage 1 was for:
every handler reads the snapshot and posts a client's request back to the app's
thread, so nothing here touches the model. `Accessible`, `Component`, `Action`
and `Value` are implemented; `Value` is read-only for the same reason
`IValueProvider` is.

**It has now been built and run**, on Debian 13 with libdbus 1.16.2, Cinnamon,
and the accessibility bus already running. For a long time this paragraph said
the opposite — that the file had never been compiled, that only a `clang
-fsyntax-only` pass against a hand-written libdbus stub stood behind it, and
that the first real build should be expected to fail. It did not fail. The
first build was clean under `-Wall -Wextra`, and the first run registered.
That is worth recording precisely because the prediction was wrong: a stub
written from the same reading of the API as the caller catches less than it
seems to, and it still caught enough.

What was checked, in the order the list below prescribes:

- **The handshake.** The app takes a name on the accessibility bus and answers
  at `/org/a11y/atspi/accessible/root` — role `application`, name `Reaktor`,
  one child. `Embed` is not refused.
- **The signatures**, which were called the largest untested surface in the
  file, and are correct. A client walk decodes `(so)`, `a(so)`, `{sv}` and the
  two-word state array without a malformed reply: the whole tree comes out with
  its roles, names and rectangles — the title bar's group, the tab list of
  seven tabs, the colour-scheme group, and the page with its widgets, which is
  the same walk Windows gives.
- **`Action` and focus.** `DoAction(0)` on the Display tab switches the page.
  `GrabFocus` on a node stamps it focused, and — since the reader's focus move
  now asks for the same reveal the keyboard does — scrolls the page to it: a
  tree node at y=2144 in a 680-tall window came back at y=636, with `SHOWING`
  set where it had been clear.
- **Shutdown**, which was the one failure predicted to be obvious rather than
  subtle. It is not there: the process exits in about 200 ms on a TERM. The
  pump thread is never explicitly stopped, because nothing stops it — see the
  note on `drain` above.

Two things are wrong, and both were predicted:

- **`GetAll` on the Properties interface answers with an empty dictionary.**
  That was a judgement rather than a measurement when it was written, and the
  measurement agrees with the worry: a client that leans on `GetAll` instead of
  asking for properties by name sees an element with nothing on it.
- **Coordinates.** `GetExtents` answers in window coordinates whatever
  coordinate type is asked for — confirmed by asking for screen coordinates and
  getting window ones — because converting needs a platform call that X and
  Wayland answer differently.

Neither is fixed here. What has not been done is **Orca**, which is the only
thing that answers whether any of this is usable rather than merely present.

**macOS, NSAccessibility — written, not yet run.** `src/sys/a11y_macos.m` is
the project's first Objective-C and the only file that is not C, because
NSAccessibility is a set of Objective-C protocols with no C entry point.

Nothing of SDL's is subclassed or swizzled. `NSAccessibilityElement` exists
exactly for UI with no `NSView` behind it, so one element answers for one node,
and SDL's content view is *told* what it contains through the accessibility
attributes AppKit lets you set on any view — `accessibilityChildren`,
`accessibilityRole`, `accessibilityLabel`. The view stays SDL's.

Two things differ from the other bridges:

- **Coordinates are upside down.** AppKit's screen origin is the bottom-left of
  the main display and the model's is the top-left of the window, so a frame
  goes view → window → screen, and hit testing goes back the other way. This is
  the thing the AT-SPI bridge leaves undone; here it cannot be, because a wrong
  frame puts the cursor ring in the wrong place rather than merely reporting an
  odd number.
- **The elements have lifetimes.** The other bridges answer with an id and
  build nothing. AppKit keeps what it is handed, so there is one element per
  live node, made on demand, and the cache is dropped whenever the tree's shape
  changes — a page that has gone would otherwise leave its elements answering
  emptily for the life of the process. Identity across frames comes free from
  the model's ids, which is what stops a reader losing its place.

Built with ARC, which is the one place in this project with object lifetimes at
all; retain counting a bridge that cannot be tested here is not a trade worth
making.

**Never compiled against real Cocoa**, like the AT-SPI one. It passes a syntax
and warnings pass under clang against a stub of the Cocoa API, which catches
typos and shape errors and cannot catch a wrong signature — the stub is this
file's idea of AppKit and would be wrong the same way. Verify cheapest-first:
Accessibility Inspector, which shows the tree the way `inspect.exe` does on
Windows, then VoiceOver.

## Phase 5 — verification

None of this is verifiable by looking at it.

- **Windows:** `inspect.exe` and Accessibility Insights from the Windows SDK
  walk the tree and show exactly what a client sees. Then a real NVDA pass.
- **Web:** Chrome DevTools' accessibility pane, then NVDA and VoiceOver.
- **Linux and the BSDs:** `busctl`, then `accerciser`, then Orca.
- **macOS:** Accessibility Inspector, then VoiceOver.
- **Always:** the Phase 1 golden file, in CI if there ever is one, because the
  instrumentation is the part that rots.

### What has to be tested on macOS, and why

The Windows bridge, the web mirror and now the AT-SPI bridge were each written
or run on a machine that could run them, and every claim about them in this
document was measured. **The NSAccessibility bridge was not.** It has never
been compiled against the real headers and has never executed an instruction.
What it has had is a `clang -fsyntax-only -Wall -Wextra` pass against a
hand-written stub of Cocoa, which catches typos, unbalanced brackets and wrong
argument counts — and cannot catch a wrong signature, because the stub is the
bridge's own idea of the API and would be wrong in the same direction.

The AT-SPI list is kept below with its answers, because the answers are the
useful part: it is the record of what a bridge written blind actually got
wrong, which is two things out of six and neither of them the one that was
feared most. The macOS list is still a list of unknowns.

**Test in this order.** Each step is cheap and tells you whether the next one
is worth attempting; a failure at step 1 makes everything after it
unobservable.

#### Linux and the BSDs — done, with the answers

1. **The bus handshake, before anything else.** `busctl --user tree` should
   show an object under `/org/a11y/atspi/accessible`. *Why first:* the bridge
   stands down silently when there is no accessibility bus — that is correct
   behaviour on a desktop without one, and indistinguishable from a bug. Until
   the object appears, nothing else can be observed. Check the log for
   "no accessibility bus" and "Embed refused", which are the two ways it gives
   up deliberately. **Answer: it appears.** Role `application`, name `Reaktor`,
   one child, and no "Embed refused" in the log. Note that `busctl tree` shows
   only `/org/a11y/atspi/accessible` itself and no nodes under it — the nodes
   are answered by a fallback handler and have nothing to introspect, which
   looks like an empty tree and is not one. Ask for the children instead.
2. **Message signatures, in Accerciser.** Every reply is hand-built with
   `dbus_message_iter_*`, and the type strings — `(so)`, `a(so)`, `{sv}`,
   `(sss)`, the two-word state array — are the largest untested surface in the
   file. *Why:* D-Bus rejects a malformed reply at run time and the stub
   accepted it at compile time, so this is where a mistake is most likely and
   least visible from here. **Answer: correct, all of them.** A client walk
   decodes every reply, and the tree comes out matching the Windows one node
   for node. The surface judged most likely to be wrong was not wrong
   anywhere.
3. **`GetAll` on the Properties interface.** It answers with an empty
   dictionary rather than an error. *Why:* that was a judgement, not a
   measurement — a client that leans on `GetAll` instead of asking for
   properties by name would see an element with nothing on it. **Answer:
   measured, and it is `{}`.** The worry was right; the behaviour is
   unchanged, and this is the first of the two things still to fix.
4. **Coordinates. This one is known wrong.** `GetExtents` answers in window
   coordinates whatever coordinate type was asked for, because converting to
   screen needs a platform call that X and Wayland answer differently, and
   guessing blind would have been worse than a documented gap. *Why it
   matters:* a magnifier or a touch reader will land in the wrong place, while
   a screen reader reading the tree aloud will seem fine — so it will pass a
   casual test and fail a real user. **Answer: still wrong, and confirmed by
   asking.** A request for screen coordinates comes back in window
   coordinates. The second of the two things still to fix.
5. **Shutdown.** The pump thread blocks in `dbus_connection_read_write_dispatch`
   and is stopped by a flag plus the connection closing. *Why:* if that is
   wrong the app hangs on exit, which is the one failure here that is obvious
   and infuriating rather than subtle. **Answer: it does not hang** — about
   200 ms on a TERM. It is also not stopped: there is no shutdown call in the
   seam and nothing sets the flag, so what ends the thread is the process
   ending. That is not a hang, and it is not a shutdown either.
6. **Then Orca**, which is the only thing that answers whether any of it is
   usable rather than merely present. **Not done.** Everything above says the
   tree is present and well-formed; none of it says it is usable.

#### macOS

1. **Whether the tree is grafted on at all.** Accessibility Inspector should
   show Reaktor's nodes under the window. *Why first:* the whole bridge rests on
   one assumption — that setting `accessibilityChildren` on SDL's content view
   is enough, with no subclass and no swizzle. If AppKit ignores it for that
   view, nothing is exposed and there is no error to see. Everything else is
   moot until this holds.
2. **Coordinates, immediately after.** AppKit's screen origin is the
   bottom-left of the main display and the model's is the top-left of the
   window; frames go view → window → screen and hit testing comes back the
   other way, including a guess about whether SDL's view is flipped. *Why:*
   unlike on Linux this cannot be deferred — a wrong frame puts VoiceOver's
   cursor ring somewhere other than the widget, which is both obvious and
   specific enough to fix from one screenshot.
3. **Sliders will not adjust, and this is a gap rather than a bug.** There is
   no `accessibilityPerformIncrement` or `Decrement`. *Why it matters more here
   than elsewhere:* on macOS those selectors are the *primary* way VoiceOver
   changes a value, where UIA and AT-SPI treat setting the value as primary.
   Wiring them needs a step request in `a11y_snapshot`, which AT-SPI's
   `Value.SetCurrentValue` wants too — one piece of work, both bridges.
4. **Whether a page change loses the reader's place.** The element cache is
   dropped whenever the tree's shape changes. *Why:* identity is what stops a
   reader jumping to the top, and dropping every element is the crudest form of
   invalidation. It may be fine — AppKit re-walks after a layout notification —
   but it is a guess.
5. **One extra nesting level.** The element for id 0 sits between the view and
   the model's window node and answers as an unlabelled group, because there is
   no node 0 in the tree. Harmless in principle; worth hearing once to know
   whether VoiceOver announces it.
6. **Then VoiceOver.**

#### What "working" looks like

There is a known-good target for both, which is the useful thing about having
done Windows first: the same tree, the same names, the same ids. On Windows a
client walk gives the window, a title-bar group, a tab list of seven tabs, a
colour-scheme group, and the page group with its widgets; `Toggle` takes a
checkbox from On to Off; `Select` moves a radio group; `Invoke` on a tab
switches the page; a point inside a tab resolves to that tab. Anything that
holds there and not on the other two is the bridge's fault and not the model's,
which is what makes these cheap to debug at a distance.

## Order, and what each phase pays for itself with

| Phase | Pays for itself with |
| --- | --- |
| 1 model | Nothing visible. Prerequisite. **Done.** |
| 2 instrumentation | Nothing visible. The bulk of the work. **Done.** |
| 3 focus + keyboard | Full keyboard operation, visible focus ring. Useful with no reader attached. **Done.** |
| 4a web | Screen-reader support on every platform, from one implementation. **Done.** |
| 4b Windows | Native support where the app is developed. **Done:** tree, focus, values, patterns and activation. |
| 4c Linux, BSD | Parity. **Done:** built and run on Debian over libdbus; tree, focus, actions. |
| 4d macOS | Parity. **Written, unbuilt** — see above. |

1 → 3 is the honest minimum before any bridge is worth writing, and 3 is the
first phase a user would notice. 4a before 4b: the web bridge is the cheapest
and reaches the most readers.

## Risks and open questions

- ~~**Per-frame cost.**~~ Measured at 3.3 µs for 80 nodes. Fixed arenas, no
  allocation, and the frame loop only builds a tree when it draws.
- ~~**Bounds.**~~ Window coordinates throughout, and a node out of sight is
  marked `offscreen` — measured against every ancestor, not only the window. A
  container's bounds are what it clips its children to, so a node scrolled out
  of a list inside a popup is out of sight even though it is well inside the
  window, which is what measuring against the root alone missed.
- ~~**Identity across pages.**~~ A subtree is reported by its root: a node
  whose parent is arriving is part of that arrival, and one whose parent is
  going is part of that departure, so neither is reported on its own. Switching
  tabs is now one addition and one removal — the two page groups — where it was
  a hundred unrelated changes, and the first frame is one addition rather than
  one per node. A client re-reads a subtree when its root changes, which is
  what every platform's structure event means; the Windows bridge accordingly
  raises `ChildrenInvalidated` rather than `ChildrenBulkAdded`, which had been
  claiming additions on a frame that only removed something.
- ~~**Live regions.**~~ The Diagnostics page turned out to have the opposite
  problem: its readings were not in the tree at all, so a reader got the prose
  and not one number. Each row is now a `label` node carrying the name and the
  value and spanning both columns, marked `REAKTOR_A11Y_VOLATILE` — it changes on
  its own and the change is not news. The web bridge writes that out as
  `aria-live="off"`, which is the default for anything that is not a live
  region and so says only what was intended; it earns its keep at 4b, where a
  provider raises a property-changed event per change unless something says
  not to.
- ~~**Scope.**~~ Phase 4 added the platform layer this project had gone without,
  and it is one directory: `src/sys/`, holding the one file that is not
  portable. `src/` is still SDL and C with no `#ifdef _WIN32` in it; the build
  adds the file on Windows and the web bridge's fallback stands down there.
