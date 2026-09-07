/* curie.h - shared helpers.
 *
 * Everything here reads from third_party/ submodules at *runtime*; no upstream
 * value is ever copied into this source tree. */
#ifndef CURIE_H
#define CURIE_H

#include <stddef.h>

/* Locates the repository root by walking up from the executable until a
 * directory containing "third_party" is found. Returns 0 on failure. */
int curie_root(char *out, size_t cap);

/* Joins the repo root and a relative path. Returns 0 on failure. */
int curie_path(char *out, size_t cap, const char *rel);

/* Reads a whole file. Caller frees with curie_free. Returns NULL on failure;
 * *len receives the byte count (excluding the NUL terminator it appends). */
char *curie_read_file(const char *path, size_t *len);

/* Both memory counters, in bytes, or 0 where the platform will not say.
 * `rss` is the resident set, the shared pages of every mapped DLL and driver
 * included; `priv` is private commit, what Task Manager calls "commit size".
 * They answer different questions. */
void curie_process_memory(size_t *rss, size_t *priv);
void  curie_free(void *p);

/* The app's own colour, for the window icon and the .ico in the executable.
 *
 * The one value in this tree that is not read from a submodule, and it cannot
 * be: the desktop draws these outside the app, before any stylesheet is
 * loaded and regardless of which theme the app is set to, so there is nothing
 * upstream to read it from. The mark *inside* the window is a different thing
 * and does follow tiny.css - see --links in main.c. */
#define CURIE_BRAND "#6b4ee6"

#endif /* CURIE_H */
