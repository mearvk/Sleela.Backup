/*
 * ballum/include/sleela_ballum/ballum_xwire.h
 * The X11 wire framer: the first step of Ballum's hook into the standard X11
 * stack. It reads the byte streams of ONE client connection, in both directions,
 * and cuts them into protocol messages, without changing or delaying a byte (a
 * proxy relays the bytes itself; the framer only watches).
 *
 *   client -> server : the setup request, then requests (BIG-REQUESTS aware)
 *   server -> client : the setup reply, then replies, errors and events
 *
 * It follows QueryExtension so a request for an extension (RENDER, ...) carries
 * the extension's name, and it matches replies to the request that asked for them
 * through a small saved word the request handler may leave behind.
 *
 * Offsets in this API are BODY-relative: `body` points just past the request's
 * 4-byte header (or its 8-byte BIG-REQUESTS header), so the field offsets in the
 * X11 protocol document (which count the 4-byte header) are the documented offset
 * minus 4. A normal and a big request therefore look the same to a handler.
 *
 * Untrusted input: the bytes come from a program Ballum does not trust. Every
 * length is checked; a malformed stream marks the framer DESYNCED and it then
 * ignores further input rather than guess. A request longer than the view limit
 * is delivered truncated (`truncated` set, `total_len` the real size) and the
 * rest is skipped without being buffered.
 */
#ifndef SLEELA_BALLUM_XWIRE_H
#define SLEELA_BALLUM_XWIRE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BALLUM_XWIRE_VIEW_MAX (1u << 20)      /* at most this much of one message is shown */

typedef struct ballum_xwire ballum_xwire_t;

typedef struct {
    int            big_endian;
    uint8_t        opcode;       /* major opcode (>= 128 is an extension)            */
    uint8_t        data;         /* second header byte; the MINOR opcode of extensions */
    uint16_t       seq;          /* this request's sequence number (1-based)         */
    const char    *ext;          /* extension name, or NULL for a core request       */
    const uint8_t *body;         /* just past the header                             */
    size_t         body_len;     /* bytes of body shown (<= the real body length)    */
    size_t         total_len;    /* the whole request, header included               */
    int            truncated;    /* 1 when body_len is less than the real body       */
} ballum_xreq_t;

typedef struct {
    int            big_endian;
    uint8_t        opcode;       /* of the request this answers                       */
    uint8_t        data;         /* its second header byte                            */
    const char    *ext;
    uint32_t       saved;        /* what the request handler saved for this request   */
    uint16_t       seq;
    const uint8_t *bytes;        /* the whole reply, 32-byte header first             */
    size_t         len;
    int            truncated;
} ballum_xreply_t;

typedef struct {
    /* Called for every request. Return nonzero to have the reply to it delivered
     * to `reply`, and write a word to *save to get it back there. */
    int  (*request)(void *user, const ballum_xreq_t *req, uint32_t *save);
    void (*reply)(void *user, const ballum_xreply_t *rep);
    /* A 32-byte event (or a generic event with its extra length). */
    void (*event)(void *user, const uint8_t *ev, size_t len, int big_endian);
    void *user;
} ballum_xwire_cb_t;

ballum_xwire_t *ballum_xwire_create(const ballum_xwire_cb_t *cb);
void            ballum_xwire_destroy(ballum_xwire_t *w);

/* Feed bytes in the order they crossed the connection. */
void ballum_xwire_client_feed(ballum_xwire_t *w, const uint8_t *data, size_t n);
void ballum_xwire_server_feed(ballum_xwire_t *w, const uint8_t *data, size_t n);

int      ballum_xwire_desynced(const ballum_xwire_t *w);   /* 1 once the stream could not be followed */
int      ballum_xwire_big_endian(const ballum_xwire_t *w);
uint64_t ballum_xwire_request_count(const ballum_xwire_t *w);

/* ---- the setup request, which a proxy may need to read or rewrite ------------- */

/* Total length of the client's connection-setup message in buf, 0 if more bytes
 * are needed, -1 if it is not a setup message. */
long ballum_xwire_setup_request_size(const uint8_t *buf, size_t n);

/* Build a setup request into out (capacity cap) with the given authorization.
 * Returns its length, or 0 if it does not fit or the arguments are invalid. */
size_t ballum_xwire_build_setup(uint8_t *out, size_t cap, int big_endian,
                                const char *auth_name, const uint8_t *auth_data, size_t auth_len);

#ifdef __cplusplus
}
#endif

#endif /* SLEELA_BALLUM_XWIRE_H */
