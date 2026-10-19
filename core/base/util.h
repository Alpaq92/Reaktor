#ifndef REAKTOR_UTIL_H
#define REAKTOR_UTIL_H

#include <stddef.h>
#include <stdio.h>


int reaktor_path(char *out, size_t cap, const char *rel);

char *reaktor_read_file(const char *path, size_t *len);
FILE *reaktor_fopen(const char *path, const char *mode);

int reaktor_path_absolute(const char *path);
const char *reaktor_path_leaf(const char *path);

char *reaktor_c_locale_keep(int numeric);
void  reaktor_c_locale_put(int numeric, char *kept);

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

#define REAKTOR_FNV 2166136261u

static inline unsigned
reaktor_fnv(unsigned h, const char *s)
{
    if (s)
        while (*s) h = (h ^ (unsigned char)*s++) * 16777619u;
    return h;
}

static inline unsigned
reaktor_fnv_n(unsigned h, const void *p, size_t n)
{
    const unsigned char *b = (const unsigned char *)p;

    while (n--) h = (h ^ *b++) * 16777619u;
    return h;
}

#define REAKTOR_MARK "assets/icons/reaktor-icon.svg"

#endif
