/*
 * ballum/src/ballum_x11proxy.c -- the X11 relay (see ballum_x11proxy.h).
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include "sleela_ballum/ballum_x11proxy.h"
#include "sleela_ballum/ballum_xwire.h"

#include <string.h>

#if defined(__unix__) || defined(__APPLE__)

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

static int set_no_sigpipe(int fd) {
#ifdef SO_NOSIGPIPE
    int enabled = 1;
    return setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) == 0;
#else
    (void)fd;
    return 1;
#endif
}

static int peer_uid_matches(int fd, uid_t expected) {
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    uid_t peer_uid;
    gid_t peer_gid;
    return getpeereid(fd, &peer_uid, &peer_gid) == 0 && peer_uid == expected;
#elif defined(__linux__)
    struct ucred cred;
    socklen_t cl = sizeof(cred);
    return getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &cl) == 0 && cred.uid == expected;
#else
    (void)fd;
    (void)expected;
    return 0;
#endif
}

#define MAX_CONN_SLOTS 64
#define BACKLOG_LIMIT  (4u * 1024u * 1024u)
#define SETUP_MAX      8192u
#define IO_CHUNK       65536u

typedef struct { uint8_t *p; size_t start, len, cap; } buf_t;

typedef struct {
    int         used;
    int         cfd, sfd;
    int         setup_done, observed, counted_unobserved;
    uint8_t     setup[SETUP_MAX];
    size_t      setup_len;
    buf_t       to_s, to_c;
    ballum_xwire_t *wire;
} conn_t;

struct ballum_x11proxy {
    char            listen_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    char            server_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    uint8_t         cookie[16];
    int             have_cookie;
    ballum_xdraw_t *draw;
    int             max_conn;
    int             lfd, wake_r, wake_w;
    pthread_t       thread;
    int             running;
    volatile int    stop;
    uid_t           uid;
    conn_t          conn[MAX_CONN_SLOTS];
    pthread_mutex_t stats_lock;
    ballum_x11proxy_stats_t st;
};

/* ---- buffers ---------------------------------------------------------------------- */

static size_t bavail(const buf_t *b) { return b->len - b->start; }

static int bappend(buf_t *b, const uint8_t *d, size_t n) {
    if (b->start > 0 && (b->start == b->len || b->start > b->cap / 2)) {
        memmove(b->p, b->p + b->start, b->len - b->start);
        b->len -= b->start; b->start = 0;
    }
    if (b->len + n > b->cap) {
        size_t nc = b->cap ? b->cap : 8192u;
        while (nc < b->len + n) nc *= 2u;
        uint8_t *np = (uint8_t *)realloc(b->p, nc);
        if (!np) return 0;
        b->p = np; b->cap = nc;
    }
    memcpy(b->p + b->len, d, n);
    b->len += n;
    return 1;
}

static void bfree(buf_t *b) { free(b->p); memset(b, 0, sizeof(*b)); }

/* ---- statistics --------------------------------------------------------------------- */

static void stat_add(ballum_x11proxy_t *p, uint64_t *field, uint64_t n) {
    pthread_mutex_lock(&p->stats_lock);
    *field += n;
    pthread_mutex_unlock(&p->stats_lock);
}

void ballum_x11proxy_stats(ballum_x11proxy_t *p, ballum_x11proxy_stats_t *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!p) return;
    pthread_mutex_lock(&p->stats_lock);
    *out = p->st;
    pthread_mutex_unlock(&p->stats_lock);
}

/* ---- connections ---------------------------------------------------------------------- */

static void conn_close(conn_t *c) {
    if (!c->used) return;
    if (c->cfd >= 0) close(c->cfd);
    if (c->sfd >= 0) close(c->sfd);
    ballum_xwire_destroy(c->wire);
    bfree(&c->to_s); bfree(&c->to_c);
    memset(c, 0, sizeof(*c));
    c->cfd = c->sfd = -1;
}

static int set_nonblock(int fd) {
    int fl = fcntl(fd, F_GETFL, 0);
    return fl >= 0 && fcntl(fd, F_SETFL, fl | O_NONBLOCK) == 0 && fcntl(fd, F_SETFD, FD_CLOEXEC) == 0;
}

static int connect_server(const char *path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    if (!set_no_sigpipe(fd)) { close(fd); return -1; }
    struct sockaddr_un a;
    memset(&a, 0, sizeof(a));
    a.sun_family = AF_UNIX;
    if (strlen(path) >= sizeof(a.sun_path)) { close(fd); return -1; }
    memcpy(a.sun_path, path, strlen(path) + 1);
    if (connect(fd, (struct sockaddr *)&a, sizeof(a)) != 0 || !set_nonblock(fd)) { close(fd); return -1; }
    return fd;
}

static void accept_one(ballum_x11proxy_t *p) {
    int fd = accept(p->lfd, NULL, NULL);
    if (fd < 0) return;
    if (!set_no_sigpipe(fd)) {
        close(fd);
        stat_add(p, &p->st.rejected, 1);
        return;
    }
    int slot = -1, live = 0;
    for (int i = 0; i < MAX_CONN_SLOTS; ++i) { if (p->conn[i].used) ++live; else if (slot < 0) slot = i; }
    if (!peer_uid_matches(fd, p->uid) ||
        slot < 0 || live >= p->max_conn || !set_nonblock(fd)) {
        close(fd);
        stat_add(p, &p->st.rejected, 1);
        return;
    }
    int sfd = connect_server(p->server_path);
    if (sfd < 0) { close(fd); stat_add(p, &p->st.rejected, 1); return; }
    conn_t *c = &p->conn[slot];
    memset(c, 0, sizeof(*c));
    c->used = 1; c->cfd = fd; c->sfd = sfd; c->observed = 1;
    ballum_xwire_cb_t cb = { ballum_xdraw_on_request, ballum_xdraw_on_reply, ballum_xdraw_on_event, p->draw };
    c->wire = ballum_xwire_create(&cb);
    if (!c->wire) c->observed = 0;
    stat_add(p, &p->st.connections, 1);
}

static void note_unobserved(ballum_x11proxy_t *p, conn_t *c) {
    if (c->observed && c->wire && ballum_xwire_desynced(c->wire)) {
        c->observed = 0;
        if (!c->counted_unobserved) { c->counted_unobserved = 1; stat_add(p, &p->st.unobserved, 1); }
    }
}

/* Queue bytes from the program toward the server, and let the framer watch them. */
static int forward_client(ballum_x11proxy_t *p, conn_t *c, const uint8_t *d, size_t n) {
    if (!bappend(&c->to_s, d, n)) return 0;
    stat_add(p, &p->st.bytes_to_server, n);
    if (c->observed) { ballum_xwire_client_feed(c->wire, d, n); note_unobserved(p, c); }
    return 1;
}

/* The first bytes: accumulate the setup request, rewrite its authorization, then
 * everything after it is relayed as it comes. Returns 0 when the connection must close. */
static int client_bytes(ballum_x11proxy_t *p, conn_t *c, const uint8_t *d, size_t n) {
    if (c->setup_done) return forward_client(p, c, d, n);
    if (c->setup_len + n > SETUP_MAX) { stat_add(p, &p->st.rejected, 1); return 0; }
    memcpy(c->setup + c->setup_len, d, n);
    c->setup_len += n;
    long sz = ballum_xwire_setup_request_size(c->setup, c->setup_len);
    if (sz < 0) { stat_add(p, &p->st.rejected, 1); return 0; }          /* not an X11 setup: refuse the connection */
    if (sz == 0) return 1;                                          /* wait for the rest of the setup */

    if (p->have_cookie) {
        uint8_t out[SETUP_MAX];
        size_t m = ballum_xwire_build_setup(out, sizeof(out), c->setup[0] == 'B', "MIT-MAGIC-COOKIE-1", p->cookie, sizeof(p->cookie));
        if (m == 0 || !bappend(&c->to_s, out, m)) return 0;
        stat_add(p, &p->st.bytes_to_server, m);
    } else {
        if (!bappend(&c->to_s, c->setup, (size_t)sz)) return 0;
        stat_add(p, &p->st.bytes_to_server, (size_t)sz);
    }
    if (c->observed) ballum_xwire_client_feed(c->wire, c->setup, (size_t)sz);      /* the framer sees the program's own setup */
    c->setup_done = 1;
    size_t rest = c->setup_len - (size_t)sz;
    int ok = rest == 0 ? 1 : forward_client(p, c, c->setup + sz, rest);
    c->setup_len = 0;
    return ok;
}

static int pump_read(ballum_x11proxy_t *p, conn_t *c, int from_client) {
    uint8_t tmp[IO_CHUNK];
    ssize_t n = recv(from_client ? c->cfd : c->sfd, tmp, sizeof(tmp), 0);
    if (n == 0) return 0;
    if (n < 0) return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? 1 : 0;
    if (from_client) return client_bytes(p, c, tmp, (size_t)n);
    if (!bappend(&c->to_c, tmp, (size_t)n)) return 0;
    stat_add(p, &p->st.bytes_to_client, (uint64_t)n);
    if (c->observed) { ballum_xwire_server_feed(c->wire, tmp, (size_t)n); note_unobserved(p, c); }
    return 1;
}

static int pump_write(conn_t *c, int to_client) {
    buf_t *b = to_client ? &c->to_c : &c->to_s;
    int fd = to_client ? c->cfd : c->sfd;
    while (bavail(b) > 0) {
        ssize_t n = send(fd, b->p + b->start, bavail(b), MSG_NOSIGNAL);
        if (n < 0) return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? 1 : 0;
        b->start += (size_t)n;
        if (b->start >= b->len) { b->start = b->len = 0; }
    }
    return 1;
}

/* ---- the relay thread --------------------------------------------------------------------- */

static void *relay_thread(void *arg) {
    ballum_x11proxy_t *p = (ballum_x11proxy_t *)arg;
    struct pollfd fds[2 + 2 * MAX_CONN_SLOTS];
    int owner[2 + 2 * MAX_CONN_SLOTS], side[2 + 2 * MAX_CONN_SLOTS];
    while (!p->stop) {
        nfds_t n = 0;
        fds[n].fd = p->wake_r; fds[n].events = POLLIN; owner[n] = -1; side[n++] = 0;
        fds[n].fd = p->lfd; fds[n].events = POLLIN; owner[n] = -2; side[n++] = 0;
        for (int i = 0; i < MAX_CONN_SLOTS; ++i) {
            conn_t *c = &p->conn[i];
            if (!c->used) continue;
            fds[n].fd = c->cfd; fds[n].events = 0;
            if (bavail(&c->to_s) < BACKLOG_LIMIT) fds[n].events |= POLLIN;
            if (bavail(&c->to_c) > 0) fds[n].events |= POLLOUT;
            owner[n] = i; side[n++] = 1;
            fds[n].fd = c->sfd; fds[n].events = 0;
            if (bavail(&c->to_c) < BACKLOG_LIMIT) fds[n].events |= POLLIN;
            if (bavail(&c->to_s) > 0) fds[n].events |= POLLOUT;
            owner[n] = i; side[n++] = 0;
        }
        int r = poll(fds, n, 250);
        if (r < 0) { if (errno == EINTR) continue; break; }
        if (r == 0) continue;
        for (nfds_t k = 0; k < n; ++k) {
            if (!fds[k].revents) continue;
            if (owner[k] == -1) { uint8_t t[64]; ssize_t u = read(p->wake_r, t, sizeof(t)); (void)u; continue; }
            if (owner[k] == -2) { accept_one(p); continue; }
            conn_t *c = &p->conn[owner[k]];
            if (!c->used) continue;
            int ok = 1;
            if (fds[k].revents & POLLIN) ok = pump_read(p, c, side[k]);
            if (ok && (fds[k].revents & POLLOUT)) ok = pump_write(c, side[k]);
            if (ok && (fds[k].revents & (POLLERR | POLLNVAL))) ok = 0;
            if (ok && (fds[k].revents & POLLHUP) && !(fds[k].revents & POLLIN)) ok = 0;
            if (!ok) conn_close(c);
        }
    }
    return NULL;
}

/* ---- start / stop --------------------------------------------------------------------------- */

ballum_status_t ballum_x11proxy_start(const ballum_x11proxy_config_t *cfg, ballum_x11proxy_t **out) {
    if (!out) return BALLUM_STATUS_INVALID_ARGUMENT;
    *out = NULL;
    if (!cfg || !cfg->listen_path || !cfg->server_path || !cfg->draw) return BALLUM_STATUS_INVALID_ARGUMENT;
    if (cfg->cookie && cfg->cookie_len != 16u) return BALLUM_STATUS_INVALID_ARGUMENT;
    ballum_x11proxy_t *p = (ballum_x11proxy_t *)calloc(1, sizeof(*p));
    if (!p) return BALLUM_STATUS_BACKEND_ERROR;
    if (strlen(cfg->listen_path) >= sizeof(p->listen_path) || strlen(cfg->server_path) >= sizeof(p->server_path) ||
        !*cfg->listen_path || !*cfg->server_path) { free(p); return BALLUM_STATUS_INVALID_ARGUMENT; }
    strcpy(p->listen_path, cfg->listen_path);
    strcpy(p->server_path, cfg->server_path);
    if (cfg->cookie) { memcpy(p->cookie, cfg->cookie, 16); p->have_cookie = 1; }
    p->draw = cfg->draw;
    p->max_conn = cfg->max_connections > 0 && cfg->max_connections <= MAX_CONN_SLOTS ? cfg->max_connections : 16;
    p->uid = geteuid();
    p->lfd = p->wake_r = p->wake_w = -1;
    for (int i = 0; i < MAX_CONN_SLOTS; ++i) p->conn[i].cfd = p->conn[i].sfd = -1;
    if (pthread_mutex_init(&p->stats_lock, NULL) != 0) { free(p); return BALLUM_STATUS_BACKEND_ERROR; }

    ballum_status_t st = BALLUM_STATUS_BACKEND_ERROR;
    int pipefd[2];
    if (pipe(pipefd) != 0) goto fail;
    p->wake_r = pipefd[0]; p->wake_w = pipefd[1];
    if (!set_nonblock(p->wake_r) || !set_nonblock(p->wake_w)) goto fail;

    p->lfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (p->lfd < 0 || !set_no_sigpipe(p->lfd)) goto fail;
    struct sockaddr_un a;
    memset(&a, 0, sizeof(a));
    a.sun_family = AF_UNIX;
    strcpy(a.sun_path, p->listen_path);
    unlink(p->listen_path);                                           /* a stale socket from an earlier run */
    mode_t old = umask(0177);                                         /* created 0600 */
    int bound = bind(p->lfd, (struct sockaddr *)&a, sizeof(a));
    umask(old);
    if (bound != 0) { st = errno == EACCES || errno == EPERM ? BALLUM_STATUS_PERMISSION_DENIED : BALLUM_STATUS_BACKEND_ERROR; goto fail; }
    if (chmod(p->listen_path, 0600) != 0 || listen(p->lfd, 16) != 0 || !set_nonblock(p->lfd)) goto fail;

    if (pthread_create(&p->thread, NULL, relay_thread, p) != 0) goto fail;
    p->running = 1;
    *out = p;
    return BALLUM_STATUS_OK;

fail:
    if (p->lfd >= 0) { close(p->lfd); unlink(p->listen_path); }
    if (p->wake_r >= 0) close(p->wake_r);
    if (p->wake_w >= 0) close(p->wake_w);
    pthread_mutex_destroy(&p->stats_lock);
    free(p);
    return st;
}

void ballum_x11proxy_stop(ballum_x11proxy_t *p) {
    if (!p) return;
    if (p->running) {
        p->stop = 1;
        ssize_t w = write(p->wake_w, "x", 1); (void)w;
        pthread_join(p->thread, NULL);
        p->running = 0;
    }
    for (int i = 0; i < MAX_CONN_SLOTS; ++i) conn_close(&p->conn[i]);
    if (p->lfd >= 0) { close(p->lfd); unlink(p->listen_path); }
    close(p->wake_r); close(p->wake_w);
    pthread_mutex_destroy(&p->stats_lock);
    free(p);
}

#else  /* no unix sockets: the hook is not available, and says so */

ballum_status_t ballum_x11proxy_start(const ballum_x11proxy_config_t *cfg, ballum_x11proxy_t **out) {
    (void)cfg;
    if (out) *out = NULL;
    return BALLUM_STATUS_UNSUPPORTED;
}
void ballum_x11proxy_stop(ballum_x11proxy_t *p) { (void)p; }
void ballum_x11proxy_stats(ballum_x11proxy_t *p, ballum_x11proxy_stats_t *out) {
    (void)p;
    if (out) { ballum_x11proxy_stats_t z; memset(&z, 0, sizeof(z)); *out = z; }
}

#endif
