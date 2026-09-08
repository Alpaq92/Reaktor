/* reaktor.h - shared helpers.
 *
 * Everything here reads from third_party/ submodules at *runtime*; no upstream
 * value is ever copied into this source tree. */
#ifndef REAKTOR_H
#define REAKTOR_H

#include <stddef.h>

/* Locates the repository root by walking up from the executable until a
 * directory containing "third_party" is found. Returns 0 on failure. */
int reaktor_root(char *out, size_t cap);

/* Joins the repo root and a relative path. Returns 0 on failure. */
int reaktor_path(char *out, size_t cap, const char *rel);

/* Reads a whole file. Caller frees with reaktor_free. Returns NULL on failure;
 * *len receives the byte count (excluding the NUL terminator it appends). */
char *reaktor_read_file(const char *path, size_t *len);

/* Both memory counters, in bytes, or 0 where the platform will not say.
 * `rss` is the resident set, the shared pages of every mapped DLL and driver
 * included; `priv` is private commit, what Task Manager calls "commit size".
 * They answer different questions. */
void reaktor_process_memory(size_t *rss, size_t *priv);
/* CPU time used so far by this process, every thread, in milliseconds. */
double reaktor_process_cpu_ms(void);
void  reaktor_free(void *p);

/* The app's own colour, for the window icon and the .ico in the executable.
 *
 * The one value in this tree that is not read from a submodule, and it cannot
 * be: the desktop draws these outside the app, before any stylesheet is
 * loaded and regardless of which theme the app is set to, so there is nothing
 * upstream to read it from. The mark *inside* the window is a different thing
 * and does follow tiny.css - see --links in main.c. */
#define REAKTOR_BRAND "#6b4ee6"

/* The application mark. One file, so the title bar, the window icon and the
 * linked .ico cannot drift apart. */
#define REAKTOR_MARK "branding/reaktor-icon.svg"

#endif /* REAKTOR_H */
