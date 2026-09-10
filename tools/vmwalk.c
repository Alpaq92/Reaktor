#define PSAPI_VERSION 2
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    B_STACK,
    B_HEAP,
    B_JIT,
    B_PRIV_OTHER,
    B_IMAGE_COW,
    B_MAPPED_PRIV,
    B_COUNT
};

static const char *const g_bucket[B_COUNT] = {
    "thread stacks (commit)",
    "private read/write (heap, arenas)",
    "private executable (JIT/shader)",
    "private, other protection",
    "image pages, copy-on-write dirty",
    "mapped pages, private dirty"
};

#define WS_BATCH 4096

typedef struct module_ent {
    unsigned char *base;
    SIZE_T         size;
    SIZE_T         dirty;
    char           name[64];
} module_ent;

#define MODULES_MAX 256

static module_ent g_mod[MODULES_MAX];
static int        g_mod_n;

static double mb(SIZE_T bytes) { return (double)bytes / 1048576.0; }

static void collect_modules(HANDLE proc)
{
    HMODULE mods[MODULES_MAX];
    DWORD needed = 0;
    unsigned i, n;

    if (!EnumProcessModulesEx(proc, mods, sizeof(mods), &needed,
                              LIST_MODULES_ALL))
        return;

    n = needed / sizeof(HMODULE);
    if (n > MODULES_MAX) n = MODULES_MAX;

    for (i = 0; i < n; i++) {
        MODULEINFO mi;
        char path[MAX_PATH];
        const char *leaf;

        if (!GetModuleInformation(proc, mods[i], &mi, sizeof(mi))) continue;
        if (!GetModuleFileNameExA(proc, mods[i], path, sizeof(path))) continue;

        leaf = strrchr(path, '\\');
        leaf = leaf ? leaf + 1 : path;

        g_mod[g_mod_n].base = (unsigned char *)mi.lpBaseOfDll;
        g_mod[g_mod_n].size = mi.SizeOfImage;
        g_mod[g_mod_n].dirty = 0;
        strncpy(g_mod[g_mod_n].name, leaf, sizeof(g_mod[0].name) - 1);
        g_mod[g_mod_n].name[sizeof(g_mod[0].name) - 1] = '\0';
        g_mod_n++;
    }
}

static module_ent *module_at(unsigned char *addr)
{
    int i;
    for (i = 0; i < g_mod_n; i++)
        if (addr >= g_mod[i].base && addr < g_mod[i].base + g_mod[i].size)
            return &g_mod[i];
    return NULL;
}

static SIZE_T private_dirty_bytes(HANDLE proc, unsigned char *base, SIZE_T len,
                                  SIZE_T page)
{
    static PSAPI_WORKING_SET_EX_INFORMATION info[WS_BATCH];
    SIZE_T done = 0, dirty = 0;

    while (done < len) {
        SIZE_T pages = (len - done) / page;
        SIZE_T i;

        if (pages > WS_BATCH) pages = WS_BATCH;
        if (pages == 0) break;

        for (i = 0; i < pages; i++)
            info[i].VirtualAddress = base + done + i * page;

        if (!QueryWorkingSetEx(proc, info,
                               (DWORD)(pages * sizeof(info[0]))))
            return dirty;

        for (i = 0; i < pages; i++)
            if (info[i].VirtualAttributes.Valid &&
                !info[i].VirtualAttributes.Shared)
                dirty += page;

        done += pages * page;
    }
    return dirty;
}

typedef LONG (WINAPI *fn_nt_query_thread)(HANDLE, int, PVOID, ULONG, PULONG);

static const char *thread_owner(DWORD tid, unsigned char **out_start)
{
    static fn_nt_query_thread query;
    static int looked_up;
    HANDLE th;
    unsigned char *start = NULL;
    module_ent *m;

    *out_start = NULL;
    if (!looked_up) {
        HMODULE nt = GetModuleHandleA("ntdll.dll");
        if (nt)
            query = (fn_nt_query_thread)(void *)
                    GetProcAddress(nt, "NtQueryInformationThread");
        looked_up = 1;
    }
    if (!query) return "?";

    th = OpenThread(THREAD_QUERY_INFORMATION, FALSE, tid);
    if (!th) return "?";
    if (query(th, 9  , &start,
              sizeof(start), NULL) != 0)
        start = NULL;
    CloseHandle(th);

    *out_start = start;
    if (!start) return "?";
    m = module_at(start);
    return m ? m->name : "(not in any module)";
}

static void list_threads(DWORD pid)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te;

    if (snap == INVALID_HANDLE_VALUE) return;
    printf("\n  threads, by the module that started them:\n");
    te.dwSize = sizeof(te);
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != pid) continue;
            {
                unsigned char *start = NULL;
                const char *owner = thread_owner(te.th32ThreadID, &start);
                printf("  tid %-8lu %-34s %p\n",
                       (unsigned long)te.th32ThreadID, owner, (void *)start);
            }
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
}

static int count_threads(DWORD pid)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te;
    int n = 0;

    if (snap == INVALID_HANDLE_VALUE) return -1;
    te.dwSize = sizeof(te);
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID == pid) n++;
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    return n;
}

static int allocation_has_guard(HANDLE proc, unsigned char *alloc_base)
{
    static unsigned char *cached_base;
    static int cached_answer;
    MEMORY_BASIC_INFORMATION mbi;
    unsigned char *p = alloc_base;
    int found = 0;

    if (!alloc_base) return 0;
    if (alloc_base == cached_base) return cached_answer;

    while (VirtualQueryEx(proc, p, &mbi, sizeof(mbi)) == sizeof(mbi)) {
        if ((unsigned char *)mbi.AllocationBase != alloc_base) break;
        if (mbi.State == MEM_COMMIT && (mbi.Protect & PAGE_GUARD)) {
            found = 1;
            break;
        }
        if (mbi.RegionSize == 0) break;
        p = (unsigned char *)mbi.BaseAddress + mbi.RegionSize;
    }
    cached_base = alloc_base;
    cached_answer = found;
    return found;
}

int main(int argc, char **argv)
{
    SIZE_T bucket[B_COUNT];
    SIZE_T reserved = 0, image_total = 0, mapped_total = 0;
    SIZE_T stacks_reserved = 0;
    int    stack_allocs = 0, threads;
    SYSTEM_INFO si;
    HANDLE proc;
    MEMORY_BASIC_INFORMATION mbi;
    unsigned char *addr = NULL;
    PROCESS_MEMORY_COUNTERS_EX pmc;
    SIZE_T attributed = 0, priv_total = 0;
    DWORD pid;
    int csv = 0, i;

    if (argc < 2) {
        fprintf(stderr, "usage: vmwalk <pid> [--csv]\n");
        return 2;
    }
    pid = (DWORD)strtoul(argv[1], NULL, 10);
    for (i = 2; i < argc; i++)
        if (strcmp(argv[i], "--csv") == 0) csv = 1;

    memset(bucket, 0, sizeof(bucket));
    GetSystemInfo(&si);

    proc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!proc) {
        fprintf(stderr, "vmwalk: cannot open pid %lu (error %lu)\n",
                (unsigned long)pid, (unsigned long)GetLastError());
        return 1;
    }

    collect_modules(proc);
    threads = count_threads(pid);

    while (addr < (unsigned char *)si.lpMaximumApplicationAddress &&
           VirtualQueryEx(proc, addr, &mbi, sizeof(mbi)) == sizeof(mbi)) {
        SIZE_T len = mbi.RegionSize;

        if (mbi.State == MEM_RESERVE) {
            reserved += len;
            if (allocation_has_guard(proc, (unsigned char *)mbi.AllocationBase))
                stacks_reserved += len;
        } else if (mbi.State == MEM_COMMIT) {
            if (mbi.Type == MEM_PRIVATE) {
                DWORD prot = mbi.Protect & ~(DWORD)(PAGE_GUARD | PAGE_NOCACHE |
                                                    PAGE_WRITECOMBINE);

                if ((mbi.Protect & PAGE_GUARD) ||
                    allocation_has_guard(proc,
                                         (unsigned char *)mbi.AllocationBase)) {
                    if (mbi.Protect & PAGE_GUARD) stack_allocs++;
                    bucket[B_STACK] += len;
                } else if (prot == PAGE_EXECUTE || prot == PAGE_EXECUTE_READ ||
                           prot == PAGE_EXECUTE_READWRITE ||
                           prot == PAGE_EXECUTE_WRITECOPY) {
                    bucket[B_JIT] += len;
                } else if (prot == PAGE_READWRITE || prot == PAGE_WRITECOPY) {
                    bucket[B_HEAP] += len;
                } else {
                    bucket[B_PRIV_OTHER] += len;
                }
            } else if (mbi.Type == MEM_IMAGE) {
                SIZE_T d;
                image_total += len;
                d = private_dirty_bytes(proc, (unsigned char *)mbi.BaseAddress,
                                        len, si.dwPageSize);
                bucket[B_IMAGE_COW] += d;
                if (d) {
                    module_ent *m = module_at((unsigned char *)mbi.BaseAddress);
                    if (m) m->dirty += d;
                }
            } else if (mbi.Type == MEM_MAPPED) {
                mapped_total += len;
                bucket[B_MAPPED_PRIV] +=
                    private_dirty_bytes(proc, (unsigned char *)mbi.BaseAddress,
                                        len, si.dwPageSize);
            }
        }

        addr = (unsigned char *)mbi.BaseAddress + len;
        if (len == 0) break;
    }

    memset(&pmc, 0, sizeof(pmc));
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(proc, (PROCESS_MEMORY_COUNTERS *)&pmc,
                             sizeof(pmc)))
        priv_total = pmc.PrivateUsage;

    for (i = 0; i < B_COUNT; i++) attributed += bucket[i];

    if (csv) {
        printf("bucket,bytes\n");
        for (i = 0; i < B_COUNT; i++)
            printf("%s,%llu\n", g_bucket[i], (unsigned long long)bucket[i]);
        printf("PrivateUsage,%llu\n", (unsigned long long)priv_total);
        printf("attributed,%llu\n", (unsigned long long)attributed);
        printf("reserved_not_charged,%llu\n", (unsigned long long)reserved);
    } else {
        printf("pid %lu   %d threads\n\n", (unsigned long)pid, threads);
        printf("  %-34s %10s\n", "bucket", "MB");
        printf("  %-34s %10s\n", "----------------------------------",
               "----------");
        for (i = 0; i < B_COUNT; i++)
            printf("  %-34s %10.3f\n", g_bucket[i], mb(bucket[i]));
        printf("  %-34s %10.3f\n", "attributed", mb(attributed));
        printf("  %-34s %10.3f\n", "PrivateUsage (ground truth)",
               mb(priv_total));
        printf("  %-34s %10.3f\n", "unattributed",
               mb(priv_total) - mb(attributed));

        printf("\n  not charged to private bytes:\n");
        printf("  %-34s %10.3f\n", "reserved (incl. stack reserve)",
               mb(reserved));
        printf("  %-34s %10.3f\n", "  of which stack reserve",
               mb(stacks_reserved));
        printf("  %-34s %10.3f\n", "image, mapped in", mb(image_total));
        printf("  %-34s %10.3f\n", "mapped files", mb(mapped_total));
        printf("  guard-page allocations: %d, against %d threads\n",
               stack_allocs, threads);
        list_threads(pid);

        printf("\n  image pages written since load, by module:\n");
        {
            char shown[MODULES_MAX] = { 0 };
            int printed;

            for (printed = 0; printed < g_mod_n; printed++) {
                int j, top = -1;
                SIZE_T best = 0;
                for (j = 0; j < g_mod_n; j++)
                    if (!shown[j] && g_mod[j].dirty > best) {
                        best = g_mod[j].dirty;
                        top = j;
                    }
                if (top < 0) break;
                printf("  %-34s %10.3f\n", g_mod[top].name,
                       mb(g_mod[top].dirty));
                shown[top] = 1;
            }
        }
    }

    CloseHandle(proc);
    return 0;
}
