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

size_t curie_process_rss(void)
{
#if defined(_WIN32)
    /* psapi via GetProcessMemoryInfo, reached through the process handle we
     * already have - no import, so nothing links against psapi. */
    PROCESS_MEMORY_COUNTERS pmc;
    typedef BOOL(WINAPI * fn_t)(HANDLE, PROCESS_MEMORY_COUNTERS *, DWORD);
    static fn_t get_info;
    static int looked_up;

    if (!looked_up) {
        HMODULE m = LoadLibraryA("psapi.dll");
        if (m) get_info = (fn_t)(void *)GetProcAddress(m, "GetProcessMemoryInfo");
        looked_up = 1;
    }
    if (!get_info) return 0;
    pmc.cb = sizeof(pmc);
    if (!get_info(GetCurrentProcess(), &pmc, sizeof(pmc))) return 0;
    return (size_t)pmc.WorkingSetSize;
#elif defined(__linux__)
    FILE *f = fopen("/proc/self/statm", "r");
    unsigned long total = 0, resident = 0;
    if (!f) return 0;
    if (fscanf(f, "%lu %lu", &total, &resident) != 2) resident = 0;
    fclose(f);
    return (size_t)resident * (size_t)sysconf(_SC_PAGESIZE);
#else
    return 0;
#endif
}
