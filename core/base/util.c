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
#  include <sys/stat.h>
#  include <sys/resource.h>
#  if defined(__APPLE__)
#    include <mach-o/dyld.h>
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

#include "reaktor.h"

#define REAKTOR_PATH_CAP 1024

static int copy_out(char *out, size_t cap, const char *src)
{
    size_t len = strlen(src);
    if (len + 1 > cap) return 0;
    memcpy(out, src, len + 1);
    return 1;
}

int reaktor_root(char *out, size_t cap)
{

    /* Resolved once. Finding it means asking for the executable's own path
     * and then probing for .reaktor-root at every level up the tree - and the
     * answer cannot change while the process runs. Every asset the app opens
     * comes through here, and a theme switch empties the icon cache and would
     * otherwise re-resolve for each icon on screen. */
    static char cached[REAKTOR_PATH_CAP];
    static int  resolved;

    if (resolved) return copy_out(out, cap, cached);

#if defined(__EMSCRIPTEN__)
    resolved = 1;
    return copy_out(out, cap, cached);
#else
    {
        char exe[REAKTOR_PATH_CAP];
        char probe[REAKTOR_PATH_CAP];
        int i;

#  if defined(_WIN32)
        DWORD n = GetModuleFileNameA(NULL, exe, (DWORD)sizeof(exe));
        if (n == 0 || n >= sizeof(exe)) return 0;
        for (i = 0; exe[i]; i++) if (exe[i] == '\\') exe[i] = '/';
#  elif defined(__APPLE__)
        {
            uint32_t n = (uint32_t)sizeof(exe);
            if (_NSGetExecutablePath(exe, &n) != 0) return 0;
        }
        (void)i;
        {
            size_t exelen = strlen(exe);
            const char *tail = "/Contents/MacOS/";
            size_t taillen = strlen(tail);
            char *cut = NULL;
            if (exelen > taillen) {
                char *p;
                for (p = exe + exelen - taillen; p >= exe; p--) {
                    if (strncmp(p, tail, taillen) == 0) { cut = p; break; }
                }
            }
            if (cut) {
                char resources[REAKTOR_PATH_CAP];
                *cut = '\0';
                if (snprintf(resources, sizeof(resources),
                             "%s/Contents/Resources", exe) < 0)
                    return 0;
                resources[sizeof(resources) - 1] = '\0';
                {
                    struct stat st;
                    if (stat(resources, &st) == 0 && (st.st_mode & S_IFDIR))
                        return copy_out(out, cap, resources);
                }
            }
        }
#  elif defined(__linux__)
        {
            ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
            if (n <= 0 || (size_t)n >= sizeof(exe)) return 0;
            exe[n] = '\0';
        }
        (void)i;
#  elif defined(__FreeBSD__) || defined(__NetBSD__)
        {
#    if defined(__FreeBSD__)
            int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1 };
#    else
            int mib[4] = { CTL_KERN, KERN_PROC_ARGS, -1, KERN_PROC_PATHNAME };
#    endif
            size_t n = sizeof(exe);

            if (sysctl(mib, 4, exe, &n, NULL, 0) != 0 || n == 0) {
                if (!getcwd(exe, sizeof(exe))) return 0;
            }
            exe[sizeof(exe) - 1] = '\0';
        }
        (void)i;
#  elif defined(__OpenBSD__)
        {
            int    mib[4] = { CTL_KERN, KERN_PROC_ARGS, 0, KERN_PROC_ARGV };
            char   args[REAKTOR_PATH_CAP * 2];
            size_t n = sizeof(args);
            char  *real = NULL;

            exe[0] = '\0';
            mib[2] = (int)getpid();
            if (sysctl(mib, 4, args, &n, NULL, 0) == 0 && n > sizeof(char *)) {
                const char *a0 = ((char **)(void *)args)[0];

                if (a0 && *a0 && strchr(a0, '/')) {
                    real = realpath(a0, NULL);
                } else if (a0 && *a0) {
                    const char *p = getenv("PATH");
                    size_t      a0len = strlen(a0);

                    while (p && *p && !real) {
                        const char *sep = strchr(p, ':');
                        size_t      len = sep ? (size_t)(sep - p) : strlen(p);
                        char        cand[REAKTOR_PATH_CAP];

                        if (len && len + 1 + a0len < sizeof(cand)) {
                            memcpy(cand, p, len);
                            cand[len] = '/';
                            memcpy(cand + len + 1, a0, a0len + 1);
                            if (access(cand, X_OK) == 0)
                                real = realpath(cand, NULL);
                        }
                        p = sep ? sep + 1 : NULL;
                    }
                }
            }
            if (real) {
                struct stat st;
                if (stat(real, &st) != 0 || !S_ISREG(st.st_mode))
                    exe[0] = '\0';
                else if (!copy_out(exe, sizeof(exe), real))
                    exe[0] = '\0';
                free(real);
            }
            if (!exe[0] && !getcwd(exe, sizeof(exe))) return 0;
        }
        (void)i;
#  else
        if (!getcwd(exe, sizeof(exe))) return 0;
        (void)i;
#  endif

        for (;;) {
            char *slash;

            if (snprintf(probe, sizeof(probe), "%s/.reaktor-root", exe) < 0)
                return 0;
            probe[sizeof(probe) - 1] = '\0';
            {
                FILE *f = fopen(probe, "rb");
                if (f) {
                    fclose(f);
                    if (!copy_out(cached, sizeof(cached), exe)) return 0;
                    resolved = 1;
                    return copy_out(out, cap, cached);
                }
            }

            slash = strrchr(exe, '/');
            if (!slash) return 0;
            *slash = '\0';
        }
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

void reaktor_free(void *p) { free(p); }

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
