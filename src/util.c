/* util.c - repo-root discovery, file loading, and Open-Color lookup.
 *
 * The Open-Color reader is a deliberately small scanner rather than a general
 * JSON parser: open-color.json is a flat map of family -> array of hex
 * strings, so locating "family" and then counting quoted strings inside the
 * following [...] is sufficient and keeps the dependency count at zero. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "curie.h"

int curie_root(char *out, size_t cap)
{
    char exe[MAX_PATH];
    char probe[MAX_PATH];
    DWORD n;
    size_t i;

    /* Explicit override wins: needed for installed layouts, and for running
     * the binary from anywhere. */
    {
        const char *env = getenv("CURIE_ROOT");
        if (env && *env) {
            size_t len = strlen(env);
            if (len + 1 > cap) return 0;
            for (i = 0; i <= len; i++) out[i] = env[i];
            return 1;
        }
    }

    n = GetModuleFileNameA(NULL, exe, (DWORD)sizeof(exe));
    if (n == 0 || n >= sizeof(exe)) return 0;

    /* Walk up from the executable looking for the .curie-root sentinel.
     *
     * This used to probe for a "third_party" directory, which broke under
     * CMake: add_subdirectory(third_party/SDL) mirrors that path into the
     * build tree, so build/third_party exists and the walk stopped one level
     * too early. A dedicated marker cannot be shadowed that way. */
    for (;;) {
        char *slash = strrchr(exe, '\\');
        if (!slash) return 0;
        *slash = '\0';

        if (_snprintf(probe, sizeof(probe), "%s\\.curie-root", exe) < 0) return 0;
        probe[sizeof(probe) - 1] = '\0';
        if (GetFileAttributesA(probe) != INVALID_FILE_ATTRIBUTES) {
            size_t len = strlen(exe);
            if (len + 1 > cap) return 0;
            for (i = 0; i <= len; i++) out[i] = exe[i];
            return 1;
        }
        /* Stop once we have chewed back to the drive root. */
        if (strchr(exe, '\\') == NULL) return 0;
    }
}

int curie_path(char *out, size_t cap, const char *rel)
{
    char root[MAX_PATH];
    if (!curie_root(root, sizeof(root))) return 0;
    if (_snprintf(out, cap, "%s\\%s", root, rel) < 0) return 0;
    out[cap - 1] = '\0';
    return 1;
}

char *curie_read_file(const char *path, size_t *len)
{
    FILE *f;
    char *buf;
    long size;
    size_t got;

    if (len) *len = 0;
    f = fopen(path, "rb");
    if (!f) return NULL;

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    size = ftell(f);
    if (size < 0) { fclose(f); return NULL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }

    buf = (char *)malloc((size_t)size + 1);
    if (!buf) { fclose(f); return NULL; }

    got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[got] = '\0';
    if (len) *len = got;
    return buf;
}

void curie_free(void *p) { free(p); }

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Parses "#rrggbb". Returns 0 if malformed. */
static int parse_hex_color(const char *s, unsigned char *r, unsigned char *g,
                           unsigned char *b)
{
    int v[6];
    int i;
    if (!s || *s != '#') return 0;
    for (i = 0; i < 6; i++) {
        v[i] = hex_nibble(s[1 + i]);
        if (v[i] < 0) return 0;
    }
    *r = (unsigned char)(v[0] * 16 + v[1]);
    *g = (unsigned char)(v[2] * 16 + v[3]);
    *b = (unsigned char)(v[4] * 16 + v[5]);
    return 1;
}

int curie_oc_color(const char *family, int index, unsigned char *r,
                   unsigned char *g, unsigned char *b)
{
    char path[MAX_PATH];
    char key[64];
    char *json;
    const char *p;
    int found = 0;
    int i;

    if (index < 0) return 0;
    if (!curie_path(path, sizeof(path),
                    "third_party\\open-color\\open-color.json")) return 0;

    json = curie_read_file(path, NULL);
    if (!json) return 0;

    if (_snprintf(key, sizeof(key), "\"%s\"", family) < 0) {
        curie_free(json);
        return 0;
    }
    key[sizeof(key) - 1] = '\0';

    p = strstr(json, key);
    if (p) {
        p = strchr(p + strlen(key), '[');   /* start of the shade array */
        if (p) {
            const char *q = p;
            /* Walk to the index-th quoted string, stopping at the closing ]. */
            for (i = 0; i <= index; i++) {
                const char *open = strchr(q, '"');
                const char *end  = strchr(q, ']');
                if (!open || (end && end < open)) { q = NULL; break; }
                q = open + 1;               /* first char inside the quotes */
                if (i == index) {
                    found = parse_hex_color(q, r, g, b);
                    break;
                }
                q = strchr(q, '"');          /* closing quote of this entry */
                if (!q) break;
                q++;
            }
        }
    }

    curie_free(json);
    return found;
}
