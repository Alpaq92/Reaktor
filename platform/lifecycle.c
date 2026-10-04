#include "lifecycle.h"
#include "reaktor/launch.h"

#if defined(_WIN32) && !defined(__EMSCRIPTEN__)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

static SDL_AtomicInt  g_ctrl_c;
static SDL_Semaphore *g_stopped;
static UINT           g_cp;
static int            g_allocated, g_own_ctrl_c;
static WNDPROC        g_prev_proc;

static int
usable(HANDLE h)
{
    return h && h != INVALID_HANDLE_VALUE;
}

static void
keep_or_open(DWORD which, HANDLE h, const char *dev, const char *mode, FILE *s)
{
    FILE *f;

    if (usable(h)) SetStdHandle(which, h);
    else freopen_s(&f, dev, mode, s);
}

static int
attach_parent(void)
{
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE err = GetStdHandle(STD_ERROR_HANDLE);

    if (usable(out) && usable(err)) return 1;
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) return 0;
    keep_or_open(STD_OUTPUT_HANDLE, out, "CONOUT$", "w", stdout);
    keep_or_open(STD_ERROR_HANDLE, err, "CONOUT$", "w", stderr);
    return 1;
}

static void
open_own(const char *title)
{
    HANDLE in  = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE err = GetStdHandle(STD_ERROR_HANDLE);

    if (!AllocConsole()) return;
    g_allocated = 1;
    keep_or_open(STD_OUTPUT_HANDLE, out, "CONOUT$", "w", stdout);
    keep_or_open(STD_ERROR_HANDLE, err, "CONOUT$", "w", stderr);
    keep_or_open(STD_INPUT_HANDLE, in, "CONIN$", "r", stdin);
    g_cp = GetConsoleOutputCP();
    SetConsoleOutputCP(CP_UTF8);
    if (title) {
        wchar_t w[256];

        if (MultiByteToWideChar(CP_UTF8, 0, title, -1, w, 256))
            SetConsoleTitleW(w);
    }
}

static BOOL WINAPI
console_ctrl(DWORD type)
{
    switch (type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
        /* A borrowed console's Ctrl+C belongs to the shell. */
        if (!g_own_ctrl_c) return TRUE;
        if (SDL_AddAtomicInt(&g_ctrl_c, 1) == 0) reaktor_console_quit();
        else reaktor_hard_quit(130);
        return TRUE;
    case CTRL_CLOSE_EVENT:
        /* Windows ends the process once this returns. */
        reaktor_hard_quit(0);
        if (g_stopped) SDL_WaitSemaphoreTimeout(g_stopped, 4500);
        return TRUE;
    default:
        return FALSE;
    }
}

void
reaktor_process_start(int console, const char *title)
{
    SDL_SetHintWithPriority(SDL_HINT_NO_SIGNAL_HANDLERS, "1", SDL_HINT_OVERRIDE);
    g_own_ctrl_c = GetConsoleWindow() != NULL;
    if (console == REAKTOR_CONSOLE_PARENT) attach_parent();
    else if (console == REAKTOR_CONSOLE_DEBUG && !attach_parent())
        open_own(title);
    if (g_allocated) g_own_ctrl_c = 1;
    g_stopped = SDL_CreateSemaphore(0);
    SetConsoleCtrlHandler(console_ctrl, TRUE);
}

static LRESULT CALLBACK
session_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_QUERYENDSESSION) return TRUE;
    if (msg == WM_ENDSESSION && wp) reaktor_session_end();
    return CallWindowProcW(g_prev_proc, h, msg, wp, lp);
}

void
reaktor_session_watch(SDL_Window *win)
{
    HWND h = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(win),
                                          SDL_PROP_WINDOW_WIN32_HWND_POINTER,
                                          NULL);

    if (!h || g_prev_proc) return;
    g_prev_proc = (WNDPROC)SetWindowLongPtrW(h, GWLP_WNDPROC,
                                             (LONG_PTR)session_proc);
}

void
reaktor_process_stopped(void)
{
    if (g_stopped) SDL_SignalSemaphore(g_stopped);
}

void
reaktor_process_end(void)
{
    SetConsoleCtrlHandler(console_ctrl, FALSE);
    if (g_allocated) {
        SetConsoleOutputCP(g_cp);
        FreeConsole();
        g_allocated = 0;
    }
}

#else

void reaktor_process_start(int console, const char *title) { (void)console; (void)title; }
void reaktor_session_watch(SDL_Window *win) { (void)win; }
void reaktor_process_stopped(void) {}
void reaktor_process_end(void) {}

#endif

#if defined(_WIN32) || defined(__EMSCRIPTEN__)

void reaktor_signal_watch(void) {}
void reaktor_signal_unwatch(void) {}

int
reaktor_signal_reason(int *again)
{
    *again = 0;
    return 0;
}

#else

#include <signal.h>

static volatile sig_atomic_t g_sigint, g_sigterm;
static int                   g_sigint_seen, g_wrapped;
static struct sigaction      g_old_int, g_old_term;

static void
on_signal(int sig)
{
    if (sig == SIGINT) {
        g_sigint++;
        g_old_int.sa_handler(sig);
    } else {
        g_sigterm = 1;
        g_old_term.sa_handler(sig);
    }
}

static int
wrap(int sig, struct sigaction *old)
{
    struct sigaction sa;

    if (sigaction(sig, NULL, old) != 0) return 0;
    if ((old->sa_flags & SA_SIGINFO) || old->sa_handler == SIG_DFL ||
        old->sa_handler == SIG_IGN)
        return 0;
    sa = *old;
    sa.sa_handler = on_signal;
    return sigaction(sig, &sa, NULL) == 0;
}

/* After SDL_Init, so SDL's handler is the one wrapped and still queues the quit. */
void
reaktor_signal_watch(void)
{
    g_wrapped = (wrap(SIGINT, &g_old_int) ? 1 : 0) | (wrap(SIGTERM, &g_old_term) ? 2 : 0);
}

int
reaktor_signal_reason(int *again)
{
    *again = 0;
    if (g_sigterm) {
        g_sigterm = 0;
        return REAKTOR_QUIT_SIGNAL + 1;
    }
    if (g_sigint != g_sigint_seen) {
        g_sigint_seen = g_sigint;
        *again = g_sigint > 1;
        return REAKTOR_QUIT_CONSOLE + 1;
    }
    return 0;
}

/* Before SDL_Quit, which only takes back a handler that is still its own. */
void
reaktor_signal_unwatch(void)
{
    if (g_wrapped & 1) sigaction(SIGINT, &g_old_int, NULL);
    if (g_wrapped & 2) sigaction(SIGTERM, &g_old_term, NULL);
    g_wrapped = 0;
}

#endif
