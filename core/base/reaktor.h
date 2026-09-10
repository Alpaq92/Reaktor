#ifndef REAKTOR_H
#define REAKTOR_H

#include <stddef.h>

int reaktor_root(char *out, size_t cap);

int reaktor_path(char *out, size_t cap, const char *rel);

char *reaktor_read_file(const char *path, size_t *len);

void reaktor_process_memory(size_t *rss, size_t *priv);
double reaktor_process_cpu_ms(void);
void  reaktor_free(void *p);

void reaktor_release_free_memory(void);

#define REAKTOR_BRAND "#6b4ee6"

#define REAKTOR_MARK "assets/icons/reaktor-icon.svg"

#endif
