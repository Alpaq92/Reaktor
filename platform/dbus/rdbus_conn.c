/* The connection half of rdbus.h: addresses, EXTERNAL authentication, Hello,
   the socket, and dispatch. Ported from minidbus's conn/ (MIT; see NOTICE.md). */

#include "rdbus_internal.h"

#include <SDL3/SDL.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <unistd.h>

#if !defined(MSG_NOSIGNAL) && !defined(SO_NOSIGPIPE)
#include <pthread.h>
#include <signal.h>
#endif

#define OPEN_MS          5000
#define BUS_CALL_MS      5000
#define DEFAULT_CALL_MS  25000
#define MAX_MESSAGE      (128u * 1024u * 1024u)
#define MAX_OUTGOING     MAX_MESSAGE
#define READ_CHUNK       65536
#define READ_ROUNDS      64
#define MAX_QUEUED       4096
#define NEVER            (~(Uint64)0)

#if defined(O_CLOEXEC)
#define CLOEXEC O_CLOEXEC
#else
#define CLOEXEC 0
#endif

#define ERR_NO_MEMORY   "org.freedesktop.DBus.Error.NoMemory"
#define ERR_BAD_ADDRESS "org.freedesktop.DBus.Error.BadAddress"
#define ERR_NO_SERVER   "org.freedesktop.DBus.Error.NoServer"
#define ERR_AUTH_FAILED "org.freedesktop.DBus.Error.AuthFailed"
#define ERR_TIMEOUT     "org.freedesktop.DBus.Error.Timeout"
#define IFACE_PEER      "org.freedesktop.DBus.Peer"

enum { H_FILTER, H_PATH, H_FALLBACK };

typedef struct handler {
    int           kind;
    char         *path;
    rdbus_handler fn;
    void         *user;
} handler;

typedef struct queued {
    struct queued *next;
    rdbus_msg     *m;
} queued;

typedef struct waiter {
    struct waiter *next;
    uint32_t       serial;
    const char    *dest;
    rdbus_msg     *reply;
} waiter;

struct rdbus_conn {
    SDL_AtomicInt  refs;
    SDL_Mutex     *lock;
    SDL_Condition *cond;
    int            fd;
    int            open;
    int            reading;
    uint32_t       serial;
    char          *unique;

    uint8_t       *out;
    size_t         out_at, out_len, out_cap;
    uint8_t       *in;
    size_t         in_len, in_cap, need;

    queued        *head, *tail;
    int            nqueued;
    waiter        *waiters;
    handler       *handlers;
    int            nhandlers, chandlers;
};

static SDL_InitState g_session_init;
static SDL_Mutex    *g_session_lock;
static rdbus_conn   *g_session;

static int
ms_left(Uint64 deadline)
{
    Uint64 now = SDL_GetTicks();

    if (now >= deadline) return 0;
    return deadline - now > (Uint64)INT_MAX ? INT_MAX : (int)(deadline - now);
}

static Uint64
deadline_in(int ms)
{
    return ms < 0 ? NEVER : SDL_GetTicks() + (Uint64)ms;
}

static int
would_block(int e)
{
#if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
    if (e == EWOULDBLOCK) return 1;
#endif
    return e == EAGAIN;
}

/* 1 when ready (or hung up), 0 at the deadline, -1 on a poll error. */
static int
wait_fd(int fd, short events, Uint64 deadline)
{
    for (;;) {
        struct pollfd p;
        int r, ms = deadline == NEVER ? -1 : ms_left(deadline);

        if (ms == 0) return 0;
        p.fd = fd;
        p.events = events;
        p.revents = 0;
        r = poll(&p, 1, ms);
        if (r > 0) return 1;
        if (r == 0) return 0;
        if (errno != EINTR && errno != EAGAIN) return -1;
    }
}

static ssize_t
send_bytes(int fd, const void *buf, size_t len)
{
#if defined(MSG_NOSIGNAL)
    return send(fd, buf, len, MSG_NOSIGNAL);
#elif defined(SO_NOSIGPIPE)
    return send(fd, buf, len, 0);
#else
    /* SIGPIPE from send goes to the calling thread: block it for the call and
       take back only one this call raised. */
    sigset_t pipe_set, pending, old;
    ssize_t n;
    int was, saved, sig;

    sigemptyset(&pipe_set);
    sigaddset(&pipe_set, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &pipe_set, &old);
    sigpending(&pending);
    was = sigismember(&pending, SIGPIPE);
    n = send(fd, buf, len, 0);
    saved = errno;
    if (n < 0 && saved == EPIPE && !was) {
        sigpending(&pending);
        if (sigismember(&pending, SIGPIPE)) sigwait(&pipe_set, &sig);
    }
    pthread_sigmask(SIG_SETMASK, &old, NULL);
    errno = saved;
    return n;
#endif
}

/* Addresses */

static int
unhex(int ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

/* The decoded length, or -1 for a bad escape or a value longer than cap. */
static long
percent_decode(const char *s, size_t n, char *out, size_t cap)
{
    size_t i = 0, o = 0;

    while (i < n) {
        int ch = (unsigned char)s[i];

        if (ch == '%') {
            int hi, lo;

            if (i + 2 >= n) return -1;
            hi = unhex((unsigned char)s[i + 1]);
            lo = unhex((unsigned char)s[i + 2]);
            if (hi < 0 || lo < 0) return -1;
            ch = hi << 4 | lo;
            i += 3;
        } else {
            i++;
        }
        if (o >= cap) return -1;
        out[o++] = (char)ch;
    }
    return (long)o;
}

static int
open_socket(void)
{
    int fd;
#if defined(SO_NOSIGPIPE)
    int one = 1;
#endif

#if defined(SOCK_CLOEXEC) && defined(SOCK_NONBLOCK)
    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) return -1;
#else
    int fl;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0 || (fl = fcntl(fd, F_GETFL)) < 0 ||
        fcntl(fd, F_SETFL, fl | O_NONBLOCK) < 0) {
        close(fd);
        return -1;
    }
#endif
#if defined(SO_NOSIGPIPE)
    if (setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one)) < 0) {
        close(fd);
        return -1;
    }
#endif
    return fd;
}

static int
connect_unix(const char *name, size_t len, int abstract, Uint64 deadline, rdbus_error *err)
{
    struct sockaddr_un sa;
    socklen_t salen;
    int fd, e;

    SDL_zero(sa);
    sa.sun_family = AF_UNIX;
    if (len + 1 > sizeof(sa.sun_path)) {
        rdbus__error_set(err, ERR_BAD_ADDRESS, "socket name too long");
        return -1;
    }
    if (abstract) {
        SDL_memcpy(sa.sun_path + 1, name, len);
        salen = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + len);
    } else {
        if (!len || SDL_strnlen(name, len) != len) {
            rdbus__error_set(err, ERR_BAD_ADDRESS, "bad socket path");
            return -1;
        }
        SDL_memcpy(sa.sun_path, name, len);
        salen = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + len + 1);
    }

    if ((fd = open_socket()) < 0) {
        rdbus__error_set(err, RDBUS_ERROR_FAILED, "socket: %s", strerror(errno));
        return -1;
    }
    for (;;) {
        int soerr = 0, w;
        socklen_t sl = sizeof(soerr);

        if (connect(fd, (struct sockaddr *)&sa, salen) == 0) return fd;
        e = errno;
        if (e == EISCONN) return fd;
        if (e == EAGAIN && ms_left(deadline) > 0) {
            /* Linux: the listener's backlog is full; nothing is in progress. */
            SDL_Delay(10);
            continue;
        }
        if (e != EINPROGRESS && e != EALREADY && e != EINTR) break;
        w = wait_fd(fd, POLLOUT, deadline);
        if (w == 0) {
            close(fd);
            rdbus__error_set(err, ERR_TIMEOUT, "timed out connecting to the bus");
            return -1;
        }
        if (w < 0) {
            e = errno;
            break;
        }
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl) < 0) {
            e = errno;
            break;
        }
        if (soerr == 0 || soerr == EISCONN) return fd;
        e = soerr;
        break;
    }
    close(fd);
    rdbus__error_set(err, ERR_NO_SERVER, "cannot connect to %s%.*s: %s", abstract ? "@" : "",
              (int)len, name, strerror(e));
    return -1;
}

static int
connect_entry(const char *entry, size_t n, Uint64 deadline, rdbus_error *err)
{
    char name[sizeof(((struct sockaddr_un *)0)->sun_path)];
    const char *kv, *stop = entry + n, *val = NULL;
    size_t vlen = 0;
    int abstract = 0;
    long len;

    if (n < 5 || SDL_strncmp(entry, "unix:", 5) != 0) {
        rdbus__error_set(err, ERR_BAD_ADDRESS, "unsupported transport in \"%.*s\"", (int)n, entry);
        return -1;
    }
    kv = entry + 5;
    while (kv < stop) {
        const char *ke = kv;

        while (ke < stop && *ke != ',') ke++;
        if (!val && ke - kv > 5 && SDL_strncmp(kv, "path=", 5) == 0) {
            val = kv + 5;
            vlen = (size_t)(ke - val);
            abstract = 0;
        } else if (!val && ke - kv > 9 && SDL_strncmp(kv, "abstract=", 9) == 0) {
            val = kv + 9;
            vlen = (size_t)(ke - val);
            abstract = 1;
        }
        if (ke == stop) break;
        kv = ke + 1;
    }
    if (!val) {
        rdbus__error_set(err, ERR_BAD_ADDRESS, "no path in \"%.*s\"", (int)n, entry);
        return -1;
    }
#ifndef __linux__
    if (abstract) {
        rdbus__error_set(err, ERR_BAD_ADDRESS, "abstract sockets are Linux's alone");
        return -1;
    }
#endif
    len = percent_decode(val, vlen, name, sizeof(name) - 1);
    if (len < 0) {
        rdbus__error_set(err, ERR_BAD_ADDRESS, "bad value in \"%.*s\"", (int)n, entry);
        return -1;
    }
    return connect_unix(name, (size_t)len, abstract, deadline, err);
}

static int
connect_address(const char *address, Uint64 deadline, rdbus_error *err)
{
    const char *p = address;
    rdbus_error last;
    int fd = -1;

    SDL_zero(last);
    for (;;) {
        const char *end = SDL_strchr(p, ';');
        size_t n = end ? (size_t)(end - p) : SDL_strlen(p);

        if (n) {
            SDL_zero(last);
            if ((fd = connect_entry(p, n, deadline, &last)) >= 0) return fd;
        }
        if (!end) break;
        p = end + 1;
        if (!ms_left(deadline)) {
            SDL_zero(last);
            rdbus__error_set(&last, ERR_TIMEOUT, "timed out connecting to the bus");
            break;
        }
    }
    if (last.set)
        rdbus__error_set(err, last.name, "%s", last.message);
    else
        rdbus__error_set(err, ERR_BAD_ADDRESS, "empty bus address");
    return -1;
}

static int
connect_session(Uint64 deadline, rdbus_error *err)
{
    const char *addr = SDL_getenv("DBUS_SESSION_BUS_ADDRESS");
    const char *dir;
    char path[sizeof(((struct sockaddr_un *)0)->sun_path) + 8];
    int n;

    if (addr && *addr) return connect_address(addr, deadline, err);
    dir = SDL_getenv("XDG_RUNTIME_DIR");
    if (!dir || !*dir) {
        rdbus__error_set(err, ERR_NO_SERVER,
                  "no session bus: DBUS_SESSION_BUS_ADDRESS and XDG_RUNTIME_DIR are unset");
        return -1;
    }
    n = SDL_snprintf(path, sizeof(path), "%s/bus", dir);
    if (n < 0 || (size_t)n >= sizeof(path)) {
        rdbus__error_set(err, ERR_BAD_ADDRESS, "XDG_RUNTIME_DIR is too long");
        return -1;
    }
    return connect_unix(path, (size_t)n, 0, deadline, err);
}

/* Authentication */

static int
write_all(int fd, const char *buf, size_t len, Uint64 deadline)
{
    while (len) {
        ssize_t n = send_bytes(fd, buf, len);

        if (n > 0) {
            buf += n;
            len -= (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && would_block(errno) && wait_fd(fd, POLLOUT, deadline) > 0) continue;
        return 0;
    }
    return 1;
}

/* FreeBSD's and DragonFly's dbus-daemon may take the credentials from an
   SCM_CREDS message sent with the NUL byte; the kernel fills them in. */
static int
send_nul(int fd, Uint64 deadline)
{
#if defined(SCM_CREDS) && (defined(__FreeBSD__) || defined(__DragonFly__))
    union {
        struct cmsghdr h;
        char           buf[CMSG_SPACE(sizeof(struct cmsgcred))];
    } ctl;
    struct msghdr mh;
    struct iovec iov;
    char nul = 0;
    int flags = 0;

#if defined(MSG_NOSIGNAL)
    flags = MSG_NOSIGNAL;
#endif
    for (;;) {
        ssize_t n;

        SDL_zero(ctl);
        SDL_zero(mh);
        iov.iov_base = &nul;
        iov.iov_len = 1;
        mh.msg_iov = &iov;
        mh.msg_iovlen = 1;
        mh.msg_control = ctl.buf;
        mh.msg_controllen = sizeof(ctl.buf);
        ctl.h.cmsg_len = CMSG_LEN(sizeof(struct cmsgcred));
        ctl.h.cmsg_level = SOL_SOCKET;
        ctl.h.cmsg_type = SCM_CREDS;
        n = sendmsg(fd, &mh, flags);
        if (n == 1) return 1;
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && would_block(errno) && wait_fd(fd, POLLOUT, deadline) > 0) continue;
        return 0;
    }
#else
    return write_all(fd, "", 1, deadline);
#endif
}

/* One CRLF-terminated line, read a byte at a time so nothing after it is
   taken from the socket. */
static int
read_line(int fd, char *line, size_t cap, Uint64 deadline, rdbus_error *err)
{
    size_t n = 0;

    for (;;) {
        char ch;
        ssize_t r = recv(fd, &ch, 1, 0);

        if (r == 0) {
            rdbus__error_set(err, ERR_AUTH_FAILED, "the bus hung up during authentication");
            return 0;
        }
        if (r < 0) {
            if (errno == EINTR) continue;
            if (would_block(errno)) {
                int w = wait_fd(fd, POLLIN, deadline);

                if (w > 0) continue;
                if (w == 0)
                    rdbus__error_set(err, ERR_TIMEOUT, "timed out authenticating");
                else
                    rdbus__error_set(err, ERR_AUTH_FAILED, "poll: %s", strerror(errno));
                return 0;
            }
            rdbus__error_set(err, ERR_AUTH_FAILED, "recv: %s", strerror(errno));
            return 0;
        }
        if ((unsigned char)ch > 0x7f || n + 1 >= cap) {
            rdbus__error_set(err, ERR_AUTH_FAILED, "malformed authentication reply");
            return 0;
        }
        line[n++] = ch;
        if (n >= 2 && line[n - 2] == '\r' && line[n - 1] == '\n') {
            line[n - 2] = 0;
            return 1;
        }
    }
}

static int
authenticate(int fd, Uint64 deadline, rdbus_error *err)
{
    static const char digits[] = "0123456789abcdef";
    char uid[24], buf[96], line[512];
    size_t i, n;

    SDL_snprintf(uid, sizeof(uid), "%lu", (unsigned long)getuid());
    n = SDL_strlcpy(buf, "AUTH EXTERNAL ", sizeof(buf));
    for (i = 0; uid[i]; i++) {
        buf[n++] = digits[((unsigned char)uid[i] >> 4) & 15];
        buf[n++] = digits[(unsigned char)uid[i] & 15];
    }
    buf[n++] = '\r';
    buf[n++] = '\n';
    if (!send_nul(fd, deadline) || !write_all(fd, buf, n, deadline)) {
        rdbus__error_set(err, ERR_AUTH_FAILED, "cannot write to the bus");
        return 0;
    }
    if (!read_line(fd, line, sizeof(line), deadline, err)) return 0;
    if (SDL_strncmp(line, "OK ", 3) != 0) {
        rdbus__error_set(err, ERR_AUTH_FAILED, "the bus refused EXTERNAL: %s", line);
        return 0;
    }
    for (i = 3; line[i]; i++)
        if (unhex((unsigned char)line[i]) < 0) break;
    if (i != 3 + 32 || line[i]) {
        rdbus__error_set(err, ERR_AUTH_FAILED, "malformed OK line from the bus");
        return 0;
    }
    if (!write_all(fd, "BEGIN\r\n", 7, deadline)) {
        rdbus__error_set(err, ERR_AUTH_FAILED, "cannot write to the bus");
        return 0;
    }
    return 1;
}

/* The socket, with the lock held */

static void
fail_locked(rdbus_conn *c)
{
    if (!c->open) return;
    c->open = 0;
    shutdown(c->fd, SHUT_RDWR);
    SDL_free(c->out);
    c->out = NULL;
    c->out_at = c->out_len = c->out_cap = 0;
    SDL_free(c->in);
    c->in = NULL;
    c->in_len = c->in_cap = c->need = 0;
    SDL_BroadcastCondition(c->cond);
}

static void
write_locked(rdbus_conn *c)
{
    while (c->open && c->out_at < c->out_len) {
        ssize_t n = send_bytes(c->fd, c->out + c->out_at, c->out_len - c->out_at);

        if (n > 0) {
            c->out_at += (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && would_block(errno)) return;
        fail_locked(c);
        return;
    }
    c->out_at = c->out_len = 0;
    if (c->out_cap > 4 * READ_CHUNK) {
        SDL_free(c->out);
        c->out = NULL;
        c->out_cap = 0;
    }
}

static int
queue_locked(rdbus_conn *c, rdbus_msg *m, uint32_t *serial)
{
    const uint8_t *b;
    size_t n, pending;
    uint32_t s;

    if (!c->open) return 0;
    s = c->serial;
    if (!(b = rdbus_msg_bytes(m, s, &n)) || !n) return 0;
    if (c->out_at == c->out_len) c->out_at = c->out_len = 0;
    pending = c->out_len - c->out_at;
    if (n > MAX_OUTGOING - pending) return 0;
    if (c->out_len + n > c->out_cap && c->out_at) {
        SDL_memmove(c->out, c->out + c->out_at, pending);
        c->out_at = 0;
        c->out_len = pending;
    }
    if (c->out_len + n > c->out_cap) {
        size_t cap = c->out_cap ? c->out_cap : 4096;
        uint8_t *p;

        while (cap < c->out_len + n) cap *= 2;
        if (!(p = (uint8_t *)SDL_realloc(c->out, cap))) return 0;
        c->out = p;
        c->out_cap = cap;
    }
    SDL_memcpy(c->out + c->out_len, b, n);
    c->out_len += n;
    c->serial = s + 1 ? s + 1 : 1;
    *serial = s;
    return 1;
}

/* The bus stamps every sender, so a call to the bus or to a unique name is
   answered by it, or by the bus with an error. */
static int
answers(const waiter *w, const rdbus_msg *m)
{
    const char *from = rdbus_msg_sender(m);

    if (!w->dest || (w->dest[0] != ':' && SDL_strcmp(w->dest, RDBUS_SERVICE_DBUS) != 0))
        return 1;
    if (!from) return 0;
    if (SDL_strcmp(from, w->dest) == 0) return 1;
    return w->dest[0] == ':' && rdbus_msg_type(m) == RDBUS_MESSAGE_ERROR &&
           SDL_strcmp(from, RDBUS_SERVICE_DBUS) == 0;
}

static void
route_locked(rdbus_conn *c, rdbus_msg *m)
{
    int type = rdbus_msg_type(m);
    queued *q;

    if (type == RDBUS_MESSAGE_METHOD_RETURN || type == RDBUS_MESSAGE_ERROR) {
        uint32_t rs = rdbus_msg_reply_serial(m);
        waiter *w;

        for (w = c->waiters; w; w = w->next) {
            if (w->serial == rs && !w->reply && answers(w, m)) {
                w->reply = m;
                return;
            }
        }
        rdbus_msg_free(m);
        return;
    }
    if (type != RDBUS_MESSAGE_METHOD_CALL && type != RDBUS_MESSAGE_SIGNAL) {
        rdbus_msg_free(m);
        return;
    }
    if (!(q = (queued *)SDL_malloc(sizeof(*q)))) {
        rdbus_msg_free(m);
        return;
    }
    if (c->nqueued >= MAX_QUEUED) {
        queued *old = c->head;

        c->head = old->next;
        if (!c->head) c->tail = NULL;
        c->nqueued--;
        rdbus_msg_free(old->m);
        SDL_free(old);
    }
    q->next = NULL;
    q->m = m;
    c->nqueued++;
    if (c->tail)
        c->tail->next = q;
    else
        c->head = q;
    c->tail = q;
}

static void
frames_locked(rdbus_conn *c)
{
    size_t at = 0;

    c->need = 0;
    while (c->open) {
        size_t have = c->in_len - at;
        long n = rdbus__frame_size(c->in + at, have);
        rdbus_msg *m;
        int type;

        if (n < 0 || (unsigned long)n > MAX_MESSAGE) {
            fail_locked(c);
            return;
        }
        if (n == 0 || (size_t)n > have) {
            c->need = (size_t)n;
            break;
        }
        type = c->in[at + 1];
        if (type > RDBUS_MESSAGE_SIGNAL) {
            /* The specification has unknown types ignored. */
            at += (size_t)n;
            continue;
        }
        m = rdbus_msg_parse(c->in + at, (size_t)n);
        at += (size_t)n;
        if (m) route_locked(c, m);
    }
    if (at == c->in_len) {
        c->in_len = 0;
        if (c->in_cap > 4 * READ_CHUNK) {
            SDL_free(c->in);
            c->in = NULL;
            c->in_cap = 0;
        }
    } else if (at) {
        SDL_memmove(c->in, c->in + at, c->in_len - at);
        c->in_len -= at;
    }
}

static void
read_locked(rdbus_conn *c)
{
    int round;

    for (round = 0; round < READ_ROUNDS && c->open; round++) {
        size_t want = READ_CHUNK;
        ssize_t n;

        if (c->need > c->in_len && c->need - c->in_len > want) want = c->need - c->in_len;
        if (c->in_cap - c->in_len < want) {
            size_t cap = c->in_len + want;
            uint8_t *p = (uint8_t *)SDL_realloc(c->in, cap);

            if (!p) {
                fail_locked(c);
                return;
            }
            c->in = p;
            c->in_cap = cap;
        }
        n = recv(c->fd, c->in + c->in_len, c->in_cap - c->in_len, 0);
        if (n > 0) {
            c->in_len += (size_t)n;
            frames_locked(c);
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && would_block(errno)) return;
        fail_locked(c);
        return;
    }
}

/* Waits up to timeout_ms (negative: without end) for input as the one reader,
   writing meanwhile, then reads what has come. Wakes every waiter. */
static void
io_locked(rdbus_conn *c, int timeout_ms)
{
    Uint64 deadline = deadline_in(timeout_ms);

    write_locked(c);
    while (c->open && timeout_ms != 0) {
        struct pollfd p;
        int r, e, ms = deadline == NEVER ? -1 : ms_left(deadline);

        if (ms == 0) break;
        p.fd = c->fd;
        p.events = POLLIN;
        p.revents = 0;
        if (c->out_at < c->out_len) p.events |= POLLOUT;
        c->reading = 1;
        SDL_UnlockMutex(c->lock);
        r = poll(&p, 1, ms);
        e = errno;
        SDL_LockMutex(c->lock);
        c->reading = 0;
        if (r == 0) break;
        if (r < 0) {
            if (e == EINTR || e == EAGAIN) continue;
            fail_locked(c);
            break;
        }
        if (p.revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL)) break;
        write_locked(c);
    }
    write_locked(c);
    read_locked(c);
    SDL_BroadcastCondition(c->cond);
}

/* Connections */

static void
destroy(rdbus_conn *c)
{
    int i;

    while (c->head) {
        queued *q = c->head;

        c->head = q->next;
        rdbus_msg_free(q->m);
        SDL_free(q);
    }
    for (i = 0; i < c->nhandlers; i++) SDL_free(c->handlers[i].path);
    SDL_free(c->handlers);
    if (c->fd >= 0) close(c->fd);
    SDL_free(c->out);
    SDL_free(c->in);
    SDL_free(c->unique);
    if (c->cond) SDL_DestroyCondition(c->cond);
    if (c->lock) SDL_DestroyMutex(c->lock);
    SDL_free(c);
}

static rdbus_conn *
establish(int fd, Uint64 deadline, rdbus_error *err)
{
    rdbus_conn *c;
    rdbus_msg *m, *r;
    const char *name = NULL;

    if (fd < 0) return NULL;
    if (!authenticate(fd, deadline, err)) {
        close(fd);
        return NULL;
    }
    if (!(c = (rdbus_conn *)SDL_calloc(1, sizeof(*c)))) {
        close(fd);
        rdbus__error_set(err, ERR_NO_MEMORY, "out of memory");
        return NULL;
    }
    c->fd = fd;
    c->open = 1;
    c->serial = 1;
    SDL_SetAtomicInt(&c->refs, 1);
    c->lock = SDL_CreateMutex();
    c->cond = SDL_CreateCondition();
    if (!c->lock || !c->cond) {
        destroy(c);
        rdbus__error_set(err, ERR_NO_MEMORY, "out of memory");
        return NULL;
    }

    m = rdbus_msg_call(RDBUS_SERVICE_DBUS, RDBUS_PATH_DBUS, RDBUS_INTERFACE_DBUS, "Hello");
    if (!m) {
        destroy(c);
        rdbus__error_set(err, ERR_NO_MEMORY, "out of memory");
        return NULL;
    }
    r = rdbus_call(c, m, SDL_max(ms_left(deadline), 1), err);
    rdbus_msg_free(m);
    if (!r) {
        destroy(c);
        return NULL;
    }
    if (!rdbus_msg_get_args(r, NULL, RDBUS_TYPE_STRING, &name, RDBUS_TYPE_INVALID) ||
        !name || name[0] != ':' || !(c->unique = SDL_strdup(name))) {
        rdbus_msg_free(r);
        destroy(c);
        rdbus__error_set(err, RDBUS_ERROR_FAILED, "the bus answered Hello without a unique name");
        return NULL;
    }
    rdbus_msg_free(r);
    return c;
}

rdbus_conn *
rdbus_open(const char *address, rdbus_error *err)
{
    Uint64 deadline = deadline_in(OPEN_MS);

    if (!address) {
        rdbus__error_set(err, ERR_BAD_ADDRESS, "no bus address");
        return NULL;
    }
    return establish(connect_address(address, deadline, err), deadline, err);
}

rdbus_conn *
rdbus_session_private(rdbus_error *err)
{
    Uint64 deadline = deadline_in(OPEN_MS);

    return establish(connect_session(deadline, err), deadline, err);
}

rdbus_conn *
rdbus_session(rdbus_error *err)
{
    rdbus_conn *c;

    if (SDL_ShouldInit(&g_session_init)) {
        g_session_lock = SDL_CreateMutex();
        SDL_SetInitialized(&g_session_init, g_session_lock != NULL);
    }
    if (!g_session_lock) {
        rdbus__error_set(err, ERR_NO_MEMORY, "out of memory");
        return NULL;
    }
    SDL_LockMutex(g_session_lock);
    if (g_session && !rdbus_connected(g_session)) {
        rdbus_unref(g_session);
        g_session = NULL;
    }
    if (!g_session) g_session = rdbus_session_private(err);
    if ((c = g_session)) SDL_AtomicIncRef(&c->refs);
    SDL_UnlockMutex(g_session_lock);
    return c;
}

/* 1 when that was the last reference. */
static int
release(rdbus_conn *c)
{
    if (!SDL_AtomicDecRef(&c->refs)) return 0;
    destroy(c);
    return 1;
}

void
rdbus_unref(rdbus_conn *c)
{
    if (c) release(c);
}

const char *
rdbus_unique_name(rdbus_conn *c)
{
    return c ? c->unique : NULL;
}

int
rdbus_fd(rdbus_conn *c)
{
    int fd;

    if (!c) return -1;
    SDL_LockMutex(c->lock);
    fd = c->open ? c->fd : -1;
    SDL_UnlockMutex(c->lock);
    return fd;
}

int
rdbus_connected(rdbus_conn *c)
{
    int open;

    if (!c) return 0;
    SDL_LockMutex(c->lock);
    open = c->open;
    SDL_UnlockMutex(c->lock);
    return open;
}

/* Sending */

int
rdbus_send(rdbus_conn *c, rdbus_msg *m, uint32_t *serial)
{
    uint32_t s = 0;
    int ok;

    if (!c || !m) return 0;
    SDL_LockMutex(c->lock);
    ok = queue_locked(c, m, &s);
    if (ok) write_locked(c);
    ok = ok && c->open;
    SDL_UnlockMutex(c->lock);
    if (ok && serial) *serial = s;
    return ok;
}

int
rdbus_flush(rdbus_conn *c)
{
    int ok;

    if (!c) return 0;
    SDL_LockMutex(c->lock);
    write_locked(c);
    while (c->open && c->out_at < c->out_len) {
        int fd = c->fd;

        /* The bus may be waiting for us to read before it reads again. */
        if (!c->reading) io_locked(c, 0);
        if (!c->open || c->out_at == c->out_len) break;
        SDL_UnlockMutex(c->lock);
        wait_fd(fd, POLLOUT, deadline_in(1000));
        SDL_LockMutex(c->lock);
        write_locked(c);
    }
    ok = c->open;
    SDL_UnlockMutex(c->lock);
    return ok;
}

rdbus_msg *
rdbus_call(rdbus_conn *c, rdbus_msg *m, int timeout_ms, rdbus_error *err)
{
    waiter w, **pp;
    Uint64 deadline;
    rdbus_msg *r;
    int open;

    if (!c || !m) {
        rdbus__error_set(err, RDBUS_ERROR_FAILED, "no connection or no message");
        return NULL;
    }
    if (rdbus_msg_type(m) != RDBUS_MESSAGE_METHOD_CALL || rdbus__msg_no_reply(m)) {
        rdbus__error_set(err, RDBUS_ERROR_INVALID_ARGS, "no reply to wait for");
        return NULL;
    }
    if (timeout_ms < 0) timeout_ms = DEFAULT_CALL_MS;
    deadline = deadline_in(timeout_ms);
    w.next = NULL;
    w.serial = 0;
    w.dest = rdbus_msg_destination(m);
    w.reply = NULL;

    SDL_LockMutex(c->lock);
    if (!c->open) {
        SDL_UnlockMutex(c->lock);
        rdbus__error_set(err, RDBUS_ERROR_DISCONNECTED, "not connected to the bus");
        return NULL;
    }
    if (!queue_locked(c, m, &w.serial)) {
        SDL_UnlockMutex(c->lock);
        rdbus__error_set(err, RDBUS_ERROR_FAILED, "the call could not be queued");
        return NULL;
    }
    w.next = c->waiters;
    c->waiters = &w;
    write_locked(c);
    while (!w.reply && c->open) {
        int left = ms_left(deadline);

        if (!left) break;
        if (!c->reading) {
            io_locked(c, left);
        } else {
            /* Another thread polls; it may not be watching for output. */
            int ms = c->out_at < c->out_len && left > 10 ? 10 : left;

            SDL_WaitConditionTimeout(c->cond, c->lock, ms);
            write_locked(c);
        }
    }
    for (pp = &c->waiters; *pp; pp = &(*pp)->next) {
        if (*pp == &w) {
            *pp = w.next;
            break;
        }
    }
    open = c->open;
    SDL_UnlockMutex(c->lock);

    if (!(r = w.reply)) {
        if (open)
            rdbus__error_set(err, RDBUS_ERROR_NO_REPLY, "no reply within %d ms", timeout_ms);
        else
            rdbus__error_set(err, RDBUS_ERROR_DISCONNECTED, "disconnected before the reply");
        return NULL;
    }
    if (rdbus_msg_type(r) == RDBUS_MESSAGE_ERROR) {
        const char *name = rdbus_msg_error_name(r), *sig = rdbus_msg_signature(r);
        const char *text = NULL;

        if (sig && sig[0] == RDBUS_TYPE_STRING)
            rdbus_msg_get_args(r, NULL, RDBUS_TYPE_STRING, &text, RDBUS_TYPE_INVALID);
        rdbus__error_set(err, name ? name : RDBUS_ERROR_FAILED, "%s", text ? text : "");
        rdbus_msg_free(r);
        return NULL;
    }
    return r;
}

/* Reading and dispatch */

int
rdbus_read_write(rdbus_conn *c, int timeout_ms)
{
    int ok;

    if (!c) return 0;
    SDL_LockMutex(c->lock);
    if (c->head) timeout_ms = 0;
    if (c->open && !c->reading) {
        io_locked(c, timeout_ms);
    } else if (c->open) {
        write_locked(c);
        if (timeout_ms < 0)
            SDL_WaitCondition(c->cond, c->lock);
        else if (timeout_ms > 0)
            SDL_WaitConditionTimeout(c->cond, c->lock, timeout_ms);
    }
    ok = c->open;
    SDL_UnlockMutex(c->lock);
    return ok;
}

static int
covers(const char *base, const char *path)
{
    size_t n = SDL_strlen(base);

    if (n == 1) return path[0] == '/' && path[1];
    return SDL_strncmp(base, path, n) == 0 && path[n] == '/';
}

static int
read_machine_id(char *id)
{
    static const char *const files[] = { "/etc/machine-id", "/var/lib/dbus/machine-id",
                                         "/var/db/dbus/machine-id" };
    size_t f;

    for (f = 0; f < SDL_arraysize(files); f++) {
        int fd = open(files[f], O_RDONLY | CLOEXEC), i;
        ssize_t n;

        if (fd < 0) continue;
        n = read(fd, id, 32);
        close(fd);
        if (n != 32) continue;
        for (i = 0; i < 32 && unhex((unsigned char)id[i]) >= 0; i++)
            ;
        if (i == 32) {
            id[32] = 0;
            return 1;
        }
    }
    return 0;
}

/* org.freedesktop.DBus.Peer, which libdbus answered on every path before any
   filter or handler saw the call. */
static int
is_peer(const rdbus_msg *m)
{
    const char *iface = rdbus_msg_interface(m);

    return rdbus_msg_type(m) == RDBUS_MESSAGE_METHOD_CALL && iface &&
           SDL_strcmp(iface, IFACE_PEER) == 0;
}

static void
answer_peer(rdbus_conn *c, rdbus_msg *m)
{
    char id[33];
    const char *s = id;
    rdbus_msg *r;

    if (rdbus__msg_no_reply(m)) return;
    if (rdbus_msg_has_member(m, "Ping")) {
        r = rdbus_msg_return(m);
    } else if (!rdbus_msg_has_member(m, "GetMachineId")) {
        r = rdbus_msg_error(m, RDBUS_ERROR_UNKNOWN_METHOD,
                            "Unknown method invoked on org.freedesktop.DBus.Peer interface");
    } else if (!read_machine_id(id)) {
        r = rdbus_msg_error(m, RDBUS_ERROR_FAILED, "no machine ID");
    } else if ((r = rdbus_msg_return(m)) &&
               !rdbus_msg_append_args(r, RDBUS_TYPE_STRING, &s, RDBUS_TYPE_INVALID)) {
        rdbus_msg_free(r);
        r = NULL;
    }
    if (r) {
        rdbus_send(c, r, NULL);
        rdbus_msg_free(r);
    }
}

typedef struct strbuf {
    char  *s;
    size_t len, cap;
    int    failed;
} strbuf;

static void
strbuf_add(strbuf *b, const char *s, size_t n)
{
    if (b->failed) return;
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 512;
        char *p;

        while (cap < b->len + n + 1) cap *= 2;
        if (!(p = (char *)SDL_realloc(b->s, cap))) {
            b->failed = 1;
            return;
        }
        b->s = p;
        b->cap = cap;
    }
    SDL_memcpy(b->s + b->len, s, n);
    b->len += n;
    b->s[b->len] = 0;
}

static int
path_char(char ch)
{
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
           ch == '_';
}

/* The element of h's path right below path, whose length with its slash is
   skip; NULL when h is not below path. */
static const char *
child_of(const handler *h, const char *path, size_t skip, size_t *len)
{
    const char *p;
    size_t n;

    if (h->kind == H_FILTER || !covers(path, h->path)) return NULL;
    p = h->path + skip;
    for (n = 0; p[n] && p[n] != '/'; n++)
        if (!path_char(p[n])) return NULL;
    *len = n;
    return n ? p : NULL;
}

/* What libdbus answered to an Introspect no handler claimed: a node for each
   next element of the registered paths below. */
static rdbus_msg *
introspect_reply(rdbus_conn *c, rdbus_msg *m)
{
    static const char head[] = RDBUS_INTROSPECT_DOCTYPE "<node>\n";
    const char *path = rdbus_msg_path(m), *xml;
    rdbus_msg *r = NULL;
    strbuf b;
    size_t skip;
    int i, j;

    if (!path || !rdbus_msg_is_call(m, RDBUS_INTERFACE_INTROSPECTABLE, "Introspect")) return NULL;
    SDL_zero(b);
    skip = path[1] ? SDL_strlen(path) + 1 : 1;
    strbuf_add(&b, head, sizeof(head) - 1);
    SDL_LockMutex(c->lock);
    for (i = 0; i < c->nhandlers; i++) {
        size_t n = 0, k = 0;
        const char *e = child_of(&c->handlers[i], path, skip, &n), *f;

        if (!e) continue;
        for (j = 0; j < i; j++) {
            f = child_of(&c->handlers[j], path, skip, &k);
            if (f && k == n && SDL_memcmp(e, f, n) == 0) break;
        }
        if (j < i) continue;
        strbuf_add(&b, "  <node name=\"", 14);
        strbuf_add(&b, e, n);
        strbuf_add(&b, "\"/>\n", 4);
    }
    SDL_UnlockMutex(c->lock);
    strbuf_add(&b, "</node>\n", 8);
    xml = b.s;
    if (!b.failed && (r = rdbus_msg_return(m)) &&
        !rdbus_msg_append_args(r, RDBUS_TYPE_STRING, &xml, RDBUS_TYPE_INVALID)) {
        rdbus_msg_free(r);
        r = NULL;
    }
    SDL_free(b.s);
    return r;
}

static void
answer_unhandled(rdbus_conn *c, rdbus_msg *m)
{
    rdbus_msg *r = introspect_reply(c, m);

    if (!r) {
        const char *iface = rdbus_msg_interface(m), *member = rdbus_msg_member(m);
        const char *sig = rdbus_msg_signature(m), *path = rdbus_msg_path(m);
        char text[256];

        SDL_snprintf(text, sizeof(text), "No method %s with signature \"%s\" on interface %s at %s",
                     member ? member : "", sig ? sig : "", iface ? iface : "(none)",
                     path ? path : "");
        r = rdbus_msg_error(m, RDBUS_ERROR_UNKNOWN_METHOD, text);
    }
    if (r) {
        rdbus_send(c, r, NULL);
        rdbus_msg_free(r);
    }
}

int
rdbus_dispatch(rdbus_conn *c)
{
    queued *q;
    handler *calls = NULL;
    const char *path;
    int i, j, n = 0, handled = 0, peer, more;

    if (!c) return 0;
    SDL_LockMutex(c->lock);
    if (!(q = c->head)) {
        SDL_UnlockMutex(c->lock);
        return 0;
    }
    c->head = q->next;
    if (!c->head) c->tail = NULL;
    c->nqueued--;
    /* A handler may drop the caller's last reference. */
    SDL_AtomicIncRef(&c->refs);

    path = rdbus_msg_path(q->m);
    peer = is_peer(q->m);
    if (!peer)
        calls = (handler *)SDL_malloc(sizeof(*calls) * (size_t)(c->nhandlers ? c->nhandlers : 1));
    if (calls) {
        for (i = 0; i < c->nhandlers; i++)
            if (c->handlers[i].kind == H_FILTER) calls[n++] = c->handlers[i];
        if (path) {
            int first;

            for (i = 0; i < c->nhandlers; i++) {
                if (c->handlers[i].kind != H_FILTER && SDL_strcmp(c->handlers[i].path, path) == 0) {
                    calls[n++] = c->handlers[i];
                    break;
                }
            }
            first = n;
            for (i = 0; i < c->nhandlers; i++) {
                handler h = c->handlers[i];

                if (h.kind != H_FALLBACK || !covers(h.path, path)) continue;
                for (j = n; j > first && SDL_strlen(calls[j - 1].path) < SDL_strlen(h.path); j--)
                    calls[j] = calls[j - 1];
                calls[j] = h;
                n++;
            }
        }
    }
    SDL_UnlockMutex(c->lock);

    if (peer) {
        answer_peer(c, q->m);
    } else {
        for (i = 0; i < n && !handled; i++)
            handled = calls[i].fn(c, q->m, calls[i].user) == RDBUS_HANDLED;
        if (!handled && rdbus_msg_type(q->m) == RDBUS_MESSAGE_METHOD_CALL &&
            !rdbus__msg_no_reply(q->m))
            answer_unhandled(c, q->m);
    }
    SDL_free(calls);
    rdbus_msg_free(q->m);
    SDL_free(q);

    SDL_LockMutex(c->lock);
    more = c->head != NULL;
    SDL_UnlockMutex(c->lock);
    return release(c) ? 0 : more;
}

int
rdbus_read_write_dispatch(rdbus_conn *c, int timeout_ms)
{
    int ok;

    if (!c) return 0;
    SDL_AtomicIncRef(&c->refs);
    rdbus_read_write(c, timeout_ms);
    while (rdbus_dispatch(c))
        ;
    ok = rdbus_connected(c);
    return release(c) ? 0 : ok;
}

static int
add_handler(rdbus_conn *c, int kind, const char *path, rdbus_handler fn, void *user)
{
    handler h;
    int i;

    if (!c || !fn) return 0;
    if (kind != H_FILTER && (!path || path[0] != '/')) return 0;
    h.kind = kind;
    h.path = NULL;
    h.fn = fn;
    h.user = user;
    if (kind != H_FILTER && !(h.path = SDL_strdup(path))) return 0;

    SDL_LockMutex(c->lock);
    for (i = 0; kind != H_FILTER && i < c->nhandlers; i++) {
        if (c->handlers[i].kind != H_FILTER && SDL_strcmp(c->handlers[i].path, path) == 0) {
            SDL_UnlockMutex(c->lock);
            SDL_free(h.path);
            return 0;
        }
    }
    if (c->nhandlers == c->chandlers) {
        int cap = c->chandlers ? c->chandlers * 2 : 8;
        handler *p = (handler *)SDL_realloc(c->handlers, sizeof(*p) * (size_t)cap);

        if (!p) {
            SDL_UnlockMutex(c->lock);
            SDL_free(h.path);
            return 0;
        }
        c->handlers = p;
        c->chandlers = cap;
    }
    c->handlers[c->nhandlers++] = h;
    SDL_UnlockMutex(c->lock);
    return 1;
}

int
rdbus_add_filter(rdbus_conn *c, rdbus_handler fn, void *user)
{
    return add_handler(c, H_FILTER, NULL, fn, user);
}

int
rdbus_register_path(rdbus_conn *c, const char *path, rdbus_handler fn, void *user)
{
    return add_handler(c, H_PATH, path, fn, user);
}

int
rdbus_register_fallback(rdbus_conn *c, const char *path, rdbus_handler fn, void *user)
{
    return add_handler(c, H_FALLBACK, path, fn, user);
}

/* The bus's own methods */

static rdbus_msg *
bus_call(rdbus_conn *c, const char *member, const char *s, const uint32_t *u, rdbus_error *err)
{
    rdbus_msg *m, *r;
    int ok;

    if (!c || !s) {
        rdbus__error_set(err, RDBUS_ERROR_INVALID_ARGS, "no connection or no argument");
        return NULL;
    }
    if (!(m = rdbus_msg_call(RDBUS_SERVICE_DBUS, RDBUS_PATH_DBUS, RDBUS_INTERFACE_DBUS, member))) {
        rdbus__error_set(err, ERR_NO_MEMORY, "out of memory");
        return NULL;
    }
    if (u)
        ok = rdbus_msg_append_args(m, RDBUS_TYPE_STRING, &s, RDBUS_TYPE_UINT32, u,
                                   RDBUS_TYPE_INVALID);
    else
        ok = rdbus_msg_append_args(m, RDBUS_TYPE_STRING, &s, RDBUS_TYPE_INVALID);
    if (!ok) {
        rdbus_msg_free(m);
        rdbus__error_set(err, RDBUS_ERROR_INVALID_ARGS, "bad argument to %s", member);
        return NULL;
    }
    r = rdbus_call(c, m, BUS_CALL_MS, err);
    rdbus_msg_free(m);
    return r;
}

int
rdbus_request_name(rdbus_conn *c, const char *name, unsigned flags, rdbus_error *err)
{
    uint32_t f = flags, code = 0;
    rdbus_msg *r = bus_call(c, "RequestName", name, &f, err);
    int ok;

    if (!r) return -1;
    ok = rdbus_msg_get_args(r, NULL, RDBUS_TYPE_UINT32, &code, RDBUS_TYPE_INVALID);
    rdbus_msg_free(r);
    if (!ok) {
        rdbus__error_set(err, RDBUS_ERROR_FAILED, "RequestName answered without a code");
        return -1;
    }
    return (int)code;
}

int
rdbus_add_match(rdbus_conn *c, const char *rule, rdbus_error *err)
{
    rdbus_msg *r = bus_call(c, "AddMatch", rule, NULL, err);

    if (!r) return 0;
    rdbus_msg_free(r);
    return 1;
}
