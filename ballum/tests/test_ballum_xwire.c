/* Tests for the X11 wire framer: setup, request framing (normal, BIG-REQUESTS,
 * truncated), extension discovery, reply matching, events, both byte orders,
 * and refusal of a stream it cannot follow. */
#include "sleela_ballum/ballum_xwire.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int      n_req, n_rep, n_ev;
    int      want_opcode;                 /* request handler asks for replies to this opcode */
    uint8_t  opcodes[64]; uint8_t datas[64]; uint16_t seqs[64];
    const char *exts[64];
    size_t   body_lens[64]; size_t totals[64]; int truncs[64];
    uint8_t  body0[64];
    uint32_t last_saved; uint16_t last_rep_seq; size_t last_rep_len; uint8_t last_rep_op; int last_rep_trunc;
    uint8_t  last_ev_type; size_t last_ev_len;
} cap_t;

static int on_request(void *u, const ballum_xreq_t *r, uint32_t *save) {
    cap_t *c = (cap_t *)u;
    if (c->n_req < 64) {
        c->opcodes[c->n_req] = r->opcode; c->datas[c->n_req] = r->data; c->seqs[c->n_req] = r->seq;
        c->exts[c->n_req] = r->ext; c->body_lens[c->n_req] = r->body_len; c->totals[c->n_req] = r->total_len;
        c->truncs[c->n_req] = r->truncated; c->body0[c->n_req] = r->body_len ? r->body[0] : 0;
    }
    c->n_req++;
    if (c->want_opcode >= 0 && r->opcode == c->want_opcode) { *save = 0xABCD0000u | r->seq; return 1; }
    return 0;
}
static void on_reply(void *u, const ballum_xreply_t *r) {
    cap_t *c = (cap_t *)u;
    c->n_rep++; c->last_saved = r->saved; c->last_rep_seq = r->seq; c->last_rep_len = r->len;
    c->last_rep_op = r->opcode; c->last_rep_trunc = r->truncated;
}
static void on_event(void *u, const uint8_t *ev, size_t len, int be) {
    cap_t *c = (cap_t *)u; (void)be;
    c->n_ev++; c->last_ev_type = ev[0]; c->last_ev_len = len;
}

static ballum_xwire_t *mk(cap_t *c) {
    memset(c, 0, sizeof(*c));
    c->want_opcode = -1;
    ballum_xwire_cb_t cb = { on_request, on_reply, on_event, c };
    ballum_xwire_t *w = ballum_xwire_create(&cb);
    assert(w);
    return w;
}

static void put16(uint8_t *p, uint16_t v, int be) { if (be) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; } else { p[1] = (uint8_t)(v >> 8); p[0] = (uint8_t)v; } }
static void put32(uint8_t *p, uint32_t v, int be) { put16(p, (uint16_t)(be ? v >> 16 : v), be); put16(p + 2, (uint16_t)(be ? v : v >> 16), be); }

/* A request of `units` 4-byte units; body[0] = tag. */
static size_t make_req(uint8_t *out, uint8_t opcode, uint8_t data, size_t units, uint8_t tag, int be) {
    memset(out, 0, units * 4);
    out[0] = opcode; out[1] = data; put16(out + 2, (uint16_t)units, be);
    if (units > 1) out[4] = tag;
    return units * 4;
}
static size_t make_big(uint8_t *out, uint8_t opcode, uint8_t data, size_t units, uint8_t tag, int be) {
    memset(out, 0, units * 4);
    out[0] = opcode; out[1] = data; put16(out + 2, 0, be); put32(out + 4, (uint32_t)units, be);
    out[8] = tag;
    return units * 4;
}
static size_t make_setup_reply(uint8_t *out, int be, size_t extra_units) {
    memset(out, 0, 8 + extra_units * 4);
    out[0] = 1; put16(out + 2, 11, be); put16(out + 6, (uint16_t)extra_units, be);
    return 8 + extra_units * 4;
}
static size_t make_reply(uint8_t *out, uint16_t seq, size_t extra_units, int be) {
    memset(out, 0, 32 + extra_units * 4);
    out[0] = 1; put16(out + 2, seq, be); put32(out + 4, (uint32_t)extra_units, be);
    return 32 + extra_units * 4;
}
static size_t make_event(uint8_t *out, uint8_t type) { memset(out, 0, 32); out[0] = type; return 32; }

static void feed_bytewise_c(ballum_xwire_t *w, const uint8_t *d, size_t n) { for (size_t i = 0; i < n; ++i) ballum_xwire_client_feed(w, d + i, 1); }
static void feed_bytewise_s(ballum_xwire_t *w, const uint8_t *d, size_t n) { for (size_t i = 0; i < n; ++i) ballum_xwire_server_feed(w, d + i, 1); }

int main(void) {
    static uint8_t stream[8 * 1024 * 1024];

    /* ---- the setup request ------------------------------------------------------------- */
    {
        uint8_t su[256];
        static const uint8_t cookie[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
        size_t n = ballum_xwire_build_setup(su, sizeof(su), 0, "MIT-MAGIC-COOKIE-1", cookie, 16);
        assert(n == 12 + 20 + 16 && su[0] == 'l' && su[2] == 11);
        assert(ballum_xwire_setup_request_size(su, n) == (long)n);
        assert(ballum_xwire_setup_request_size(su, n - 1) == 0);                    /* more bytes needed */
        assert(ballum_xwire_setup_request_size(su, 5) == 0);
        assert(ballum_xwire_setup_request_size(su, 0) == 0);
        su[0] = 'X';                                                                 assert(ballum_xwire_setup_request_size(su, n) == -1);
        su[0] = 'l'; su[2] = 10;                                                     assert(ballum_xwire_setup_request_size(su, n) == -1);   /* not protocol 11 */
        size_t m = ballum_xwire_build_setup(su, sizeof(su), 1, NULL, NULL, 0);
        assert(m == 12 && su[0] == 'B' && ballum_xwire_setup_request_size(su, m) == 12);
        assert(ballum_xwire_build_setup(su, 10, 0, NULL, NULL, 0) == 0);           /* does not fit */
        assert(ballum_xwire_build_setup(NULL, 100, 0, NULL, NULL, 0) == 0);
        assert(ballum_xwire_build_setup(su, sizeof(su), 0, "x", NULL, 5) == 0);    /* data length without data */
    }

    /* ---- requests: byte order, bytewise == whole, sequence numbers ----------------------- */
    for (int be = 0; be < 2; ++be) {
        cap_t c; ballum_xwire_t *w = mk(&c);
        size_t n = 0;
        n += ballum_xwire_build_setup(stream + n, 256, be, NULL, NULL, 0);
        n += make_req(stream + n, 70, 0, 5, 0x11, be);
        n += make_req(stream + n, 76, 9, 3, 0x22, be);
        n += make_req(stream + n, 53, 24, 1, 0, be);                                 /* header only */
        feed_bytewise_c(w, stream, n);
        assert(!ballum_xwire_desynced(w) && ballum_xwire_big_endian(w) == be);
        assert(c.n_req == 3 && ballum_xwire_request_count(w) == 3);
        assert(c.opcodes[0] == 70 && c.opcodes[1] == 76 && c.opcodes[2] == 53);
        assert(c.datas[1] == 9 && c.seqs[0] == 1 && c.seqs[1] == 2 && c.seqs[2] == 3);
        assert(c.body_lens[0] == 16 && c.body0[0] == 0x11 && c.totals[0] == 20 && !c.truncs[0]);   /* body is past the 4-byte header */
        assert(c.body_lens[2] == 0 && c.exts[0] == NULL);
        ballum_xwire_destroy(w);

        cap_t c2; w = mk(&c2);                                                       /* the same bytes in one feed */
        ballum_xwire_client_feed(w, stream, n);
        assert(c2.n_req == 3 && memcmp(c.opcodes, c2.opcodes, 3) == 0 && memcmp(c.seqs, c2.seqs, 6) == 0);
        ballum_xwire_destroy(w);
    }

    /* ---- BIG-REQUESTS and long requests ------------------------------------------------- */
    {
        cap_t c; ballum_xwire_t *w = mk(&c);
        size_t n = ballum_xwire_build_setup(stream, 256, 0, NULL, NULL, 0);
        n += make_big(stream + n, 72, 2, 600000, 0x33, 0);                           /* 2.4 MB: shown truncated */
        size_t after = n;
        n += make_req(stream + n, 12, 0, 3, 0x44, 0);                                /* must still be framed */
        size_t pos = 0;
        while (pos < n) { size_t k = n - pos < 7001 ? n - pos : 7001; ballum_xwire_client_feed(w, stream + pos, k); pos += k; }
        assert(!ballum_xwire_desynced(w) && c.n_req == 2);
        assert(c.opcodes[0] == 72 && c.truncs[0] && c.totals[0] == 2400000 && c.body_lens[0] == BALLUM_XWIRE_VIEW_MAX - 8);
        assert(c.body0[0] == 0x33);                                                  /* body skips the 8-byte header */
        assert(c.opcodes[1] == 12 && c.body0[1] == 0x44 && !c.truncs[1] && after > 0);
        ballum_xwire_destroy(w);

        /* a big request that fits the view is complete, not truncated */
        cap_t c2; w = mk(&c2);
        n = ballum_xwire_build_setup(stream, 256, 1, NULL, NULL, 0);
        n += make_big(stream + n, 72, 2, 1000, 0x55, 1);
        ballum_xwire_client_feed(w, stream, n);
        assert(c2.n_req == 1 && !c2.truncs[0] && c2.totals[0] == 4000 && c2.body_lens[0] == 4000 - 8);
        ballum_xwire_destroy(w);
    }

    /* ---- extensions and replies ----------------------------------------------------------- */
    for (int be = 0; be < 2; ++be) {
        cap_t c; ballum_xwire_t *w = mk(&c);
        c.want_opcode = 47;                                                          /* ask for replies to QueryFont */
        size_t n = ballum_xwire_build_setup(stream, 256, be, NULL, NULL, 0);
        /* QueryExtension("RENDER"): body = name length (2), pad (2), name padded */
        uint8_t qe[16]; memset(qe, 0, sizeof(qe));
        qe[0] = 98; put16(qe + 2, 4, be); put16(qe + 4, 6, be); memcpy(qe + 8, "RENDER", 6);
        memcpy(stream + n, qe, 16); n += 16;                                         /* seq 1 */
        n += make_req(stream + n, 47, 0, 2, 0x66, be);                               /* seq 2: QueryFont */
        n += make_req(stream + n, 47, 0, 2, 0x67, be);                               /* seq 3: QueryFont */
        feed_bytewise_c(w, stream, n);
        assert(c.n_req == 3);

        uint8_t sv[512]; size_t m = make_setup_reply(sv, be, 3);
        feed_bytewise_s(w, sv, m);                                                    /* setup reply consumed */
        assert(c.n_rep == 0 && c.n_ev == 0);
        m = make_reply(sv, 1, 0, be); sv[8] = 1; sv[9] = 140;                         /* RENDER is present, major 140 */
        ballum_xwire_server_feed(w, sv, m);
        assert(c.n_rep == 0);                                                         /* the framer's own business; not delivered */

        /* a request for the extension now carries its name and minor opcode */
        uint8_t ext[8]; memset(ext, 0, sizeof(ext)); ext[0] = 140; ext[1] = 20; put16(ext + 2, 2, be);
        ballum_xwire_client_feed(w, ext, 8);
        assert(c.n_req == 4 && c.exts[3] && strcmp(c.exts[3], "RENDER") == 0 && c.datas[3] == 20);
        assert(c.exts[0] == NULL && c.opcodes[0] == 98);

        /* a reply with extra data, matched to seq 3 (it carries what the handler saved) */
        m = make_reply(sv, 3, 5, be);
        feed_bytewise_s(w, sv, m);
        assert(c.n_rep == 1 && c.last_saved == (0xABCD0000u | 3) && c.last_rep_seq == 3 && c.last_rep_len == 32 + 20 && c.last_rep_op == 47);
        /* a reply that nothing asked for is ignored; an error and an event are not replies */
        m = make_reply(sv, 99, 0, be); ballum_xwire_server_feed(w, sv, m);
        assert(c.n_rep == 1);
        m = make_event(sv, 0); sv[1] = 8; ballum_xwire_server_feed(w, sv, m);          /* an error */
        assert(c.n_ev == 0);
        m = make_event(sv, 12 | 0x80); ballum_xwire_server_feed(w, sv, m);             /* an event with send-event set */
        assert(c.n_ev == 1 && c.last_ev_type == (12 | 0x80) && c.last_ev_len == 32);
        /* a generic event carries extra length */
        m = make_event(sv, 35); put32(sv + 4, 2, be); memset(sv + 32, 0, 8);
        ballum_xwire_server_feed(w, sv, 40);
        assert(c.n_ev == 2 && c.last_ev_len == 40);
        /* then the stream is still in step */
        m = make_reply(sv, 2, 0, be);
        ballum_xwire_server_feed(w, sv, m);
        assert(c.n_rep == 2 && c.last_saved == (0xABCD0000u | 2));
        ballum_xwire_destroy(w);
    }

    /* an extension that is absent is not recorded; unknown majors stay core-looking */
    {
        cap_t c; ballum_xwire_t *w = mk(&c);
        size_t n = ballum_xwire_build_setup(stream, 256, 0, NULL, NULL, 0);
        uint8_t qe[12]; memset(qe, 0, sizeof(qe)); qe[0] = 98; put16(qe + 2, 3, 0); put16(qe + 4, 4, 0); memcpy(qe + 8, "NOPE", 4);
        memcpy(stream + n, qe, 12); n += 12;
        ballum_xwire_client_feed(w, stream, n);
        uint8_t sv[64]; size_t m = make_setup_reply(sv, 0, 0); ballum_xwire_server_feed(w, sv, m);
        m = make_reply(sv, 1, 0, 0); sv[8] = 0; ballum_xwire_server_feed(w, sv, m);     /* present = 0 */
        uint8_t ext[4] = { 150, 1, 1, 0 };
        ballum_xwire_client_feed(w, ext, 4);
        assert(c.n_req == 2 && c.exts[1] == NULL);
        ballum_xwire_destroy(w);
    }

    /* a long reply is delivered truncated and the stream stays in step */
    {
        cap_t c; ballum_xwire_t *w = mk(&c);
        c.want_opcode = 73;                                                           /* GetImage */
        size_t n = ballum_xwire_build_setup(stream, 256, 0, NULL, NULL, 0);
        n += make_req(stream + n, 73, 2, 2, 0, 0);
        ballum_xwire_client_feed(w, stream, n);
        uint8_t *sv = (uint8_t *)malloc(40 + 4 * 400000 + 64);
        size_t m = make_setup_reply(sv, 0, 0); ballum_xwire_server_feed(w, sv, m);
        m = make_reply(sv, 1, 400000, 0);
        size_t ev = make_event(sv + m, 2);
        size_t pos = 0, total = m + ev;
        while (pos < total) { size_t k = total - pos < 4093 ? total - pos : 4093; ballum_xwire_server_feed(w, sv + pos, k); pos += k; }
        assert(c.n_rep == 1 && c.last_rep_trunc && c.last_rep_len == BALLUM_XWIRE_VIEW_MAX);
        assert(c.n_ev == 1 && c.last_ev_type == 2);
        free(sv);
        ballum_xwire_destroy(w);
    }

    /* ---- streams the framer refuses to guess about ---------------------------------------------- */
    {
        cap_t c; ballum_xwire_t *w = mk(&c);
        uint8_t junk[12] = { 'Z', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
        ballum_xwire_client_feed(w, junk, sizeof(junk));
        assert(ballum_xwire_desynced(w));
        size_t n = make_req(stream, 70, 0, 2, 1, 0);
        ballum_xwire_client_feed(w, stream, n);                                         /* ignored from now on */
        assert(c.n_req == 0);
        ballum_xwire_destroy(w);

        w = mk(&c);                                                                     /* an extended length of 1 is impossible */
        n = ballum_xwire_build_setup(stream, 256, 0, NULL, NULL, 0);
        memset(stream + n, 0, 8); stream[n] = 70; put32(stream + n + 4, 1, 0); n += 8;
        ballum_xwire_client_feed(w, stream, n);
        assert(ballum_xwire_desynced(w) && c.n_req == 0);
        ballum_xwire_destroy(w);

        w = mk(&c);                                                                     /* and one over the sanity limit */
        n = ballum_xwire_build_setup(stream, 256, 0, NULL, NULL, 0);
        memset(stream + n, 0, 8); stream[n] = 70; put32(stream + n + 4, 0x7FFFFFFFu, 0); n += 8;
        ballum_xwire_client_feed(w, stream, n);
        assert(ballum_xwire_desynced(w));
        ballum_xwire_destroy(w);

        w = mk(&c);                                                                     /* a reply claiming a huge length */
        n = ballum_xwire_build_setup(stream, 256, 0, NULL, NULL, 0);
        ballum_xwire_client_feed(w, stream, n);
        uint8_t sv[64]; size_t m = make_setup_reply(sv, 0, 0); ballum_xwire_server_feed(w, sv, m);
        m = make_reply(sv, 1, 0, 0); put32(sv + 4, 0x7FFFFFFFu, 0); ballum_xwire_server_feed(w, sv, m);
        assert(ballum_xwire_desynced(w));
        ballum_xwire_destroy(w);
    }

    /* ---- argument handling ------------------------------------------------------------------------ */
    assert(ballum_xwire_create(NULL) == NULL);
    assert(ballum_xwire_desynced(NULL) == 1 && ballum_xwire_request_count(NULL) == 0 && ballum_xwire_big_endian(NULL) == 0);
    ballum_xwire_client_feed(NULL, stream, 4);
    ballum_xwire_server_feed(NULL, stream, 4);
    ballum_xwire_destroy(NULL);
    {
        cap_t c; ballum_xwire_t *w = mk(&c);
        ballum_xwire_client_feed(w, NULL, 10);
        ballum_xwire_client_feed(w, stream, 0);
        assert(!ballum_xwire_desynced(w));
        ballum_xwire_destroy(w);
    }

    puts("test_ballum_xwire: ok");
    return 0;
}
