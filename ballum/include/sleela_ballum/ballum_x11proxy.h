/*
 * ballum/include/sleela_ballum/ballum_x11proxy.h
 * The hook in the standard X11 stack: a relay that sits on the unix socket between
 * a hosted program and the X server and lets Ballum SEE what the program draws.
 *
 *   program  --(X11 over a unix socket)-->  PROXY  --(same bytes)-->  X server
 *                                             |
 *                                  ballum_xwire (frames the bytes)
 *                                  ballum_xdraw (what was drawn: text, fills, images)
 *
 * The proxy forwards every byte in both directions, unchanged and in order. It
 * never alters, drops, delays on purpose or reorders what the program and the
 * server say to each other; the program cannot tell it is there. Observation is
 * all it does. ACTING on what it saw happens afterwards, on pixels, in the
 * interpreter, so the program's own picture is always the unmodified reference.
 *
 * One thing it does change, deliberately: the connection-setup request. The
 * program is given no X credentials; the proxy's own directory permissions are
 * the boundary. The proxy replaces the setup's authorization with the cookie it
 * was given for the real server, so the program never holds that cookie.
 *
 * Safety
 *   - The listening socket is created mode 0600 and only a peer with the same
 *     effective user id is served (checked with SO_PEERCRED); anything else is
 *     closed at once.
 *   - At most max_connections at a time (default 16).
 *   - Each direction is buffered to 4 MiB at most; past that the proxy stops
 *     reading the sender, so a stalled receiver pushes back instead of growing
 *     memory without bound.
 *   - A stream the framer cannot follow stops being OBSERVED but is still
 *     relayed: observation failing never breaks the program.
 *
 * Linux and other POSIX systems with unix sockets and pthreads. Elsewhere
 * ballum_x11proxy_start returns UNSUPPORTED.
 */
#ifndef SLEELA_BALLUM_X11PROXY_H
#define SLEELA_BALLUM_X11PROXY_H

#include "ballum_manager.h"
#include "ballum_xdraw.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ballum_x11proxy ballum_x11proxy_t;

typedef struct {
    const char       *listen_path;       /* unix socket to create for the program        */
    const char       *server_path;       /* the real X server's unix socket              */
    const uint8_t    *cookie;            /* MIT-MAGIC-COOKIE-1 for the server, or NULL   */
    size_t            cookie_len;        /* 16 when cookie is given                      */
    ballum_xdraw_t   *draw;              /* receives what the program draws (required)   */
    int               max_connections;   /* 0 = 16                                       */
} ballum_x11proxy_config_t;

typedef struct {
    uint64_t connections;                /* accepted and served                          */
    uint64_t rejected;                   /* refused: wrong user, bad setup, too many     */
    uint64_t bytes_to_server;
    uint64_t bytes_to_client;
    uint64_t unobserved;                 /* connections whose stream could not be framed */
} ballum_x11proxy_stats_t;

/* Create the socket and start relaying on a background thread. On failure the
 * status says why (INVALID_ARGUMENT, PERMISSION_DENIED for the socket,
 * BACKEND_ERROR) and nothing is left running. */
ballum_status_t ballum_x11proxy_start(const ballum_x11proxy_config_t *cfg, ballum_x11proxy_t **out);

/* Stop the thread, close every connection and remove the socket. NULL is allowed. */
void ballum_x11proxy_stop(ballum_x11proxy_t *p);

void ballum_x11proxy_stats(ballum_x11proxy_t *p, ballum_x11proxy_stats_t *out);

#ifdef __cplusplus
}
#endif

#endif /* SLEELA_BALLUM_X11PROXY_H */
