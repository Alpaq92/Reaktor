#ifndef REAKTOR_H
#define REAKTOR_H

#include <stddef.h>
#include <stdio.h>

int reaktor_root(char *out, size_t cap);

int reaktor_path(char *out, size_t cap, const char *rel);

char *reaktor_read_file(const char *path, size_t *len);
/* fopen, with the path read as UTF-8 on Windows too. */
FILE *reaktor_fopen(const char *path, const char *mode);

int reaktor_path_absolute(const char *path);

/* An absolute path as given; else under the root, the app's registered bytes,
 * the copy compiled in. Free with reaktor_free. */
char *reaktor_asset_load(const char *name, size_t *len);
int   reaktor_asset_exists(const char *name);
int   reaktor_asset_register(const char *name, const void *data, size_t size);
void  reaktor_asset_forget(void);

typedef struct reaktor_builtin {
    const char          *name;
    const unsigned char *data;
    size_t               size;
} reaktor_builtin;

extern const reaktor_builtin reaktor_builtins[];
extern const int             reaktor_builtin_count;

void reaktor_process_memory(size_t *rss, size_t *priv);
double reaktor_process_cpu_ms(void);
void  reaktor_free(void *p);

void reaktor_release_free_memory(void);

#define REAKTOR_BRAND "#6b4ee6"

#define REAKTOR_MARK "assets/icons/reaktor-icon.svg"

#endif
