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
#  include <sys/resource.h>
#  if defined(__APPLE__)
#    include <mach-o/dyld.h>
#    include <stdint.h>
#    include <malloc/malloc.h>
#  endif
/* The BSDs have no /proc, so both questions Linux answers by reading a file -
 * where am I installed, and how much memory am I using - are asked of the
 * kernel directly instead.
 *
 * All three answer the memory one. FreeBSD and NetBSD answer the other
 * outright; OpenBSD does not expose a process's executable path at all, on
 * purpose, so there it is reconstructed from argv[0] and checked before it is
 * believed. See the branches below for what each one costs. */
#  if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
#    include <sys/types.h>
#    include <sys/sysctl.h>
#    if defined(__FreeBSD__)
#      include <sys/user.h>   /* struct kinfo_proc; the others put it above */
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
    /* Explicit override wins: needed for installed layouts, and for running
     * the binary from anywhere. */
    const char *env = getenv("REAKTOR_ROOT");
    if (env && *env) return copy_out(out, cap, env);

#if defined(__EMSCRIPTEN__)
    /* There is no executable to walk up from, and no ambiguity to resolve:
     * the assets are packaged into the virtual filesystem at exactly the
     * paths the rest of the code already asks for, so the root is its root.
     * "" rather than "/" because reaktor_path inserts the separator. */
    return copy_out(out, cap, "");
#else
    {
        char exe[REAKTOR_PATH_CAP];
        char probe[REAKTOR_PATH_CAP];
        int i;

#  if defined(_WIN32)
        DWORD n = GetModuleFileNameA(NULL, exe, (DWORD)sizeof(exe));
        if (n == 0 || n >= sizeof(exe)) return 0;
        /* Normalise, so the walk below only has to look for one separator. */
        for (i = 0; exe[i]; i++) if (exe[i] == '\\') exe[i] = '/';
#  elif defined(__APPLE__)
        {
            uint32_t n = (uint32_t)sizeof(exe);
            if (_NSGetExecutablePath(exe, &n) != 0) return 0;
        }
        (void)i;
        /* Inside a .app bundle the executable lives at
         *   Something.app/Contents/MacOS/reaktor
         * and the assets belong under Contents/Resources. Detect that shape
         * from the path suffix and hand back the resource dir directly, so
         * the walk below never runs and a bundle can sit anywhere on disk
         * without a .reaktor-root file next to it. */
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
            /* The same question, spelled two ways: FreeBSD hangs
             * KERN_PROC_PATHNAME off KERN_PROC, NetBSD off KERN_PROC_ARGS.
             * -1 is this process in both. */
#    if defined(__FreeBSD__)
            int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1 };
#    else
            int mib[4] = { CTL_KERN, KERN_PROC_ARGS, -1, KERN_PROC_PATHNAME };
#    endif
            size_t n = sizeof(exe);

            /* A kernel that will not answer leaves the working directory,
             * which is what every platform without this call already uses -
             * so being wrong here costs the old behaviour, not the app. */
            if (sysctl(mib, 4, exe, &n, NULL, 0) != 0 || n == 0) {
                if (!getcwd(exe, sizeof(exe))) return 0;
            }
            exe[sizeof(exe) - 1] = '\0';
        }
        (void)i;
#  elif defined(__OpenBSD__)
        {
            /* OpenBSD will not tell a process where its own executable is,
             * and that is a decision rather than a gap. What is left is
             * argv[0], which the kernel does not promise is a path at all -
             * so it is resolved, then checked on disk, and anything that does
             * not check out leaves the working directory, which is what this
             * platform used before. REAKTOR_ROOT still overrides both.
             *
             * Good enough for the case that matters: started from a shell,
             * argv[0] is either a path or a name the shell found on PATH, and
             * both lead to the right file. */
            int    mib[4] = { CTL_KERN, KERN_PROC_ARGS, 0, KERN_PROC_ARGV };
            char   args[REAKTOR_PATH_CAP * 2];
            size_t n = sizeof(args);
            char  *real = NULL;

            exe[0] = '\0';
            mib[2] = (int)getpid();
            /* The buffer comes back as a NULL-terminated array of pointers
             * into itself, with the strings following them. */
            if (sysctl(mib, 4, args, &n, NULL, 0) == 0 && n > sizeof(char *)) {
                const char *a0 = ((char **)(void *)args)[0];

                if (a0 && *a0 && strchr(a0, '/')) {
                    real = realpath(a0, NULL);
                } else if (a0 && *a0) {
                    /* A bare name means the shell searched PATH; do the same,
                     * and stop at the first entry that could have been run. */
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
        /* No executable path without a platform call. The working directory
         * is the honest fallback, and REAKTOR_ROOT covers the rest. */
        if (!getcwd(exe, sizeof(exe))) return 0;
        (void)i;
#  endif

        /* Walk up looking for the .reaktor-root sentinel.
         *
         * This used to probe for a "third_party" directory, which broke under
         * CMake: add_subdirectory(third_party/SDL) mirrors that path into the
         * build tree, so build/third_party existed and the walk stopped one
         * level too early. A dedicated marker cannot be shadowed that way.
         *
         * Probe first, strip second - and not the other way round, which cost
         * the starting directory its turn. That is invisible on Windows, where
         * the walk starts at reaktor.exe and the first strip is what turns a
         * file into the directory holding it. It is the whole of the bug
         * everywhere else, where the walk starts at the working directory: run
         * from the repository root, the one directory that carries the marker
         * was the one directory never tested, and the app came up with no
         * stylesheet and no icons. Probing the start costs Windows one fopen
         * of "...reaktor.exe/.reaktor-root", which cannot succeed, and every
         * probe after it is the one it always made. */
        for (;;) {
            char *slash;

            if (snprintf(probe, sizeof(probe), "%s/.reaktor-root", exe) < 0)
                return 0;
            probe[sizeof(probe) - 1] = '\0';
            {
                FILE *f = fopen(probe, "rb");
                if (f) { fclose(f); return copy_out(out, cap, exe); }
            }

            /* Stop once we have chewed back past the root. */
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

/* macOS-specific: libmalloc caches freed pages by default rather than
 * returning them to the kernel; on a resident-set-conscious app that is
 * pure footprint. Ask each zone to give back what it can. No-op on
 * Windows, Linux and Emscripten - glibc's arena and Windows's heap
 * decommit their own way, so there is nothing to prompt. */
void reaktor_release_free_memory(void)
{
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
    malloc_zone_pressure_relief(NULL, 0);
#endif
}

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

/* CPU time this process has used, all threads, in milliseconds. It is the
 * only honest measure of what a frame costs on a machine that rasterises in
 * software: the work lands on threads the app never created, so wall-clock
 * on the main thread misses nearly all of it. */
double reaktor_process_cpu_ms(void)
{
#if defined(_WIN32)
    FILETIME c, e, k, u;
    ULARGE_INTEGER ku, uu;
    if (!GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) return 0.0;
    ku.LowPart = k.dwLowDateTime; ku.HighPart = k.dwHighDateTime;
    uu.LowPart = u.dwLowDateTime; uu.HighPart = u.dwHighDateTime;
    return (double)(ku.QuadPart + uu.QuadPart) / 10000.0;   /* 100 ns units */
#elif defined(__EMSCRIPTEN__)
    return 0.0;      /* no process to ask about; the browser owns the threads */
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
#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
    {
        /* Resident pages, from the process table - all three BSDs answer this
         * one. None of them offers Windows' commit or Linux's Private_Clean
         * and Private_Dirty, and a number invented here would read as a
         * measurement, so priv stays 0 and the page renders it as unknown. */
#  if defined(__FreeBSD__)
        struct kinfo_proc kp;
        int    mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, 0 };
        size_t n = sizeof(kp);

        mib[3] = (int)getpid();
        if (sysctl(mib, 4, &kp, &n, NULL, 0) == 0 && n >= sizeof(kp))
            *rss = (size_t)kp.ki_rssize * (size_t)sysconf(_SC_PAGESIZE);
#  else
        /* NetBSD and OpenBSD make the identical request and answer with the
         * identical field; only the two names below differ. Both want the
         * struct's size and a count as the last two levels, so one call
         * serves whatever version of the struct the kernel has. */
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
