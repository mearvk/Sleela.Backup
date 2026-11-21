/*
 * ballum/src/ballum_xwire.c -- the X11 wire framer (see ballum_xwire.h).
 */
#include "sleela_ballum/ballum_xwire.h"

#include <stdlib.h>
#include <string.h>

#define MAX_REQUEST_BYTES (128u * 1024u * 1024u)
#define MAX_EXTENSIONS    32
#define EXT_NAME_BYTES    24
#define PENDING           256

typedef struct { uint8_t *p; size_t start, len, cap; size_t skip; } stream_t;

typedef struct {
    int      used;
    uint16_t seq;
    uint8_t  opcode, data;
    const char *ext;
    uint32_t saved;
    int      want_reply;
    int      wants_cb;        /* the request handler asked for the reply */
    int      is_query_ext;
    char     qname[EXT_NAME_BYTES];
} pending_t;

struct ballum_xwire {
    ballum_xwire_cb_t cb;
    int       big_endian;
    int       client_run, server_run, desync;
    stream_t  c, s;
    uint16_t  seq;
    uint64_t  nreq;
    pending_t pend[PENDING];
    struct { char name[EXT_NAME_BYTES]; uint8_t major; int used; } ext[MAX_EXTENSIONS];
};

static uint16_t rd16(const uint8_t *p, int be) { return be ? (uint16_t)((p[0] << 8) | p[1]) : (uint16_t)((p[1] << 8) | p[0]); }
static uint32_t rd32(const uint8_t *p, int be) {
    return be ? ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]
              : ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0];
}
static size_t pad4(size_t n) { return (n + 3u) & ~(size_t)3u; }

ballum_xwire_t *ballum_xwire_create(const ballum_xwire_cb_t *cb) {
    if (!cb) return NULL;
    ballum_xwire_t *w = (ballum_xwire_t *)calloc(1, sizeof(*w));
    if (!w) return NULL;
    w->cb = *cb;
    return w;
}

void ballum_xwire_destroy(ballum_xwire_t *w) {
    if (!w) return;
    free(w->c.p); free(w->s.p);
    free(w);
}

int ballum_xwire_desynced(const ballum_xwire_t *w) { return w ? w->desync : 1; }
int ballum_xwire_big_endian(const ballum_xwire_t *w) { return w ? w->big_endian : 0; }
uint64_t ballum_xwire_request_count(const ballum_xwire_t *w) { return w ? w->nreq : 0u; }

/* ---- stream buffer -------------------------------------------------------------- */

static int append(stream_t *s, const uint8_t *d, size_t n) {
    if (s->start > 0 && (s->start == s->len || s->start > s->cap / 2)) {        /* compact */
        memmove(s->p, s->p + s->start, s->len - s->start);
        s->len -= s->start; s->start = 0;
    }
    if (s->len + n > s->cap) {
        size_t nc = s->cap ? s->cap : 4096u;
        while (nc < s->len + n) nc *= 2u;
        uint8_t *np = (uint8_t *)realloc(s->p, nc);
        if (!np) return 0;
        s->p = np; s->cap = nc;
    }
    memcpy(s->p + s->len, d, n);
    s->len += n;
    return 1;
}
static size_t avail(const stream_t *s) { return s->len - s->start; }
static const uint8_t *head(const stream_t *s) { return s->p + s->start; }
static void drop(stream_t *s, size_t n) { s->start += n; if (s->start >= s->len) { s->start = s->len = 0; } }

/* ---- setup request -------------------------------------------------------------- */

long ballum_xwire_setup_request_size(const uint8_t *b, size_t n) {
    if (n < 12) return (n >= 1 && b[0] != 'l' && b[0] != 'B') ? -1 : 0;
    int be = b[0] == 'B';
    if (b[0] != 'l' && b[0] != 'B') return -1;
    if (rd16(b + 2, be) != 11) return -1;                     /* protocol major version 11 */
    size_t nl = rd16(b + 6, be), dl = rd16(b + 8, be);
    if (nl > 256 || dl > 4096) return -1;
    size_t total = 12u + pad4(nl) + pad4(dl);
    return n >= total ? (long)total : 0;
}

size_t ballum_xwire_build_setup(uint8_t *out, size_t cap, int be, const char *name, const uint8_t *data, size_t dl) {
    size_t nl = name ? strlen(name) : 0u;
    if (!out || nl > 256 || dl > 4096 || (dl && !data)) return 0;
    size_t total = 12u + pad4(nl) + pad4(dl);
    if (cap < total) return 0;
    memset(out, 0, total);
    out[0] = be ? 'B' : 'l';
    if (be) { out[2] = 0; out[3] = 11; out[6] = (uint8_t)(nl >> 8); out[7] = (uint8_t)nl; out[8] = (uint8_t)(dl >> 8); out[9] = (uint8_t)dl; }
    else    { out[2] = 11; out[3] = 0; out[6] = (uint8_t)nl; out[7] = (uint8_t)(nl >> 8); out[8] = (uint8_t)dl; out[9] = (uint8_t)(dl >> 8); }
    if (nl) memcpy(out + 12, name, nl);
    if (dl) memcpy(out + 12 + pad4(nl), data, dl);
    return total;
}

/* ---- extensions and pending replies ------------------------------------------------ */

static const char *ext_name(const ballum_xwire_t *w, uint8_t major) {
    if (major < 128) return NULL;
    for (int i = 0; i < MAX_EXTENSIONS; ++i)
        if (w->ext[i].used && w->ext[i].major == major) return w->ext[i].name;
    return NULL;
}

static void remember_ext(ballum_xwire_t *w, const char *name, uint8_t major) {
    int slot = -1;
    for (int i = 0; i < MAX_EXTENSIONS; ++i) {
        if (w->ext[i].used && strcmp(w->ext[i].name, name) == 0) { slot = i; break; }
        if (!w->ext[i].used && slot < 0) slot = i;
    }
    if (slot < 0) return;
    w->ext[slot].used = 1;
    strncpy(w->ext[slot].name, name, EXT_NAME_BYTES - 1);
    w->ext[slot].name[EXT_NAME_BYTES - 1] = '\0';
    w->ext[slot].major = major;
}

/* ---- requests ---------------------------------------------------------------------- */

static void deliver_request(ballum_xwire_t *w, const uint8_t *msg, size_t hdr, size_t total, size_t view) {
    ballum_xreq_t r;
    memset(&r, 0, sizeof(r));
    r.big_endian = w->big_endian;
    r.opcode = msg[0]; r.data = msg[1];
    r.seq = ++w->seq;
    r.ext = ext_name(w, r.opcode);
    r.body = msg + hdr; r.body_len = view > hdr ? view - hdr : 0u;
    r.total_len = total; r.truncated = total > view;
    w->nreq++;

    pending_t *p = &w->pend[r.seq % PENDING];
    memset(p, 0, sizeof(*p));
    if (r.opcode == 98 && r.body_len >= 4) {                   /* QueryExtension */
        size_t nl = rd16(r.body, w->big_endian);
        if (nl > 0 && nl < EXT_NAME_BYTES && r.body_len >= 4 + nl) {
            p->used = 1; p->seq = r.seq; p->opcode = 98; p->want_reply = 1; p->is_query_ext = 1;
            memcpy(p->qname, r.body + 4, nl); p->qname[nl] = '\0';
        }
    }
    uint32_t save = 0;
    int want = w->cb.request ? w->cb.request(w->cb.user, &r, &save) : 0;
    if (want) {
        p->used = 1; p->seq = r.seq; p->opcode = r.opcode; p->data = r.data; p->ext = r.ext;
        p->saved = save; p->want_reply = 1; p->wants_cb = 1;
    }
}

static void parse_client(ballum_xwire_t *w) {
    stream_t *s = &w->c;
    for (;;) {
        if (w->desync) return;
        if (s->skip) {
            size_t take = avail(s) < s->skip ? avail(s) : s->skip;
            drop(s, take); s->skip -= take;
            if (s->skip) return;
        }
        size_t have = avail(s);
        if (!w->client_run) {
            if (have == 0) return;
            long sz = ballum_xwire_setup_request_size(head(s), have);
            if (sz < 0) { w->desync = 1; return; }
            if (sz == 0) return;
            w->big_endian = head(s)[0] == 'B';
            drop(s, (size_t)sz);
            w->client_run = 1;
            continue;
        }
        if (have < 4) return;
        const uint8_t *m = head(s);
        size_t hdr = 4, total;
        uint16_t l16 = rd16(m + 2, w->big_endian);
        if (l16 == 0) {                                        /* BIG-REQUESTS: a 32-bit length follows */
            if (have < 8) return;
            uint32_t ext = rd32(m + 4, w->big_endian);
            if (ext < 2 || (uint64_t)ext * 4u > MAX_REQUEST_BYTES) { w->desync = 1; return; }
            total = (size_t)ext * 4u; hdr = 8;
        } else {
            total = (size_t)l16 * 4u;
        }
        size_t view = total < BALLUM_XWIRE_VIEW_MAX ? total : BALLUM_XWIRE_VIEW_MAX;
        if (have < view) return;                               /* wait for the rest of what we will show */
        deliver_request(w, head(s), hdr, total, view);
        drop(s, view);
        s->skip = total - view;
    }
}

void ballum_xwire_client_feed(ballum_xwire_t *w, const uint8_t *d, size_t n) {
    if (!w || !d || n == 0 || w->desync) return;
    if (w->c.skip) {                                           /* inside a long request we are not buffering */
        size_t take = n < w->c.skip ? n : w->c.skip;
        w->c.skip -= take; d += take; n -= take;
        if (n == 0) return;
    }
    if (!append(&w->c, d, n)) { w->desync = 1; return; }
    parse_client(w);
}

/* ---- replies and events ---------------------------------------------------------------- */

static void deliver_reply(ballum_xwire_t *w, const uint8_t *m, size_t view, size_t total) {
    uint16_t seq = rd16(m + 2, w->big_endian);
    pending_t *p = &w->pend[seq % PENDING];
    if (!p->used || !p->want_reply || p->seq != seq) return;
    p->want_reply = 0;
    if (p->is_query_ext && view >= 12 && m[8] && m[9] >= 128) remember_ext(w, p->qname, m[9]);
    if (p->wants_cb && w->cb.reply) {
        ballum_xreply_t r;
        memset(&r, 0, sizeof(r));
        r.big_endian = w->big_endian; r.opcode = p->opcode; r.data = p->data; r.ext = p->ext;
        r.saved = p->saved; r.seq = seq; r.bytes = m; r.len = view; r.truncated = total > view;
        w->cb.reply(w->cb.user, &r);
    }
}

static void parse_server(ballum_xwire_t *w) {
    stream_t *s = &w->s;
    for (;;) {
        if (w->desync) return;
        if (s->skip) {
            size_t take = avail(s) < s->skip ? avail(s) : s->skip;
            drop(s, take); s->skip -= take;
            if (s->skip) return;
        }
        size_t have = avail(s);
        if (!w->server_run) {
            if (have < 8) return;
            const uint8_t *m = head(s);
            size_t total = 8u + (size_t)rd16(m + 6, w->big_endian) * 4u;
            if (have < total) return;
            drop(s, total);
            w->server_run = 1;
            continue;
        }
        if (have < 32) return;
        const uint8_t *m = head(s);
        uint8_t type = m[0] & 0x7Fu;
        size_t total = 32;
        if (m[0] == 1 || type == 35) {
            uint32_t extra = rd32(m + 4, w->big_endian);
            if ((uint64_t)extra * 4u > MAX_REQUEST_BYTES) { w->desync = 1; return; }
            total = 32u + (size_t)extra * 4u;
        }
        size_t view = total < BALLUM_XWIRE_VIEW_MAX ? total : BALLUM_XWIRE_VIEW_MAX;
        if (have < view) return;
        if (m[0] == 1) deliver_reply(w, m, view, total);
        else if (m[0] != 0 && w->cb.event) w->cb.event(w->cb.user, m, view, w->big_endian);
        drop(s, view);
        s->skip = total - view;
    }
}

void ballum_xwire_server_feed(ballum_xwire_t *w, const uint8_t *d, size_t n) {
    if (!w || !d || n == 0 || w->desync) return;
    if (w->s.skip) {
        size_t take = n < w->s.skip ? n : w->s.skip;
        w->s.skip -= take; d += take; n -= take;
        if (n == 0) return;
    }
    if (!append(&w->s, d, n)) { w->desync = 1; return; }
    parse_server(w);
}
