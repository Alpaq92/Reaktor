/* One running copy per app when .single_instance asks for it: a later launch
   hands its file arguments to the running copy and exits. Each message is a
   count of paths, then each path as a length and its bytes. */
#include "internal.h"

#ifndef __EMSCRIPTEN__
#define INSTANCE_PATH_MAX 4096
#define INSTANCE_PATHS_MAX 64
#define INSTANCE_READ_MS   2000

static void (*g_opened)(App *app, const char *path);

static void
deliver_path(App *app, void *user)
{
    if (g_opened) g_opened(app, (const char *)user);
    SDL_free(user);
}

static void
deliver_raise(App *app, void *user)
{
    (void)user;
    if (!app || !app->win) return;
    if (SDL_GetWindowFlags(app->win) & SDL_WINDOW_MINIMIZED) SDL_RestoreWindow(app->win);
    SDL_RaiseWindow(app->win);
}

/* What a read gives back: the paths, posted to the main thread in order. */
typedef int (*instance_read)(void *conn, void *buf, Uint32 n);

static void
receive(App *app, instance_read rd, void *conn)
{
    Uint32 count = 0, i, len;
    char  *path;

    if (!rd(conn, &count, sizeof count)) return;
    if (count > INSTANCE_PATHS_MAX) count = 0;
    for (i = 0; i < count; i++) {
        if (!rd(conn, &len, sizeof len) || len == 0 || len >= INSTANCE_PATH_MAX) break;
        if (!(path = (char *)SDL_malloc(len + 1))) break;
        if (!rd(conn, path, len)) {
            SDL_free(path);
            break;
        }
        path[len] = 0;
        reaktor_post(app, deliver_path, path);
    }
    reaktor_post(app, deliver_raise, NULL);
}

/* The message a later launch sends: its arguments, made absolute, since the
   running copy's working folder is its own. */
static int
message(int argc, char **argv, char **out, Uint32 *out_len)
{
    Uint32 count = 0, total = sizeof count, at;
    char  *abs[INSTANCE_PATHS_MAX];
    int    i;

    for (i = 1; i < argc && count < INSTANCE_PATHS_MAX; i++) {
        if (!argv[i] || !*argv[i]) continue;
        if (reaktor_path_absolute(argv[i])) {
            abs[count] = SDL_strdup(argv[i]);
        } else {
            char *cwd = SDL_GetCurrentDirectory();

            abs[count] = NULL;
            if (cwd && SDL_asprintf(&abs[count], "%s%s", cwd, argv[i]) < 0) abs[count] = NULL;
            SDL_free(cwd);
        }
        if (!abs[count]) continue;
        total += (Uint32)(sizeof(Uint32) + SDL_strlen(abs[count]));
        count++;
    }
    *out = (char *)SDL_malloc(total);
    if (*out) {
        SDL_memcpy(*out, &count, sizeof count);
        at = sizeof count;
        for (i = 0; i < (int)count; i++) {
            Uint32 len = (Uint32)SDL_strlen(abs[i]);

            SDL_memcpy(*out + at, &len, sizeof len);
            SDL_memcpy(*out + at + sizeof len, abs[i], len);
            at += (Uint32)sizeof len + len;
        }
    }
    for (i = 0; i < (int)count; i++) SDL_free(abs[i]);
    *out_len = total;
    return *out != NULL;
}
#endif

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static HANDLE        g_mutex;
static char          g_pipe[256];
static SDL_Thread   *g_thread;
static HANDLE        g_stop;

typedef struct pipe_conn { HANDLE pipe, event; } pipe_conn;

union token_user_buf {
    TOKEN_USER user;
    BYTE       raw[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
};

static void
pipe_arm(OVERLAPPED *o, HANDLE event)
{
    SDL_zero(*o);
    o->hEvent = event;
    ResetEvent(event);
}

static int
pipe_wait(HANDLE pipe, OVERLAPPED *o, DWORD ms, DWORD *got)
{
    HANDLE wait[2];

    wait[0] = o->hEvent;
    wait[1] = g_stop;
    if (WaitForMultipleObjects(2, wait, FALSE, ms) == WAIT_OBJECT_0 &&
        GetOverlappedResult(pipe, o, got, FALSE))
        return 1;
    CancelIoEx(pipe, o);
    GetOverlappedResult(pipe, o, got, TRUE);
    return 0;
}

static int
pipe_read(void *conn, void *buf, Uint32 n)
{
    pipe_conn *c = (pipe_conn *)conn;
    char      *p = (char *)buf;

    while (n) {
        OVERLAPPED o;
        DWORD      got = 0;

        pipe_arm(&o, c->event);
        if (!ReadFile(c->pipe, p, n, NULL, &o) && GetLastError() != ERROR_IO_PENDING)
            return 0;
        if (!pipe_wait(c->pipe, &o, INSTANCE_READ_MS, &got) || !got) return 0;
        p += got;
        n -= got;
    }
    return 1;
}

static void
names(void)
{
    DWORD    session = 0;
    unsigned h = reaktor_fnv(REAKTOR_FNV, reaktor_app_ident());

    ProcessIdToSessionId(GetCurrentProcessId(), &session);
    SDL_snprintf(g_pipe, sizeof g_pipe, "\\\\.\\pipe\\reaktor.%lu.%08x", (unsigned long)session, h);
}

static int
token_user(HANDLE process, union token_user_buf *out)
{
    HANDLE token;
    DWORD  len = 0;
    int    ok;

    if (!OpenProcessToken(process, TOKEN_QUERY, &token)) return 0;
    ok = GetTokenInformation(token, TokenUser, out, sizeof *out, &len);
    CloseHandle(token);
    return ok;
}

static int
same_user(ULONG pid)
{
    union token_user_buf mine, theirs;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    int    ok;

    if (!process) return 0;
    ok = token_user(process, &theirs) && token_user(GetCurrentProcess(), &mine) &&
         EqualSid(mine.user.User.Sid, theirs.user.User.Sid);
    CloseHandle(process);
    return ok;
}

int
reaktor_instance_claim(int argc, char **argv)
{
    char     mutex[96], *msg = NULL;
    Uint32   len = 0;
    HANDLE   pipe = INVALID_HANDLE_VALUE;
    ULONG    server = 0;
    DWORD    wrote = 0;
    int      tries;

    names();
    SDL_snprintf(mutex, sizeof mutex, "Local\\reaktor.%08x",
                 reaktor_fnv(REAKTOR_FNV, reaktor_app_ident()));
    g_mutex = CreateMutexA(NULL, TRUE, mutex);
    if (g_mutex && GetLastError() != ERROR_ALREADY_EXISTS) return 1;
    if (g_mutex) CloseHandle(g_mutex);
    g_mutex = NULL;

    /* The first copy may still be starting its listener. */
    for (tries = 0; tries < 50; tries++) {
        pipe = CreateFileA(g_pipe, GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                           SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, NULL);
        if (pipe != INVALID_HANDLE_VALUE) break;
        if (GetLastError() == ERROR_PIPE_BUSY) WaitNamedPipeA(g_pipe, 200);
        else Sleep(100);
    }
    if (pipe == INVALID_HANDLE_VALUE) {
        SDL_Log("single instance: the running copy did not answer; starting another");
        return 1;
    }
    if (!GetNamedPipeServerProcessId(pipe, &server) || !same_user(server)) {
        SDL_Log("single instance: %s belongs to another user; starting another", g_pipe);
        CloseHandle(pipe);
        return 1;
    }
    AllowSetForegroundWindow(server);
    if (message(argc, argv, &msg, &len)) WriteFile(pipe, msg, len, &wrote, NULL);
    SDL_free(msg);
    CloseHandle(pipe);
    return 0;
}

static int SDLCALL
listen_thread(void *data)
{
    App      *app = (App *)data;
    pipe_conn c;

    c.event = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!c.event) return 0;
    while (WaitForSingleObject(g_stop, 0) != WAIT_OBJECT_0) {
        OVERLAPPED o;
        DWORD      none = 0;
        int        connected;

        c.pipe = CreateNamedPipeA(g_pipe, PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED |
                                      FILE_FLAG_FIRST_PIPE_INSTANCE,
                                  PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
                                      PIPE_REJECT_REMOTE_CLIENTS,
                                  PIPE_UNLIMITED_INSTANCES, 0, 64 * 1024, 0, NULL);
        if (c.pipe == INVALID_HANDLE_VALUE) {
            SDL_Log("single instance: %s is held by another process; not listening", g_pipe);
            break;
        }
        pipe_arm(&o, c.event);
        connected = ConnectNamedPipe(c.pipe, &o) ||
                    GetLastError() == ERROR_PIPE_CONNECTED ||
                    (GetLastError() == ERROR_IO_PENDING &&
                     pipe_wait(c.pipe, &o, INFINITE, &none));
        if (connected) receive(app, pipe_read, &c);
        DisconnectNamedPipe(c.pipe);
        CloseHandle(c.pipe);
    }
    CloseHandle(c.event);
    return 0;
}

void
reaktor_instance_listen(App *app, void (*opened)(App *app, const char *path))
{
    g_opened = opened;
    if (!g_mutex || g_thread) return;
    if (!g_stop && !(g_stop = CreateEventA(NULL, TRUE, FALSE, NULL))) return;
    g_thread = SDL_CreateThread(listen_thread, "reaktor-instance", app);
}

void
reaktor_instance_stop(void)
{
    if (g_thread) {
        SetEvent(g_stop);
        SDL_WaitThread(g_thread, NULL);
        g_thread = NULL;
    }
    if (g_stop) {
        CloseHandle(g_stop);
        g_stop = NULL;
    }
    if (g_mutex) {
        ReleaseMutex(g_mutex);
        CloseHandle(g_mutex);
        g_mutex = NULL;
    }
}

#elif !defined(__EMSCRIPTEN__)
#include <errno.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

static int           g_listen = -1;
static char          g_sock[sizeof(((struct sockaddr_un *)0)->sun_path)];
static SDL_Thread   *g_thread;
static SDL_AtomicInt g_stop;

static int
sock_read(void *conn, void *buf, Uint32 n)
{
    char *p = (char *)buf;

    while (n) {
        ssize_t got = read(*(int *)conn, p, n);

        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) return 0;
        p += got;
        n -= (Uint32)got;
    }
    return 1;
}

/* Beside the app's settings when that fits a socket's path, else in a /tmp
   folder only this user can reach. */
static int
sock_path(void)
{
    char       *dir = SDL_GetPrefPath(NULL, reaktor_app_ident());
    char        own[64];
    struct stat st;
    int         n = -1;

    if (dir) n = SDL_snprintf(g_sock, sizeof g_sock, "%sinstance.sock", dir);
    SDL_free(dir);
    if (n > 0 && (size_t)n < sizeof g_sock) return 1;

    SDL_snprintf(own, sizeof own, "/tmp/reaktor-%lu", (unsigned long)getuid());
    if (mkdir(own, 0700) != 0 && errno != EEXIST) return 0;
    if (lstat(own, &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != getuid() ||
        (st.st_mode & 077)) {
        SDL_Log("single instance: %s is not this user's own; not listening", own);
        return 0;
    }
    n = SDL_snprintf(g_sock, sizeof g_sock, "%s/%08x.sock", own,
                     reaktor_fnv(REAKTOR_FNV, reaktor_app_ident()));
    return n > 0 && (size_t)n < sizeof g_sock;
}

static int
sock_connect(void)
{
    struct sockaddr_un a;
    int                s = socket(AF_UNIX, SOCK_STREAM, 0);

    if (s < 0) return -1;
    SDL_zero(a);
    a.sun_family = AF_UNIX;
    SDL_strlcpy(a.sun_path, g_sock, sizeof a.sun_path);
    if (connect(s, (struct sockaddr *)&a, sizeof a) == 0) return s;
    close(s);
    return -1;
}

int
reaktor_instance_claim(int argc, char **argv)
{
    struct sockaddr_un a;
    char              *msg = NULL;
    Uint32             len = 0;
    int                s, tries;

    if (!sock_path()) return 1;
    for (tries = 0; tries < 3; tries++) {
        g_listen = socket(AF_UNIX, SOCK_STREAM, 0);
        if (g_listen < 0) return 1;
        SDL_zero(a);
        a.sun_family = AF_UNIX;
        SDL_strlcpy(a.sun_path, g_sock, sizeof a.sun_path);
        if (bind(g_listen, (struct sockaddr *)&a, sizeof a) == 0 && listen(g_listen, 8) == 0)
            return 1;
        close(g_listen);
        g_listen = -1;
        if (errno != EADDRINUSE) return 1;

        s = sock_connect();
        if (s >= 0) {
            if (message(argc, argv, &msg, &len)) {
                ssize_t put = write(s, msg, len);

                (void)put;
            }
            SDL_free(msg);
            close(s);
            return 0;
        }
        /* Nobody listens: a copy that crashed left its socket behind. */
        unlink(g_sock);
    }
    return 1;
}

static int SDLCALL
listen_thread(void *data)
{
    App           *app = (App *)data;
    struct timeval tv;

    tv.tv_sec  = INSTANCE_READ_MS / 1000;
    tv.tv_usec = (INSTANCE_READ_MS % 1000) * 1000;
    while (!SDL_GetAtomicInt(&g_stop)) {
        int c = accept(g_listen, NULL, NULL);

        if (c < 0) {
            if (errno == EINTR) continue;
            break;
        }
        setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        if (!SDL_GetAtomicInt(&g_stop)) receive(app, sock_read, &c);
        close(c);
    }
    return 0;
}

void
reaktor_instance_listen(App *app, void (*opened)(App *app, const char *path))
{
    g_opened = opened;
    if (g_listen < 0 || g_thread) return;
    g_thread = SDL_CreateThread(listen_thread, "reaktor-instance", app);
}

void
reaktor_instance_stop(void)
{
    if (g_listen < 0) return;
    SDL_SetAtomicInt(&g_stop, 1);
    /* The listener waits in accept; one connection releases it. */
    if (g_thread) {
        int s = sock_connect();

        if (s >= 0) close(s);
        SDL_WaitThread(g_thread, NULL);
        g_thread = NULL;
    }
    close(g_listen);
    g_listen = -1;
    unlink(g_sock);
}

#else
int
reaktor_instance_claim(int argc, char **argv)
{
    (void)argc; (void)argv;
    return 1;
}

void
reaktor_instance_listen(App *app, void (*opened)(App *app, const char *path))
{
    (void)app; (void)opened;
}

void
reaktor_instance_stop(void)
{
}
#endif
