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
void  curie_free(void *p);

/* Open-Color lookup: family is e.g. "indigo", index is 0-9.
 * Reads third_party/open-color/open-color.json on each call.
 * Returns 0 if the family or index is not present. */
int curie_oc_color(const char *family, int index, unsigned char *r,
                   unsigned char *g, unsigned char *b);

#endif /* CURIE_H */
