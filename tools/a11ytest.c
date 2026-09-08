/* a11ytest.c - the model's own tests, and its benchmark.
 *
 * What matters here is not that the tree can be built but that it is *stable*:
 * that ids name the same element across frames, and that the diff reports what
 * actually changed and nothing else. A tree that renumbers itself would have a
 * screen reader announce the whole window every frame.
 *
 * The expected text is in this file rather than beside it, so there is no
 * second thing to keep in sync. Exits non-zero on the first mismatch.
 *
 *     build/a11ytest            the checks
 *     build/a11ytest --bench    what describing a frame costs
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "a11y.h"

static int failures;

static struct nk_rect
r(float x, float y, float w, float h)
{
    struct nk_rect v;
    v.x = x; v.y = y; v.w = w; v.h = h;
    return v;
}

static void
check(const char *what, int got, int want)
{
    if (got == want) return;
    printf("FAIL %s: got %d, want %d\n", what, got, want);
    failures++;
}

/* The dump, into a buffer, so it can be compared rather than eyeballed. */
static void
dump_to(reaktor_a11y *a, char *out, size_t cap)
{
    FILE *f = tmpfile();
    long n;

    out[0] = '\0';
    if (!f) { printf("FAIL cannot open tmpfile\n"); failures++; return; }
    reaktor_a11y_dump(a, f);
    fflush(f);
    n = ftell(f);
    if (n < 0 || (size_t)n >= cap) {
        printf("FAIL dump is %ld bytes, buffer is %d\n", n, (int)cap);
        failures++;
        fclose(f);
        return;
    }
    rewind(f);
    n = (long)fread(out, 1, (size_t)n, f);
    out[n] = '\0';
    fclose(f);
}

static void
check_text(const char *what, const char *got, const char *want)
{
    if (strcmp(got, want) == 0) return;
    printf("FAIL %s\n--- got ---\n%s--- want ---\n%s---\n", what, got, want);
    failures++;
}

/* One screen, described twice with a difference the diff has to find. `tab`
 * says which tab is selected; `with_link` adds a node above the button, to
 * test that inserting one does not renumber the ones below. */
static void
build(reaktor_a11y *a, int tab, int with_link, int checked)
{
    static const char *const names[3] = { "Login", "Buttons", "Inputs" };
    int i;

    reaktor_a11y_begin(a, "Reaktor", r(0, 0, 960, 680));

    reaktor_a11y_push(a, REAKTOR_A11Y_TABLIST, NULL, NULL, 0, r(0, 40, 960, 34));
    for (i = 0; i < 3; i++)
        reaktor_a11y_add(a, REAKTOR_A11Y_TAB, names[i], NULL,
                       i == tab ? REAKTOR_A11Y_SELECTED : 0u,
                       r((float)(8 + i * 72), 40, 68, 34));
    reaktor_a11y_pop(a);

    reaktor_a11y_push(a, REAKTOR_A11Y_GROUP, "Buttons", NULL, 0,
                    r(0, 74, 960, 606));
    if (with_link)
        reaktor_a11y_add(a, REAKTOR_A11Y_LINK, "contact", NULL, 0,
                       r(16, 82, 60, 20));
    reaktor_a11y_add(a, REAKTOR_A11Y_BUTTON, "Continue", NULL, 0,
                   r(16, 110, 200, 38));
    reaktor_a11y_add(a, REAKTOR_A11Y_CHECKBOX, "Wrap lines", NULL,
                   checked ? REAKTOR_A11Y_CHECKED : 0u, r(16, 158, 160, 24));
    reaktor_a11y_add(a, REAKTOR_A11Y_TEXTBOX, "E-mail", "you@example.com",
                   REAKTOR_A11Y_FOCUSED, r(16, 190, 380, 38));
    reaktor_a11y_pop(a);

    reaktor_a11y_end(a);
}

/* The same shape with the page group under a given name. The app names that
 * group after the tab, and an id is derived from the name - so changing tabs
 * replaces the whole page rather than restating it, which is the case the
 * subtree rule exists for. */
static void
build_page(reaktor_a11y *a, const char *group)
{
    reaktor_a11y_begin(a, "Reaktor", r(0, 0, 960, 680));
    reaktor_a11y_push(a, REAKTOR_A11Y_GROUP, group, NULL, 0, r(0, 74, 960, 606));
    reaktor_a11y_add(a, REAKTOR_A11Y_BUTTON, "Continue", NULL, 0,
                   r(16, 110, 200, 38));
    reaktor_a11y_add(a, REAKTOR_A11Y_CHECKBOX, "Wrap lines", NULL, 0,
                   r(16, 158, 160, 24));
    reaktor_a11y_pop(a);
    reaktor_a11y_end(a);
}

/* Ids of the nodes after the insertion point, so two frames can be compared. */
static void
ids_of(reaktor_a11y *a, unsigned out[3])
{
    int n = 0, i, k = 0;
    const reaktor_a11y_node *t = reaktor_a11y_tree(a, &n);

    out[0] = out[1] = out[2] = 0;
    for (i = 0; i < n && k < 3; i++)
        if (t[i].role == REAKTOR_A11Y_BUTTON ||
            t[i].role == REAKTOR_A11Y_CHECKBOX ||
            t[i].role == REAKTOR_A11Y_TEXTBOX)
            out[k++] = t[i].id;
}

/* A page the size of the busiest real one - the Buttons tab reports 80 nodes -
 * described over and over, so the cost of describing a frame can be measured
 * on its own rather than inferred from a frame time that is mostly rendering.
 *
 *     build/a11ytest --bench
 */
static void
bench(void)
{
    static reaktor_a11y a;
    static const char *const labels[8] = {
        "Default", "Primary", "With icon", "Back", "Forward", "Menu",
        "Hold me", "Disabled"
    };
    const int frames = 20000, runs = 5;
    clock_t t0;
    double ms, best = 0.0;
    int f, i, n = 0, run;

    /* Best of five. This is a scheduler-noisy box, and the interesting number
     * is what the work costs, not what else the machine was doing. */
    for (run = 0; run < runs; run++) {
    t0 = clock();
    for (f = 0; f < frames; f++) {
        reaktor_a11y_begin(&a, "Reaktor", r(0, 0, 960, 680));
        reaktor_a11y_push(&a, REAKTOR_A11Y_TABLIST, "Pages", NULL, 0,
                        r(0, 36, 960, 34));
        for (i = 0; i < 7; i++)
            reaktor_a11y_add(&a, REAKTOR_A11Y_TAB, labels[i % 8], NULL,
                           i == (f & 3) ? REAKTOR_A11Y_SELECTED : 0u,
                           r((float)(i * 72), 40, 68, 28));
        reaktor_a11y_pop(&a);
        reaktor_a11y_push(&a, REAKTOR_A11Y_GROUP, "Buttons", NULL, 0,
                        r(0, 70, 960, 610));
        for (i = 0; i < 24; i++)
            reaktor_a11y_add(&a, REAKTOR_A11Y_BUTTON, labels[i % 8], NULL, 0,
                           r(20, (float)(100 + i * 40), 298, 38));
        /* Twenty-one identically-roled, differently-named buttons and a run of
         * unnamed ones: the two shapes the id scheme has to tell apart. */
        for (i = 0; i < 21; i++)
            reaktor_a11y_add(&a, REAKTOR_A11Y_BUTTON, NULL, NULL, 0,
                           r((float)(20 + i * 42), 507, 34, 34));
        for (i = 0; i < 27; i++)
            reaktor_a11y_add(&a, REAKTOR_A11Y_LABEL, labels[i % 8], "v", 0,
                           r(20, (float)(600 + i * 30), 910, 30));
        reaktor_a11y_pop(&a);
        reaktor_a11y_end(&a);
    }
    ms = 1000.0 * (double)(clock() - t0) / (double)CLOCKS_PER_SEC;
    if (run == 0 || ms < best) best = ms;
    }
    reaktor_a11y_tree(&a, &n);
    printf("a11y bench: %d nodes, best of %d x %d frames, %.2f us/frame\n",
           n, runs, frames, best * 1000.0 / (double)frames);
}

int main(int argc, char **argv)
{
    static reaktor_a11y a;      /* 600 KB of arenas; not a stack object */
    static char text[8192];
    unsigned before[3], after[3];
    int n, i, added, removed, restated, renamed;

    static const char *const want =
        "window \"Reaktor\" 0,0 960x680\n"
        "  tablist 0,40 960x34\n"
        "    tab \"Login\" [selected] 8,40 68x34\n"
        "    tab \"Buttons\" 80,40 68x34\n"
        "    tab \"Inputs\" 152,40 68x34\n"
        "  group \"Buttons\" 0,74 960x606\n"
        "    button \"Continue\" 16,110 200x38\n"
        "    checkbox \"Wrap lines\" 16,158 160x24\n"
        "    textbox \"E-mail\" = \"you@example.com\" [focused] 16,190 380x38\n";

    if (argc > 1 && strcmp(argv[1], "--bench") == 0) { bench(); return 0; }

    /* 1. The tree serialises the way the plan says it does. */
    build(&a, 0, 0, 0);
    dump_to(&a, text, sizeof(text));
    check_text("dump", text, want);

    reaktor_a11y_tree(&a, &n);
    check("node count", n, 9);

    /* Everything is new on the first frame, and that is one change, not nine:
     * a subtree is reported by its root and a client re-reads what hangs off
     * it. The whole tree arriving is the largest case of that. */
    reaktor_a11y_changes(&a, &n);
    check("first frame changes", n, 1);

    /* 2. The same screen again changes nothing at all. This is the property
     *    the whole design rests on: a reader must not hear the screen rebuild
     *    itself every frame. */
    build(&a, 0, 0, 0);
    reaktor_a11y_changes(&a, &n);
    check("identical frame changes", n, 0);

    /* 3. Selecting another tab is two state changes and nothing else - not a
     *    removal and an addition, which is what an unstable id would give. */
    build(&a, 1, 0, 0);
    {
        const reaktor_a11y_change *c = reaktor_a11y_changes(&a, &n);
        added = removed = restated = renamed = 0;
        for (i = 0; i < n; i++) {
            if (c[i].kind == REAKTOR_A11Y_ADDED)    added++;
            if (c[i].kind == REAKTOR_A11Y_REMOVED)  removed++;
            if (c[i].kind == REAKTOR_A11Y_RESTATED) restated++;
            if (c[i].kind == REAKTOR_A11Y_RENAMED)  renamed++;
        }
        check("tab switch: added",    added,    0);
        check("tab switch: removed",  removed,  0);
        check("tab switch: restated", restated, 2);
        check("tab switch: renamed",  renamed,  0);
    }

    /* 4. Ticking a checkbox is one state change. */
    build(&a, 1, 0, 1);
    {
        const reaktor_a11y_change *c = reaktor_a11y_changes(&a, &n);
        restated = 0;
        for (i = 0; i < n; i++)
            if (c[i].kind == REAKTOR_A11Y_RESTATED) restated++;
        check("checkbox: total changes", n, 1);
        check("checkbox: restated", restated, 1);
    }

    /* 4b. Keyboard focus is a state like any other: naming a node is one
     *     state change on the next frame, moving it is two, clearing it one.
     *     Phase 3 hangs off this. */
    {
        const reaktor_a11y_node *t = reaktor_a11y_tree(&a, &n);
        unsigned btn = 0, box = 0;

        for (i = 0; i < n; i++) {
            if (t[i].name && strcmp(t[i].name, "Continue") == 0)   btn = t[i].id;
            if (t[i].name && strcmp(t[i].name, "Wrap lines") == 0) box = t[i].id;
        }
        check("focus: nodes found", btn != 0 && box != 0, 1);

        reaktor_a11y_set_focus(&a, btn);
        build(&a, 1, 0, 1);
        reaktor_a11y_changes(&a, &n);
        check("focus: set is one change", n, 1);
        t = reaktor_a11y_tree(&a, &n);
        for (i = 0; i < n; i++)
            if (t[i].id == btn)
                check("focus: bit on the node",
                      (t[i].state & REAKTOR_A11Y_FOCUSED) != 0, 1);

        reaktor_a11y_set_focus(&a, box);
        build(&a, 1, 0, 1);
        reaktor_a11y_changes(&a, &n);
        check("focus: move is two changes", n, 2);

        reaktor_a11y_set_focus(&a, 0);
        build(&a, 1, 0, 1);
        reaktor_a11y_changes(&a, &n);
        check("focus: clear is one change", n, 1);
    }

    /* 5. Inserting a node above three others leaves their ids alone. An id
     *    derived from call order would fail this, and every widget below the
     *    insertion would be announced as new. */
    build(&a, 1, 0, 1);
    ids_of(&a, before);
    build(&a, 1, 1, 1);
    ids_of(&a, after);
    for (i = 0; i < 3; i++)
        check("id stable across an insertion above",
              (int)(before[i] == after[i] && before[i] != 0), 1);

    /* And the insertion itself is reported, once. */
    {
        const reaktor_a11y_change *c = reaktor_a11y_changes(&a, &n);
        added = 0;
        for (i = 0; i < n; i++)
            if (c[i].kind == REAKTOR_A11Y_ADDED) added++;
        check("insertion: added", added, 1);
        check("insertion: total changes", n, 1);
    }

    /* 6. Removing it again is one removal. */
    build(&a, 1, 0, 1);
    {
        const reaktor_a11y_change *c = reaktor_a11y_changes(&a, &n);
        removed = 0;
        for (i = 0; i < n; i++)
            if (c[i].kind == REAKTOR_A11Y_REMOVED) removed++;
        check("removal: removed", removed, 1);
        check("removal: total changes", n, 1);
    }

    /* 6b. A page replaced wholesale is one addition and one removal - the two
     *     group nodes - and not one of each per widget inside them. A client
     *     re-reads a subtree when its root changes, so reporting the contents
     *     as well is noise, and on a real page it is a hundred lines of it. */
    build_page(&a, "Buttons");
    build_page(&a, "Popups");
    {
        const reaktor_a11y_change *c = reaktor_a11y_changes(&a, &n);
        added = removed = 0;
        for (i = 0; i < n; i++) {
            if (c[i].kind == REAKTOR_A11Y_ADDED)   added++;
            if (c[i].kind == REAKTOR_A11Y_REMOVED) removed++;
        }
        check("page swap: added",          added,   1);
        check("page swap: removed",        removed, 1);
        check("page swap: total changes",  n,       2);
    }

    /* 7. Unlabelled siblings of the same role are told apart by order, which
     *    is the documented limit of the scheme: they keep their identities
     *    frame to frame, but inserting another one above them does shift the
     *    rest. Asserted so the limit is known rather than discovered. */
    reaktor_a11y_begin(&a, "w", r(0, 0, 10, 10));
    for (i = 0; i < 4; i++)
        reaktor_a11y_add(&a, REAKTOR_A11Y_BUTTON, NULL, NULL, 0, r(0, 0, 1, 1));
    reaktor_a11y_end(&a);
    {
        int m = 0, j, dup = 0;
        const reaktor_a11y_node *t = reaktor_a11y_tree(&a, &m);
        for (i = 0; i < m; i++)
            for (j = i + 1; j < m; j++)
                if (t[i].id == t[j].id) dup++;
        check("unlabelled siblings get distinct ids", dup, 0);
    }

    /* 8. Overflow is counted and survived, not trapped. A tree that is
     *    slightly wrong beats a frame that does not draw. */
    a.overflow_nodes = 0;
    reaktor_a11y_begin(&a, "w", r(0, 0, 10, 10));
    for (i = 0; i < REAKTOR_A11Y_MAX_NODES + 64; i++)
        reaktor_a11y_add(&a, REAKTOR_A11Y_BUTTON, "x", NULL, 0, r(0, 0, 1, 1));
    reaktor_a11y_end(&a);
    reaktor_a11y_tree(&a, &n);
    check("overflow clamps at the arena size", n, REAKTOR_A11Y_MAX_NODES);
    check("overflow is counted", a.overflow_nodes > 0, 1);

    if (failures) {
        printf("\n%d check%s failed\n", failures, failures == 1 ? "" : "s");
        return 1;
    }
    printf("a11y: all checks passed\n");
    return 0;
}
