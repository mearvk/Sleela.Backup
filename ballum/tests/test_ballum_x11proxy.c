/* Tests for the X11 relay, against a fake X server on a unix socket: bytes cross
 * unchanged in both directions, the program's credentials are replaced, what the
 * program draws is seen, and misuse is refused. */
#define _GNU_SOURCE 1
#include "sleela_ballum/ballum_x11proxy.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
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

/* ---- a fake X server -------------------------------------------------------------------- */

#define SRV_CONNS 6
typedef struct {
    int fd; int setup_seen; size_t setup_len;
    char auth_name[64]; uint8_t auth_data[64]; size_t auth_name_len, auth_data_len;
    uint8_t *rx; size_t rx_len, rx_cap;        /* everything after the setup request */
    uint8_t *raw; size_t raw_len, raw_cap;     /* everything received */
} srv_conn_t;

typedef struct {
    int lfd; pthread_t th; volatile int stop;
    srv_conn_t c[SRV_CONNS]; int n;
    pthread_mutex_t mu;
    uint8_t *tx; size_t tx_len; int tx_conn;   /* bytes to send to connection tx_conn */
} fake_t;

static void vappend(uint8_t **p, size_t *len, size_t *cap, const uint8_t *d, size_t n) {
    if (*len + n > *cap) { size_t nc = *cap ? *cap : 4096; while (nc < *len + n) nc *= 2; *p = (uint8_t *)realloc(*p, nc); *cap = nc; }
    memcpy(*p + *len, d, n); *len += n;
}

static void *fake_thread(void *arg) {
    fake_t *f = (fake_t *)arg;
    while (!f->stop) {
        struct pollfd fds[1 + SRV_CONNS];
        int n = 0;
        fds[n].fd = f->lfd; fds[n].events = POLLIN; ++n;
        pthread_mutex_lock(&f->mu);
        for (int i = 0; i < f->n; ++i) { fds[n].fd = f->c[i].fd; fds[n].events = POLLIN | (f->tx_len && f->tx_conn == i ? POLLOUT : 0); ++n; }
        pthread_mutex_unlock(&f->mu);
        if (poll(fds, (nfds_t)n, 30) <= 0) continue;
        if (fds[0].revents & POLLIN) {
            int fd = accept(f->lfd, NULL, NULL);
            if (fd >= 0) {
                if (!set_no_sigpipe(fd)) { close(fd); continue; }
                pthread_mutex_lock(&f->mu);
                if (f->n < SRV_CONNS) { memset(&f->c[f->n], 0, sizeof(f->c[0])); f->c[f->n].fd = fd; f->n++; } else close(fd);
                pthread_mutex_unlock(&f->mu);
            }
        }
        for (int i = 0; i < f->n; ++i) {
            struct pollfd *pf = &fds[1 + i];
            if (pf->fd != f->c[i].fd) break;
            if (pf->revents & POLLOUT) {
                pthread_mutex_lock(&f->mu);
                ssize_t w = send(f->c[i].fd, f->tx, f->tx_len, MSG_NOSIGNAL);
                if (w > 0) { memmove(f->tx, f->tx + w, f->tx_len - (size_t)w); f->tx_len -= (size_t)w; }
                pthread_mutex_unlock(&f->mu);
            }
            if (pf->revents & POLLIN) {
                uint8_t tmp[65536];
                ssize_t r = recv(f->c[i].fd, tmp, sizeof(tmp), 0);
                if (r <= 0) continue;
                pthread_mutex_lock(&f->mu);
                srv_conn_t *c = &f->c[i];
                vappend(&c->raw, &c->raw_len, &c->raw_cap, tmp, (size_t)r);
                if (!c->setup_seen) {
                    const uint8_t *b = c->raw;
                    if (c->raw_len >= 12) {
                        size_t nl = b[6] | ((size_t)b[7] << 8), dl = b[8] | ((size_t)b[9] << 8);
                        size_t total = 12 + ((nl + 3) & ~3u) + ((dl + 3) & ~3u);
                        if (c->raw_len >= total) {
                            c->setup_seen = 1; c->setup_len = total;
                            memcpy(c->auth_name, b + 12, nl < 63 ? nl : 63); c->auth_name_len = nl;
                            memcpy(c->auth_data, b + 12 + ((nl + 3) & ~3u), dl < 64 ? dl : 64); c->auth_data_len = dl;
                            uint8_t reply[8] = { 1, 0, 11, 0, 0, 0, 0, 0 };
                            send(c->fd, reply, 8, MSG_NOSIGNAL);
                            if (c->raw_len > total) vappend(&c->rx, &c->rx_len, &c->rx_cap, c->raw + total, c->raw_len - total);
                        }
                    }
                } else {
                    vappend(&c->rx, &c->rx_len, &c->rx_cap, tmp, (size_t)r);
                }
                pthread_mutex_unlock(&f->mu);
            }
        }
    }
    return NULL;
}

static void fake_start(fake_t *f, const char *path) {
    memset(f, 0, sizeof(*f));
    pthread_mutex_init(&f->mu, NULL);
    f->lfd = socket(AF_UNIX, SOCK_STREAM, 0);
    assert(f->lfd >= 0 && set_no_sigpipe(f->lfd));
    struct sockaddr_un a; memset(&a, 0, sizeof(a)); a.sun_family = AF_UNIX; strcpy(a.sun_path, path);
    unlink(path);
    assert(bind(f->lfd, (struct sockaddr *)&a, sizeof(a)) == 0 && listen(f->lfd, 8) == 0);
    pthread_create(&f->th, NULL, fake_thread, f);
}
static void fake_stop(fake_t *f) {
    f->stop = 1; pthread_join(f->th, NULL); close(f->lfd);
    for (int i = 0; i < f->n; ++i) { close(f->c[i].fd); free(f->c[i].rx); free(f->c[i].raw); }
    free(f->tx); pthread_mutex_destroy(&f->mu);
}
static void fake_send(fake_t *f, int conn, const uint8_t *d, size_t n) {
    pthread_mutex_lock(&f->mu);
    f->tx = (uint8_t *)realloc(f->tx, f->tx_len + n); memcpy(f->tx + f->tx_len, d, n); f->tx_len += n; f->tx_conn = conn;
    pthread_mutex_unlock(&f->mu);
}

/* ---- helpers ------------------------------------------------------------------------------- */

static void nap(int ms) { struct timespec ts = { 0, ms * 1000000L }; nanosleep(&ts, NULL); }

#define WAIT(cond) do { int _t = 0; while (!(cond) && _t < 4000) { nap(5); _t += 5; } assert(cond); } while (0)

static int dial(const char *path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0 || !set_no_sigpipe(fd)) { if (fd >= 0) close(fd); return -1; }
    struct sockaddr_un a; memset(&a, 0, sizeof(a)); a.sun_family = AF_UNIX; strcpy(a.sun_path, path);
    if (connect(fd, (struct sockaddr *)&a, sizeof(a)) != 0) { close(fd); return -1; }
    return fd;
}

static void send_all(int fd, const uint8_t *d, size_t n) {
    while (n) {
        struct pollfd pf = { fd, POLLOUT, 0 };
        poll(&pf, 1, 100);
        ssize_t w = send(fd, d, n, MSG_NOSIGNAL);
        if (w < 0) { if (errno == EAGAIN || errno == EINTR) continue; assert(0 && "send failed"); }
        d += w; n -= (size_t)w;
    }
}

/* Like send_all, but the peer may already have hung up (a refused connection). */
static void send_try(int fd, const uint8_t *d, size_t n) {
    while (n) {
        ssize_t w = send(fd, d, n, MSG_NOSIGNAL);
        if (w < 0) { if (errno == EAGAIN || errno == EINTR) continue; return; }
        d += w; n -= (size_t)w;
    }
}

static size_t recv_exact(int fd, uint8_t *out, size_t n, int ms) {
    size_t got = 0; int waited = 0;
    while (got < n && waited < ms) {
        struct pollfd pf = { fd, POLLIN, 0 };
        if (poll(&pf, 1, 20) > 0) { ssize_t r = recv(fd, out + got, n - got, 0); if (r <= 0) break; got += (size_t)r; }
        else waited += 20;
    }
    return got;
}

static int closed_by_peer(int fd) {
    for (int i = 0; i < 200; ++i) {
        struct pollfd pf = { fd, POLLIN, 0 };
        if (poll(&pf, 1, 20) > 0) { uint8_t b[16]; ssize_t r = recv(fd, b, sizeof(b), 0); if (r == 0 || (r < 0 && errno != EAGAIN)) return 1; }
    }
    return 0;
}

static void le16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void le32(uint8_t *p, uint32_t v) { le16(p, v & 0xFFFF); le16(p + 2, v >> 16); }

int main(void) {
    char dir[] = "/tmp/ballum-proxy-test-XXXXXX";
    assert(mkdtemp(dir));
    char listen_path[128], server_path[128];
    snprintf(listen_path, sizeof(listen_path), "%s/prog", dir);
    snprintf(server_path, sizeof(server_path), "%s/server", dir);

    fake_t fake; fake_start(&fake, server_path);
    ballum_xdraw_t *xd = ballum_xdraw_create();
    static const uint8_t cookie[16] = { 0xC0, 0x0C, 0x1E, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 };

    /* ---- refusals at start ---------------------------------------------------------------- */
    ballum_x11proxy_t *px = NULL;
    ballum_x11proxy_config_t cfg; memset(&cfg, 0, sizeof(cfg));
    cfg.listen_path = listen_path; cfg.server_path = server_path; cfg.draw = xd; cfg.cookie = cookie; cfg.cookie_len = 16;
    assert(ballum_x11proxy_start(NULL, &px) == BALLUM_STATUS_INVALID_ARGUMENT && !px);
    assert(ballum_x11proxy_start(&cfg, NULL) == BALLUM_STATUS_INVALID_ARGUMENT);
    { ballum_x11proxy_config_t b = cfg; b.draw = NULL;             assert(ballum_x11proxy_start(&b, &px) == BALLUM_STATUS_INVALID_ARGUMENT); }
    { ballum_x11proxy_config_t b = cfg; b.cookie_len = 5;          assert(ballum_x11proxy_start(&b, &px) == BALLUM_STATUS_INVALID_ARGUMENT); }
    { ballum_x11proxy_config_t b = cfg; b.listen_path = "";        assert(ballum_x11proxy_start(&b, &px) == BALLUM_STATUS_INVALID_ARGUMENT); }
    { char longp[300]; memset(longp, 'a', sizeof(longp)); longp[299] = 0;
      ballum_x11proxy_config_t b = cfg; b.listen_path = longp;     assert(ballum_x11proxy_start(&b, &px) == BALLUM_STATUS_INVALID_ARGUMENT); }
    { char nodir[160]; snprintf(nodir, sizeof(nodir), "%s/no/such/dir/sock", dir);
      ballum_x11proxy_config_t b = cfg; b.listen_path = nodir;     assert(ballum_x11proxy_start(&b, &px) != BALLUM_STATUS_OK && !px); }

    cfg.max_connections = 3;
    assert(ballum_x11proxy_start(&cfg, &px) == BALLUM_STATUS_OK && px);
    struct stat sb;
    assert(stat(listen_path, &sb) == 0 && S_ISSOCK(sb.st_mode) && (sb.st_mode & 077) == 0);     /* mode 0600 */

    /* ---- the program connects: credentials replaced, bytes relayed unchanged --------------- */
    int cl = dial(listen_path);
    assert(cl >= 0);
    uint8_t setup[128];
    static const uint8_t wrong[16] = { 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9 };
    size_t sl = ballum_xwire_build_setup(setup, sizeof(setup), 0, "MIT-MAGIC-COOKIE-1", wrong, 16);
    send_all(cl, setup, sl / 2);                                  /* the setup can arrive in pieces */
    nap(20);
    send_all(cl, setup + sl / 2, sl - sl / 2);
    uint8_t rep[8];
    assert(recv_exact(cl, rep, 8, 2000) == 8 && rep[0] == 1);     /* the server's reply came back */
    WAIT(fake.n >= 1 && fake.c[0].setup_seen);
    assert(fake.c[0].auth_name_len == 18 && memcmp(fake.c[0].auth_name, "MIT-MAGIC-COOKIE-1", 18) == 0);
    assert(fake.c[0].auth_data_len == 16 && memcmp(fake.c[0].auth_data, cookie, 16) == 0);       /* the real cookie */
    assert(memcmp(fake.c[0].auth_data, wrong, 16) != 0);                                         /* never the program's */

    /* requests: CreateWindow, MapWindow, then a 2 MB PutImage with a BIG-REQUESTS header */
    uint8_t *stream = (uint8_t *)malloc(40 + 8 + 2u * 1024 * 1024);
    size_t n = 0;
    memset(stream, 0, 32); stream[0] = 1; le16(stream + 2, 8);                                  /* 32 bytes */
    le32(stream + 4, 0x1000); le32(stream + 8, 0x100); le16(stream + 12, 0); le16(stream + 14, 0); le16(stream + 16, 200); le16(stream + 18, 100);
    n = 32;
    memset(stream + n, 0, 8); stream[n] = 8; le16(stream + n + 2, 2); le32(stream + n + 4, 0x1000); n += 8;
    size_t big = 2u * 1024 * 1024; size_t bigunits = big / 4;
    memset(stream + n, 0, big); stream[n] = 72; stream[n + 1] = 2; le16(stream + n + 2, 0); le32(stream + n + 4, (uint32_t)bigunits);
    le32(stream + n + 8, 0x1000); le32(stream + n + 12, 1); le16(stream + n + 16, 40); le16(stream + n + 18, 30); le16(stream + n + 20, 10); le16(stream + n + 22, 20);
    uint32_t rng = 7; for (size_t i = 28; i < big; ++i) { rng = rng * 1664525u + 1013904223u; stream[n + i] = (uint8_t)(rng >> 24); }
    n += big;
    send_all(cl, stream, n);
    WAIT(fake.c[0].rx_len >= n);
    assert(fake.c[0].rx_len == n && memcmp(fake.c[0].rx, stream, n) == 0);                      /* byte for byte */

    /* what the program drew was seen: a mapped window and the image */
    {
        ballum_element_t el[8]; char txt[64]; int sett;
        size_t ne = 0;
        for (int t = 0; t < 400 && ne == 0; ++t) { nap(5); ne = ballum_xdraw_snapshot(xd, 200, 100, 1u << 30, 0, el, 8, txt, sizeof(txt), &sett); }
        assert(ne == 1 && el[0].kind == BALLUM_ELEM_IMAGE && el[0].rect.x == 10 && el[0].rect.y == 20 && el[0].rect.width == 40 && el[0].rect.height == 30);
    }

    /* the server's bytes reach the program unchanged: an event and a long reply */
    {
        size_t rl = 32 + 4 * 30000;
        uint8_t *srv = (uint8_t *)calloc(1, 32 + rl);
        srv[0] = 19; le32(srv + 4, 0x1000); le32(srv + 8, 0x1000);                               /* MapNotify */
        srv[32] = 1; le16(srv + 34, 5); le32(srv + 36, 30000);                                   /* a reply nobody asked for */
        for (size_t i = 64; i < 32 + rl; ++i) srv[i] = (uint8_t)(i * 31);
        fake_send(&fake, 0, srv, 32 + rl);
        uint8_t *got = (uint8_t *)malloc(32 + rl);
        assert(recv_exact(cl, got, 32 + rl, 3000) == 32 + rl && memcmp(got, srv, 32 + rl) == 0);
        free(got); free(srv);
    }

    ballum_x11proxy_stats_t ps; ballum_x11proxy_stats(px, &ps);
    assert(ps.connections == 1 && ps.bytes_to_server >= n && ps.bytes_to_client > 120000 && ps.unobserved == 0);

    /* ---- a stream the framer cannot follow is still relayed, just not observed ------------------ */
    int c2 = dial(listen_path); assert(c2 >= 0);
    send_all(c2, setup, sl);
    assert(recv_exact(c2, rep, 8, 2000) == 8);
    uint8_t junk[64]; memset(junk, 0, sizeof(junk)); junk[0] = 70; le16(junk + 2, 0); le32(junk + 4, 1);   /* an impossible BIG length */
    send_all(c2, junk, sizeof(junk));
    WAIT(fake.n >= 2 && fake.c[1].rx_len >= sizeof(junk));
    assert(memcmp(fake.c[1].rx, junk, sizeof(junk)) == 0);
    ballum_x11proxy_stats(px, &ps);
    WAIT((ballum_x11proxy_stats(px, &ps), ps.unobserved == 1));

    /* ---- misuse is refused ----------------------------------------------------------------------- */
    int c3 = dial(listen_path); assert(c3 >= 0);                                                     /* third: at the limit of 3 */
    int c4 = dial(listen_path); assert(c4 >= 0);                                                     /* fourth: refused */
    assert(closed_by_peer(c4));
    close(c3);
    int bad = dial(listen_path); assert(bad >= 0);
    uint8_t garbage[16] = "this is not X11!";
    send_try(bad, garbage, sizeof(garbage));
    assert(closed_by_peer(bad));
    ballum_x11proxy_stats(px, &ps);
    assert(ps.rejected >= 2);
    close(bad); close(c4);

    /* the program hanging up closes the server side too */
    close(cl);
    WAIT(fake.n >= 1);
    { int closed = 0; for (int t = 0; t < 200 && !closed; ++t) { struct pollfd pf = { fake.c[0].fd, POLLIN, 0 }; if (poll(&pf, 1, 20) > 0) { char b[8]; closed = recv(fake.c[0].fd, b, sizeof(b), MSG_DONTWAIT) == 0; } } assert(closed); }
    close(c2);

    /* ---- stop removes the socket, and a second proxy can take its place ---------------------------- */
    ballum_x11proxy_stop(px);
    assert(stat(listen_path, &sb) != 0);
    ballum_x11proxy_stop(NULL);
    cfg.cookie = NULL; cfg.cookie_len = 0;
    assert(ballum_x11proxy_start(&cfg, &px) == BALLUM_STATUS_OK);
    { int c = dial(listen_path); assert(c >= 0);
      send_all(c, setup, sl); assert(recv_exact(c, rep, 8, 2000) == 8);
      WAIT(fake.n >= 3 && fake.c[fake.n - 1].setup_seen);
      assert(memcmp(fake.c[fake.n - 1].auth_data, wrong, 16) == 0);                                   /* no cookie configured: passed through */
      close(c); }
    ballum_x11proxy_stop(px);

    fake_stop(&fake);
    ballum_xdraw_destroy(xd);
    free(stream);
    unlink(server_path); rmdir(dir);
    puts("test_ballum_x11proxy: ok");
    return 0;
}
