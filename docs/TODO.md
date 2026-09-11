# TODO

Wanted, not yet built.

- **Localization.** Translating what a program says, and letting an
  application ship more than one language.
- **Support for non-Latin scripts.** Everything past Western European text —
  Cyrillic, Greek, CJK, Arabic, Hebrew, the Indic scripts.
- **A retained-mode renderer, in place of Nuklear.** Rebuilding the widget
  list every frame turns out to cost almost nothing; not knowing *which*
  widget changed costs the whole window, rasterized and pushed to the screen
  again. Retaining the tree is what would let a frame repaint only the part
  that moved, which is where the software rasterizer's time actually goes.
  The price is the entire drawing layer, and it is a high one.
