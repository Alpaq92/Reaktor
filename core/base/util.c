#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <psapi.h>
#elif defined(__EMSCRIPTEN__)
#else
#  include <unistd.h>
#  include <sys/resource.h>
#  if defined(__APPLE__)
#    include <stdint.h>
#    include <malloc/malloc.h>
#  endif
#  if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#    include <sys/types.h>
#    include <sys/sysctl.h>
#    if defined(__FreeBSD__)
#      include <sys/user.h>
#    endif
#  endif
#endif

#include <SDL3/SDL.h>

#include "util.h"

#define REAKTOR_PATH_CAP 1024

FILE *reaktor_fopen(const char *path, const char *mode)
{
#if defined(_WIN32)
    wchar_t w[REAKTOR_PATH_CAP], m[8];

    if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, w, REAKTOR_PATH_CAP) ||
        !MultiByteToWideChar(CP_UTF8, 0, mode, -1, m, 8))
        return NULL;
    return _wfopen(w, m);
#else
    return fopen(path, mode);
#endif
}

static int file_exists(const char *path)
{
    SDL_PathInfo info;

    return SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_FILE;
}

static int copy_out(char *out, size_t cap, const char *src)
{
    size_t len = strlen(src);
    if (len + 1 > cap) return 0;
    memcpy(out, src, len + 1);
    return 1;
}

static int reaktor_root(char *out, size_t cap)
{
    static char cached[REAKTOR_PATH_CAP];
    static int  resolved;

    if (resolved) return copy_out(out, cap, cached);

#if defined(__EMSCRIPTEN__)
    resolved = 1;
    return copy_out(out, cap, cached);
#else
    {
        const char *base = SDL_GetBasePath();
        char        dir[REAKTOR_PATH_CAP], probe[REAKTOR_PATH_CAP];
        size_t      n;
        int         i, up;

        if (!base || !copy_out(dir, sizeof(dir), base)) return 0;
        for (i = 0; dir[i]; i++) if (dir[i] == '\\') dir[i] = '/';
        n = strlen(dir);
        while (n > 1 && dir[n - 1] == '/') dir[--n] = '\0';
        if (n > 19 && strcmp(dir + n - 19, "/Contents/Resources") == 0)
            return copy_out(out, cap, dir);

        for (up = 0; up <= 2; up++) {
            char *slash;

            if (snprintf(probe, sizeof(probe), "%s/.reaktor-root", dir) < 0)
                return 0;
            probe[sizeof(probe) - 1] = '\0';
            if (file_exists(probe)) {
                if (!copy_out(cached, sizeof(cached), dir)) return 0;
                resolved = 1;
                return copy_out(out, cap, cached);
            }

            slash = strrchr(dir, '/');
            if (!slash) return 0;
            *slash = '\0';
        }
        return 0;
    }
#endif
}

int reaktor_path(char *out, size_t cap, const char *rel)
{
    char root[REAKTOR_PATH_CAP];

    if (!reaktor_root(root, sizeof(root))) return 0;
    if (snprintf(out, cap, "%s/%s", root, rel) < 0) return 0;
    out[cap - 1] = '\0';
    return 1;
}

char *reaktor_read_file(const char *path, size_t *len)
{
    if (len) *len = 0;
    return (char *)SDL_LoadFile(path, len);
}

int reaktor_path_absolute(const char *path)
{
    if (!path || !path[0]) return 0;
    if (path[0] == '/' || path[0] == '\\') return 1;
#if defined(_WIN32)
    if (((path[0] >= 'A' && path[0] <= 'Z') ||
         (path[0] >= 'a' && path[0] <= 'z')) &&
        path[1] == ':' && (path[2] == '/' || path[2] == '\\'))
        return 1;
#endif
    return 0;
}

int reaktor_grow(void **p, int *cap, int need, size_t size)
{
    int   n = *cap > 0 ? *cap : 64;
    void *q;

    if (need <= *cap) return 1;
    while (n < need) n *= 2;
    if (!(q = SDL_realloc(*p, (size_t)n * size))) return 0;
    *p   = q;
    *cap = n;
    return 1;
}

char *reaktor_path_from_cwd(const char *path)
{
    char *cwd, *out = NULL;

    if (reaktor_path_absolute(path)) return SDL_strdup(path);
    if (!(cwd = SDL_GetCurrentDirectory())) return NULL;
    if (SDL_asprintf(&out, "%s%s", cwd, path) < 0) out = NULL;
    SDL_free(cwd);
    return out;
}

const char *reaktor_path_leaf(const char *path)
{
    const char *p, *leaf = path;

    for (p = path; p && *p; p++)
        if (*p == '/' || *p == '\\') leaf = p + 1;
    return leaf;
}

char *reaktor_c_locale_keep(int numeric)
{
    const char *now = setlocale(numeric ? LC_NUMERIC : LC_ALL, NULL);
    size_t n = now ? strlen(now) + 1 : 0;
    char *kept = n ? (char *)malloc(n) : NULL;

    if (kept) memcpy(kept, now, n);
    return kept;
}

void reaktor_c_locale_put(int numeric, char *kept)
{
    setlocale(numeric ? LC_NUMERIC : LC_ALL, kept ? kept : "C");
    free(kept);
}

#define REGISTERED_MAX 64

static reaktor_builtin g_registered[REGISTERED_MAX];
static int             g_registered_n;

int reaktor_asset_register(const char *name, const void *data, size_t size)
{
    int i;

    if (!name || !data) return 0;
    for (i = 0; i < g_registered_n; i++)
        if (strcmp(g_registered[i].name, name) == 0) break;
    if (i == REGISTERED_MAX) return 0;
    g_registered[i].name = name;
    g_registered[i].data = (const unsigned char *)data;
    g_registered[i].size = size;
    if (i == g_registered_n) g_registered_n++;
    return 1;
}

void reaktor_asset_forget(void)
{
    g_registered_n = 0;
}

static const reaktor_builtin *in_memory(const char *name)
{
    int i;

    for (i = 0; i < g_registered_n; i++)
        if (strcmp(g_registered[i].name, name) == 0) return &g_registered[i];
    for (i = 0; i < reaktor_builtin_count; i++)
        if (strcmp(reaktor_builtins[i].name, name) == 0) return &reaktor_builtins[i];
    return NULL;
}

int reaktor_asset_exists(const char *name)
{
    char full[REAKTOR_PATH_CAP];

    if (!name) return 0;
    if (reaktor_path_absolute(name)) return file_exists(name);
    if (reaktor_path(full, sizeof(full), name) && file_exists(full)) return 1;
    return in_memory(name) != NULL;
}

char *reaktor_asset_load(const char *name, size_t *len)
{
    char full[REAKTOR_PATH_CAP];
    const reaktor_builtin *b;
    char *p = NULL;

    if (len) *len = 0;
    if (!name) return NULL;
    if (reaktor_path_absolute(name)) return reaktor_read_file(name, len);
    if (reaktor_path(full, sizeof(full), name)) p = reaktor_read_file(full, len);
    if (p) return p;

    b = in_memory(name);
    if (!b) return NULL;
    p = (char *)SDL_malloc(b->size + 1);
    if (!p) return NULL;
    memcpy(p, b->data, b->size);
    p[b->size] = '\0';
    if (len) *len = b->size;
    return p;
}

void reaktor_free(void *p) { SDL_free(p); }

void reaktor_release_free_memory(void)
{
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
    malloc_zone_pressure_relief(NULL, 0);
#endif
}

#if defined(_WIN32)
static int win_mem(PROCESS_MEMORY_COUNTERS_EX *out)
{
    out->cb = sizeof(*out);
    return GetProcessMemoryInfo(GetCurrentProcess(),
                                (PROCESS_MEMORY_COUNTERS *)out,
                                sizeof(*out)) ? 1 : 0;
}
#endif

double reaktor_process_cpu_ms(void)
{
#if defined(_WIN32)
    FILETIME c, e, k, u;
    ULARGE_INTEGER ku, uu;
    if (!GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) return 0.0;
    ku.LowPart = k.dwLowDateTime; ku.HighPart = k.dwHighDateTime;
    uu.LowPart = u.dwLowDateTime; uu.HighPart = u.dwHighDateTime;
    return (double)(ku.QuadPart + uu.QuadPart) / 10000.0;
#elif defined(__EMSCRIPTEN__)
    return 0.0;
#else
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) != 0) return 0.0;
    return (double)ru.ru_utime.tv_sec * 1000.0 + (double)ru.ru_utime.tv_usec / 1000.0
         + (double)ru.ru_stime.tv_sec * 1000.0 + (double)ru.ru_stime.tv_usec / 1000.0;
#endif
}

void reaktor_process_memory(size_t *rss, size_t *priv)
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
#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
    {
#  if defined(__FreeBSD__)
        struct kinfo_proc kp;
        int    mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, 0 };
        size_t n = sizeof(kp);

        mib[3] = (int)getpid();
        if (sysctl(mib, 4, &kp, &n, NULL, 0) == 0 && n >= sizeof(kp))
            *rss = (size_t)kp.ki_rssize * (size_t)sysconf(_SC_PAGESIZE);
#  else
#    if defined(__NetBSD__)
#      define REAKTOR_KINFO struct kinfo_proc2
#      define REAKTOR_KWHAT KERN_PROC2
#    else
#      define REAKTOR_KINFO struct kinfo_proc
#      define REAKTOR_KWHAT KERN_PROC
#    endif
        REAKTOR_KINFO kp;
        int    mib[6] = { CTL_KERN, REAKTOR_KWHAT, KERN_PROC_PID, 0,
                          (int)sizeof(REAKTOR_KINFO), 1 };
        size_t n = sizeof(kp);

        mib[3] = (int)getpid();
        if (sysctl(mib, 6, &kp, &n, NULL, 0) == 0 && n >= sizeof(kp))
            *rss = (size_t)kp.p_vm_rssize * (size_t)sysconf(_SC_PAGESIZE);
#    undef REAKTOR_KINFO
#    undef REAKTOR_KWHAT
#  endif
    }
#endif
}
