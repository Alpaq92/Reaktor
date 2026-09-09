# Onlay

A 2D layout engine in C99. It computes rectangles and does nothing else — it
draws nothing, owns no widgets, handles no input, and knows nothing about the
program using it.

```c
lay_context ctx;
lay_init_context(&ctx);
lay_reserve_items_capacity(&ctx, 256);

lay_id card = lay_item(&ctx);
lay_set_size_xy(&ctx, card, 420, 354);
lay_set_contain(&ctx, card, LAY_COLUMN | LAY_START);

lay_id row = lay_item(&ctx);
lay_set_behave(&ctx, row, LAY_HFILL);
lay_set_weight(&ctx, row, 2.0f);     /* twice a sibling's share */
lay_insert(&ctx, card, row);

lay_run_context(&ctx);
lay_vec4 r = lay_get_rect(&ctx, row);   /* r.v[0..3] = x, y, w, h */
```

## Why the symbols still say `lay_`

Onlay is the library; `lay_` is the prefix it inherited. Renaming every symbol
touched 96% of the file and changed the diff against upstream from 215 lines to
1,153 — for no functional gain. A fork that still reads as a diff is worth more
than a consistent prefix, so the rename waits for the commit that moves Onlay
into its own repository, where there is no longer an upstream to read against.

Every line that differs from upstream is marked `ONLAY:` in a comment. To see
the whole patch:

```
grep -n "ONLAY:" onlay.h
```

## Where it came from

Onlay is a fork of [randrew/layout](https://github.com/randrew/layout), itself
derived from the layout code in Leonard Ritter's *oui*. Both are MIT, and
`LICENSE` carries all three copyrights.

One thing changed, and it is the reason the fork exists:

**It is plain C.** Upstream used GCC's `vector_size` extension for its rect and
size types, and fell back to a C++ class with `operator[]` under MSVC — which
forces the whole library to be compiled as C++ on that toolchain. Both are
replaced by a struct holding an array, so the algorithm still indexes by
dimension and the code stays C99 everywhere. Rects are read as `r.v[0]` rather
than `r[0]`; that is the only change to the API.

## What it adds

Upstream splits leftover space equally among the items that fill. Onlay makes
that share proportional, and gives each filling item a floor:

    final = floor + leftover * weight / total_weight

One formula then covers proportional columns (`0.2 / 0.5 / 0.3`), `fr`-style
grid tracks, and "at least 80 pixels, then grow". A filling item's own size is
its floor, which costs nothing for the items that do not set one — and is why
the change is invisible to code written before weights existed.

Still to come: `gap`, grid, and anchoring one item to another's computed rect.

## Design notes

- **No allocation during layout.** `lay_reset_context()` sets the item count
  to zero and reuses the buffer, so reserving once means a frame never touches
  the allocator. `LAY_REALLOC`, `LAY_FREE` and `LAY_MEMSET` are
  overridable if you want it inside an arena of your own.
- **Coordinates are `int16_t` by default.** Define `LAY_FLOAT` for floats.
- **Indexed by dimension.** The same code runs for the horizontal pass and then
  the vertical one, which is why rects hold an array rather than named fields.

## Tests

`test_onlay.c` checks the engine against real geometry — the login card of the
application Onlay was written for, with the expected coordinates taken from
that application's accessibility tree rather than from Onlay itself.
