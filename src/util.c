/* util.c - path resolution and whole-file reading.
 *
 * Everything the app needs from third_party/ is read from disk at runtime, so
 * these two functions are the whole of the I/O layer - and the only place in
 * the tree that has to know what a filesystem looks like on each platform.
 *
 * Paths are joined with '/' everywhere, including Windows: every Win32 file
 * API and the CRT accept it, and one separator means one code path. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <psapi.h>
#elif defined(__EMSCRIPTEN__)
/* nothing: the root is fixed, see below */
#else
#  include <unistd.h>
#  include <sys/stat.h>
#endif

#include "curie.h"

#define CURIE_PATH_CAP 1024

static int copy_out(char *out, size_t cap, const char *src)
{
    size_t len = strlen(src);
    if (len + 1 > cap) return 0;
    memcpy(out, src, len + 1);
    return 1;
}

int curie_root(char *out, size_t cap)
{
    /* Explicit override wins: needed for installed layouts, and for running
     * the binary from anywhere. */
    const char *env = getenv("CURIE_ROOT");
    if (env && *env) return copy_out(out, cap, env);

#if defined(__EMSCRIPTEN__)
    /* There is no executable to walk up from, and no ambiguity to resolve:
     * the assets are packaged into the virtual filesystem at exactly the
     * paths the rest of the code already asks for, so the root is its root.
     * "" rather than "/" because curie_path inserts the separator. */
    return copy_out(out, cap, "");
#else
    {
        char exe[CURIE_PATH_CAP];
        char probe[CURIE_PATH_CAP];
        int i;

#  if defined(_WIN32)
        DWORD n = GetModuleFileNameA(NULL, exe, (DWORD)sizeof(exe));
        if (n == 0 || n >= sizeof(exe)) return 0;
        /* Normalise, so the walk below only has to look for one separator. */
        for (i = 0; exe[i]; i++) if (exe[i] == '\\') exe[i] = '/';
#  else
        /* No executable path without a platform call; the working directory
         * is the honest fallback, and CURIE_ROOT covers the rest. */
        if (!getcwd(exe, sizeof(exe))) return 0;
        (void)i;
#  endif

        /* Walk up looking for the .curie-root sentinel.
         *
         * This used to probe for a "third_party" directory, which broke under
         * CMake: add_subdirectory(third_party/SDL) mirrors that path into the
         * build tree, so build/third_party existed and the walk stopped one
         * level too early. A dedicated marker cannot be shadowed that way. */
        for (;;) {
            char *slash = strrchr(exe, '/');
            if (!slash) return 0;
            *slash = '\0';

            if (snprintf(probe, sizeof(probe), "%s/.curie-root", exe) < 0)
                return 0;
            probe[sizeof(probe) - 1] = '\0';
            {
                FILE *f = fopen(probe, "rb");
                if (f) { fclose(f); return copy_out(out, cap, exe); }
            }
            /* Stop once we have chewed back to the root. */
            if (strchr(exe, '/') == NULL) return 0;
        }
    }
#endif
}

int curie_path(char *out, size_t cap, const char *rel)
{
    char root[CURIE_PATH_CAP];

    if (!curie_root(root, sizeof(root))) return 0;
    if (snprintf(out, cap, "%s/%s", root, rel) < 0) return 0;
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

#if defined(_WIN32)
/* Both counters come from one call. <psapi.h> maps GetProcessMemoryInfo to
 * K32GetProcessMemoryInfo, which kernel32 exports, so this needs no import
 * library and no runtime lookup. The EX form is a superset - PrivateUsage is
 * appended after the fields the plain struct has. */
static int win_mem(PROCESS_MEMORY_COUNTERS_EX *out)
{
    out->cb = sizeof(*out);
    return GetProcessMemoryInfo(GetCurrentProcess(),
                                (PROCESS_MEMORY_COUNTERS *)out,
                                sizeof(*out)) ? 1 : 0;
}
#endif

void curie_process_memory(size_t *rss, size_t *priv)
{
    *rss = *priv = 0;

#if defined(_WIN32)
    {
        PROCESS_MEMORY_COUNTERS_EX pmc;
        if (win_mem(&pmc)) {
            *rss  = (size_t)pmc.WorkingSetSize;
            *priv = (size_t)pmc.PrivateUsage;
        }
    }
#elif defined(__linux__)
    {
        FILE *f = fopen("/proc/self/statm", "r");
        unsigned long resident = 0;

        if (f) {
            if (fscanf(f, "%*lu %lu", &resident) == 1)
                *rss = (size_t)resident * (size_t)sysconf(_SC_PAGESIZE);
            fclose(f);
        }
        /* The nearest thing Linux has to Windows' commit. The kernel
         * synthesises this file by walking every mapping, so both wanted rows
         * are taken on one pass and the loop stops as soon as it has them. */
        f = fopen("/proc/self/smaps_rollup", "r");
        if (f) {
            char line[256];
            unsigned long kb = 0;
            int got = 0;

            while (got < 2 && fgets(line, sizeof(line), f)) {
                if (strncmp(line, "Private_Clean:", 14) == 0 ||
                    strncmp(line, "Private_Dirty:", 14) == 0) {
                    kb += strtoul(line + 14, NULL, 10);
                    got++;
                }
            }
            fclose(f);
            *priv = (size_t)kb * 1024u;
        }
    }
#endif
}
