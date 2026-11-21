#include "sleela_ballum/ballum_lms.h"
#include "slcrypto.h"

#include <string.h>

#define N 32
#define P 34                              /* LM-OTS chains for n=32, w=8 */
#define LMOTS_TYPE 4u                     /* LMOTS_SHA256_N32_W8 */
#define LMS_TYPE   5u                     /* LMS_SHA256_M32_H5   */
#define D_PBLC 0x8080
#define D_MESG 0x8181
#define D_LEAF 0x8282
#define D_INTR 0x8383

static void u32(uint8_t *o, uint32_t v) { o[0] = (uint8_t)(v >> 24); o[1] = (uint8_t)(v >> 16); o[2] = (uint8_t)(v >> 8); o[3] = (uint8_t)v; }
static void u16(uint8_t *o, unsigned v) { o[0] = (uint8_t)(v >> 8); o[1] = (uint8_t)v; }

/* One chain step: H(I || q || i || j || tmp) */
static void chain_step(const uint8_t id[16], uint32_t q, unsigned i, unsigned j, uint8_t tmp[N]) {
    uint8_t b[16 + 4 + 2 + 1 + N];
    memcpy(b, id, 16); u32(b + 16, q); u16(b + 20, i); b[22] = (uint8_t)j; memcpy(b + 23, tmp, N);
    slc_sha256(b, sizeof(b), tmp);
}

/* x_i = H(I || q || i || 0xff || SEED) */
static void secret_x(const ballum_lms_key_t *k, uint32_t q, unsigned i, uint8_t x[N]) {
    uint8_t b[16 + 4 + 2 + 1 + N];
    memcpy(b, k->id, 16); u32(b + 16, q); u16(b + 20, i); b[22] = 0xff; memcpy(b + 23, k->seed, N);
    slc_sha256(b, sizeof(b), x);
}

/* K = H(I || q || D_PBLC || y[0] || ... || y[P-1]) from the chain ends. */
static void ots_public(const uint8_t id[16], uint32_t q, const void *yv, uint8_t K[N]) {
    const uint8_t (*y)[N] = (const uint8_t (*)[N])yv;
    slc_sha256_ctx c;
    uint8_t h[22];
    memcpy(h, id, 16); u32(h + 16, q); u16(h + 20, D_PBLC);
    slc_sha256_init(&c);
    slc_sha256_update(&c, h, sizeof(h));
    for (int i = 0; i < P; ++i) slc_sha256_update(&c, y[i], N);
    slc_sha256_final(&c, K);
}

static void ots_leaf_public(const ballum_lms_key_t *k, uint32_t q, uint8_t K[N]) {
    uint8_t y[P][N];
    for (unsigned i = 0; i < P; ++i) {
        secret_x(k, q, i, y[i]);
        for (unsigned j = 0; j < 255; ++j) chain_step(k->id, q, i, j, y[i]);
    }
    ots_public(k->id, q, y, K);
}

/* Q || checksum -> 34 byte coefficients (w=8, ls=0). */
static void coefficients(const uint8_t Q[N], uint8_t a[P]) {
    unsigned sum = 0;
    memcpy(a, Q, N);
    for (int i = 0; i < N; ++i) sum += 255u - Q[i];
    a[N] = (uint8_t)(sum >> 8); a[N + 1] = (uint8_t)sum;
}

static void message_hash(const uint8_t id[16], uint32_t q, const uint8_t C[N],
                         const uint8_t *msg, size_t len, uint8_t Q[N]) {
    slc_sha256_ctx c;
    uint8_t h[22];
    memcpy(h, id, 16); u32(h + 16, q); u16(h + 20, D_MESG);
    slc_sha256_init(&c);
    slc_sha256_update(&c, h, sizeof(h));
    slc_sha256_update(&c, C, N);
    if (len) slc_sha256_update(&c, msg, len);
    slc_sha256_final(&c, Q);
}

static void leaf_node(const uint8_t id[16], uint32_t r, const uint8_t K[N], uint8_t out[N]) {
    uint8_t b[16 + 4 + 2 + N];
    memcpy(b, id, 16); u32(b + 16, r); u16(b + 20, D_LEAF); memcpy(b + 22, K, N);
    slc_sha256(b, sizeof(b), out);
}
static void inner_node(const uint8_t id[16], uint32_t r, const uint8_t l[N], const uint8_t rt[N], uint8_t out[N]) {
    uint8_t b[16 + 4 + 2 + 2 * N];
    memcpy(b, id, 16); u32(b + 16, r); u16(b + 20, D_INTR); memcpy(b + 22, l, N); memcpy(b + 22 + N, rt, N);
    slc_sha256(b, sizeof(b), out);
}

ballum_status_t ballum_lms_keygen(ballum_lms_key_t *key, const uint8_t seed[32], const uint8_t id[16]) {
    if (!key || !seed || !id) return BALLUM_STATUS_INVALID_ARGUMENT;
    memset(key, 0, sizeof(*key));
    memcpy(key->seed, seed, 32); memcpy(key->id, id, 16);
    for (uint32_t q = 0; q < BALLUM_LMS_LEAVES; ++q) {
        ots_leaf_public(key, q, key->leaf[q]);
        leaf_node(key->id, BALLUM_LMS_LEAVES + q, key->leaf[q], key->node[BALLUM_LMS_LEAVES + q]);
    }
    for (uint32_t r = BALLUM_LMS_LEAVES - 1; r >= 1; --r)
        inner_node(key->id, r, key->node[2 * r], key->node[2 * r + 1], key->node[r]);
    u32(key->pub, LMS_TYPE); u32(key->pub + 4, LMOTS_TYPE);
    memcpy(key->pub + 8, key->id, 16); memcpy(key->pub + 24, key->node[1], N);
    return BALLUM_STATUS_OK;
}

ballum_status_t ballum_lms_sign(ballum_lms_key_t *key, const uint8_t *msg, size_t len,
                                uint8_t out[BALLUM_LMS_SIG_BYTES]) {
    if (!key || !out || (!msg && len)) return BALLUM_STATUS_INVALID_ARGUMENT;
    if (key->next >= BALLUM_LMS_LEAVES) return BALLUM_STATUS_PERMISSION_DENIED;   /* exhausted */
    uint32_t q = key->next++;                      /* advance first: a leaf is never reused */

    /* C is deterministic per (seed, leaf, message): safe because a leaf signs once. */
    uint8_t cin[N + 1 + 4 + N], C[N], Q[N], a[P];
    uint8_t mh[N];
    slc_sha256(msg ? msg : (const uint8_t *)"", len, mh);
    memcpy(cin, key->seed, N); cin[N] = 'C'; u32(cin + N + 1, q); memcpy(cin + N + 5, mh, N);
    slc_sha256(cin, sizeof(cin), C);
    message_hash(key->id, q, C, msg, len, Q);
    coefficients(Q, a);

    uint8_t *o = out;
    u32(o, q); o += 4;
    u32(o, LMOTS_TYPE); o += 4;
    memcpy(o, C, N); o += N;
    for (unsigned i = 0; i < P; ++i) {
        uint8_t t[N];
        secret_x(key, q, i, t);
        for (unsigned j = 0; j < a[i]; ++j) chain_step(key->id, q, i, j, t);
        memcpy(o, t, N); o += N;
    }
    u32(o, LMS_TYPE); o += 4;
    for (unsigned h = 0; h < BALLUM_LMS_HEIGHT; ++h) {
        uint32_t r = (BALLUM_LMS_LEAVES + q) >> h;
        memcpy(o, key->node[r ^ 1u], N); o += N;
    }
    memset(cin, 0, sizeof(cin));
    return BALLUM_STATUS_OK;
}

static uint32_t rd32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

int ballum_lms_verify(const uint8_t pub[BALLUM_LMS_PUBKEY_BYTES], const uint8_t *msg, size_t len,
                      const uint8_t *sig, size_t sig_len) {
    if (!pub || !sig || (!msg && len) || sig_len != BALLUM_LMS_SIG_BYTES) return 0;
    if (rd32(pub) != LMS_TYPE || rd32(pub + 4) != LMOTS_TYPE) return 0;
    const uint8_t *id = pub + 8, *root = pub + 24;
    uint32_t q = rd32(sig);
    if (q >= BALLUM_LMS_LEAVES) return 0;
    const uint8_t *p = sig + 4;
    if (rd32(p) != LMOTS_TYPE) return 0;
    p += 4;
    const uint8_t *C = p; p += N;
    uint8_t Q[N], a[P], y[P][N];
    message_hash(id, q, C, msg, len, Q);
    coefficients(Q, a);
    for (unsigned i = 0; i < P; ++i) {
        memcpy(y[i], p, N); p += N;
        for (unsigned j = a[i]; j < 255; ++j) chain_step(id, q, i, j, y[i]);
    }
    if (rd32(p) != LMS_TYPE) return 0;
    p += 4;
    uint8_t K[N], tmp[N];
    ots_public(id, q, y, K);
    uint32_t r = BALLUM_LMS_LEAVES + q;
    leaf_node(id, r, K, tmp);
    for (unsigned h = 0; h < BALLUM_LMS_HEIGHT; ++h) {
        uint8_t nxt[N];
        if (r & 1u) inner_node(id, r >> 1, p, tmp, nxt);
        else        inner_node(id, r >> 1, tmp, p, nxt);
        memcpy(tmp, nxt, N);
        p += N; r >>= 1;
    }
    unsigned diff = 0;                               /* constant-time compare */
    for (unsigned i = 0; i < N; ++i) diff |= (unsigned)(tmp[i] ^ root[i]);
    return diff == 0;
}

void ballum_lms_wipe(ballum_lms_key_t *key) {
    if (!key) return;
    volatile uint8_t *v = (volatile uint8_t *)key;
    for (size_t i = 0; i < sizeof(*key); ++i) v[i] = 0;
}
