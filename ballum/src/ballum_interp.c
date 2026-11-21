/*
 * ballum/src/ballum_interp.c -- the Ballum interpreter (see ballum_interp.h).
 *
 * Invariants this file exists to keep:
 *   1. The mode is chosen at creation and never changes.
 *   2. NOTATION: the output is a byte-for-byte copy of the input frame.
 *   3. ALTERATION: a pixel differs from the input only inside the rectangle of
 *      an element whose alteration passed every filter axis.
 *   4. Everything the producer hands in is untrusted: rectangles are checked
 *      against the frame with 64-bit arithmetic, counts against the buffers.
 */
#include "sleela_ballum/ballum_interp.h"
#include "slcrypto.h"

#include <stdlib.h>
#include <string.h>

#define MAX_ELEMENTS   8192u
#define MAX_TEXT_BYTES 4096u
#define ARENA_BYTES    (256u * 1024u)
#define MAX_PIXELS     ((size_t)1 << 28)
#define AUDIT_CAP      4096u
#define AUDIT_KEY_MAX  64u
#define OPS_CAP        4096u
#define OP_ARENA_BYTES (64u * 1024u)
#define RAW_BYTES_CAP  (8u * 1024u * 1024u)

enum { OP_CONTACT = 0, OP_RAW, OP_TEXT };

/* One applied alteration, kept so the frame can be rebuilt in z order. */
typedef struct {
    int              kind;
    size_t           index;
    ballum_change_t  ch;
    ballum_color_t  *raw;        /* OP_RAW: owned copy of the replacement */
    size_t           raw_n;
    const char      *text;       /* OP_TEXT: points into op_arena          */
    size_t           text_len;
} op_t;

struct ballum_interp {
    ballum_interp_mode_t   mode;
    uint32_t               w, h;
    ballum_filter_band_t   band;

    const ballum_color_t  *src;       /* borrowed for the frame            */
    ballum_color_t        *work;      /* ALTERATION only: src + alterations */
    int                    in_frame;

    ballum_element_t      *elems;
    size_t                 n_elems;
    char                  *arena;     /* text copies for this frame        */
    size_t                 arena_used;

    ballum_interp_stats_t  stats;

    int                    grade;                 /* BALLUM_CRYPTO_*          */
    int                    log_grade;             /* grade of the entries held */
    uint8_t                key[AUDIT_KEY_MAX];
    size_t                 key_len;
    ballum_audit_entry_t  *audit;                 /* AUDIT_CAP, lazily allocated */
    size_t                 n_audit;

    op_t                  *ops;                   /* this frame's alterations, in order */
    size_t                 n_ops;
    size_t                 raw_bytes;
    char                  *op_arena;
    size_t                 op_arena_used;
    const char           **cur_text;              /* per element, as of the last alteration */
    size_t                *cur_len;
    int                   *cur_family;
    ballum_glyph_source_t  glyphs;
    int                    have_glyphs;
};

/* ---- names ----------------------------------------------------------------- */

const char *ballum_interp_mode_name(ballum_interp_mode_t m) {
    return m == BALLUM_INTERP_NOTATION ? "notation"
         : m == BALLUM_INTERP_ALTERATION ? "alteration" : "unknown";
}

const char *ballum_filter_axis_name(ballum_filter_axis_t a) {
    switch (a) {
    case BALLUM_FILTER_MORALS:      return "morals";
    case BALLUM_FILTER_CLEANLINESS: return "cleanliness";
    case BALLUM_FILTER_THROUGHPUT:  return "throughput";
    case BALLUM_FILTER_CONFIDENCE:  return "confidence";
    default:                        return "unknown";
    }
}

const char *ballum_alter_result_name(ballum_alter_result_t r) {
    switch (r) {
    case BALLUM_ALTER_APPLIED:      return "applied";
    case BALLUM_ALTER_MODE_NOTATION:return "mode-notation";
    case BALLUM_ALTER_BLOCKED:      return "blocked";
    case BALLUM_ALTER_OUT_OF_BOUNDS:return "out-of-bounds";
    case BALLUM_ALTER_INVALID:      return "invalid";
    case BALLUM_ALTER_UNSUPPORTED:  return "unsupported";
    case BALLUM_ALTER_AUDIT_FULL:   return "audit-full";
    case BALLUM_ALTER_FRAME_FULL:   return "frame-full";
    }
    return "unknown";
}

/* ---- band ------------------------------------------------------------------ */

void ballum_filter_band_default(ballum_filter_band_t *band) {
    if (!band) return;
    for (int i = 0; i < (int)BALLUM_FILTER__COUNT; ++i) {
        band->axis[i].low = BALLUM_SCORE_MIN;
        band->axis[i].high = BALLUM_SCORE_MAX;
    }
    band->axis[BALLUM_FILTER_CONFIDENCE].low = 500;
}

static int clamp_score(int v) {
    return v < BALLUM_SCORE_MIN ? BALLUM_SCORE_MIN : (v > BALLUM_SCORE_MAX ? BALLUM_SCORE_MAX : v);
}

/* A window is clamped into 0..1000. low > high is kept: it passes nothing,
 * which is the fail-closed reading of a contradictory window. */
static void copy_band(ballum_filter_band_t *dst, const ballum_filter_band_t *src) {
    if (!src) { ballum_filter_band_default(dst); return; }
    for (int i = 0; i < (int)BALLUM_FILTER__COUNT; ++i) {
        dst->axis[i].low = clamp_score(src->axis[i].low);
        dst->axis[i].high = clamp_score(src->axis[i].high);
    }
}

void ballum_interp_set_band(ballum_interp_t *it, const ballum_filter_band_t *band) {
    if (it) copy_band(&it->band, band);
}

void ballum_interp_get_band(const ballum_interp_t *it, ballum_filter_band_t *out) {
    if (it && out) *out = it->band;
}

/* ---- lifecycle -------------------------------------------------------------- */

ballum_interp_t *ballum_interp_create(ballum_interp_mode_t mode, uint32_t w, uint32_t h,
                                      const ballum_filter_band_t *band) {
    if (mode != BALLUM_INTERP_NOTATION && mode != BALLUM_INTERP_ALTERATION) return NULL;
    if (w == 0u || h == 0u || (size_t)w > MAX_PIXELS / (size_t)h) return NULL;
    ballum_interp_t *it = (ballum_interp_t *)calloc(1, sizeof(*it));
    if (!it) return NULL;
    it->mode = mode; it->w = w; it->h = h;
    copy_band(&it->band, band);
    it->elems = (ballum_element_t *)calloc(MAX_ELEMENTS, sizeof(ballum_element_t));
    it->arena = (char *)malloc(ARENA_BYTES);
    it->ops = (op_t *)calloc(OPS_CAP, sizeof(op_t));
    it->op_arena = (char *)malloc(OP_ARENA_BYTES);
    it->cur_text = (const char **)calloc(MAX_ELEMENTS, sizeof(char *));
    it->cur_len = (size_t *)calloc(MAX_ELEMENTS, sizeof(size_t));
    it->cur_family = (int *)calloc(MAX_ELEMENTS, sizeof(int));
    if (mode == BALLUM_INTERP_ALTERATION)
        it->work = (ballum_color_t *)calloc((size_t)w * h, sizeof(ballum_color_t));
    if (!it->elems || !it->arena || !it->ops || !it->op_arena || !it->cur_text || !it->cur_len ||
        !it->cur_family || (mode == BALLUM_INTERP_ALTERATION && !it->work)) {
        ballum_interp_destroy(it);
        return NULL;
    }
    return it;
}

void ballum_interp_destroy(ballum_interp_t *it) {
    if (!it) return;
    if (it->ops) for (size_t i = 0; i < it->n_ops; ++i) free(it->ops[i].raw);
    free(it->ops); free(it->op_arena); free(it->cur_text); free(it->cur_len); free(it->cur_family);
    free(it->elems); free(it->arena); free(it->work); free(it->audit);
    memset(it->key, 0, sizeof(it->key));
    free(it);
}

ballum_interp_mode_t ballum_interp_mode(const ballum_interp_t *it) {
    return it ? it->mode : BALLUM_INTERP_NOTATION;       /* NULL reads as the safe mode */
}

/* ---- a frame ---------------------------------------------------------------- */

ballum_status_t ballum_interp_begin_frame(ballum_interp_t *it, const ballum_color_t *src) {
    if (!it || !src) return BALLUM_STATUS_INVALID_ARGUMENT;
    it->src = src;
    for (size_t i = 0; i < it->n_ops; ++i) { free(it->ops[i].raw); it->ops[i].raw = NULL; }
    it->n_ops = 0; it->raw_bytes = 0; it->op_arena_used = 0;
    it->n_elems = 0;
    it->arena_used = 0;
    it->in_frame = 1;
    if (it->work) memcpy(it->work, src, (size_t)it->w * it->h * sizeof(ballum_color_t));
    it->stats.frames++;
    return BALLUM_STATUS_OK;
}

static int rect_in_frame(const ballum_interp_t *it, ballum_rect_t r) {
    if (r.width == 0u || r.height == 0u) return 0;
    return (uint64_t)r.x + r.width <= it->w && (uint64_t)r.y + r.height <= it->h;
}

ballum_status_t ballum_interp_submit(ballum_interp_t *it, const ballum_element_t *e) {
    if (!it || !e) return BALLUM_STATUS_INVALID_ARGUMENT;
    if (!it->in_frame) return BALLUM_STATUS_NOT_INITIALIZED;
    if ((unsigned)e->kind >= (unsigned)BALLUM_ELEM__COUNT) return BALLUM_STATUS_INVALID_ARGUMENT;
    if (!rect_in_frame(it, e->rect)) return BALLUM_STATUS_INVALID_ARGUMENT;
    if (e->text_len > MAX_TEXT_BYTES || (e->text_len > 0 && !e->text)) return BALLUM_STATUS_INVALID_ARGUMENT;
    if (it->n_elems >= MAX_ELEMENTS) return BALLUM_STATUS_BUFFER_TOO_SMALL;

    ballum_element_t *slot = &it->elems[it->n_elems];
    *slot = *e;
    slot->text = NULL; slot->text_len = 0;
    if (e->kind == BALLUM_ELEM_TEXT && e->text_len > 0 &&
        it->arena_used + e->text_len + 1u <= ARENA_BYTES) {
        memcpy(it->arena + it->arena_used, e->text, e->text_len);
        it->arena[it->arena_used + e->text_len] = '\0';
        slot->text = it->arena + it->arena_used;
        slot->text_len = e->text_len;
        it->arena_used += e->text_len + 1u;
    } else if (e->kind == BALLUM_ELEM_TEXT && e->text_len > 0) {
        slot->glyph_known = 0;                       /* text did not fit: do not claim it */
    }
    it->cur_text[it->n_elems] = slot->text;
    it->cur_len[it->n_elems] = slot->text_len;
    it->cur_family[it->n_elems] = 0;
    it->n_elems++;
    it->stats.elements++;
    return BALLUM_STATUS_OK;
}

size_t ballum_interp_element_count(const ballum_interp_t *it) { return it ? it->n_elems : 0u; }

int ballum_interp_element_at(const ballum_interp_t *it, size_t i, ballum_element_t *out) {
    if (!it || !out || i >= it->n_elems) return 0;
    *out = it->elems[i];
    return 1;
}

/* ---- audit trail ------------------------------------------------------------ */

static void put_be(uint8_t *p, uint64_t v, int bytes) {
    for (int i = bytes - 1; i >= 0; --i) { p[i] = (uint8_t)(v & 0xFFu); v >>= 8; }
}

static int audit_full(const ballum_interp_t *it) {
    return it->grade > BALLUM_CRYPTO_NONE && it->n_audit >= AUDIT_CAP;
}

/* The bytes the chain MAC covers: previous MAC, then the entry's own fields. */
static size_t mac_message(uint8_t msg[108], const uint8_t prev[32], const ballum_audit_entry_t *a) {
    memcpy(msg, prev, 32);
    put_be(msg + 32, a->seq, 8);
    put_be(msg + 40, a->frame, 8);
    put_be(msg + 48, (uint64_t)a->element, 8);
    put_be(msg + 56, (uint32_t)(int32_t)a->point, 4);
    put_be(msg + 60, a->rect.x, 4);
    put_be(msg + 64, a->rect.y, 4);
    put_be(msg + 68, a->rect.width, 4);
    put_be(msg + 72, a->rect.height, 4);
    memcpy(msg + 76, a->digest, 32);
    return 108;
}

/* Called after an alteration was applied. Grade 0 records nothing. */
static void audit_record(ballum_interp_t *it, size_t index, int point) {
    if (it->grade == BALLUM_CRYPTO_NONE || !it->audit || it->n_audit >= AUDIT_CAP) return;
    const ballum_element_t *e = &it->elems[index];
    ballum_audit_entry_t *a = &it->audit[it->n_audit];
    memset(a, 0, sizeof(*a));
    a->seq = (uint64_t)it->n_audit + 1u;
    a->frame = it->stats.frames;
    a->element = index;
    a->point = point;
    a->rect = e->rect;

    slc_sha256_ctx c;
    slc_sha256_init(&c);
    for (uint32_t y = 0; y < e->rect.height; ++y)
        slc_sha256_update(&c, it->work + (size_t)(e->rect.y + y) * it->w + e->rect.x,
                          (size_t)e->rect.width * sizeof(ballum_color_t));
    slc_sha256_final(&c, a->digest);

    if (it->grade == BALLUM_CRYPTO_CHAIN) {
        static const uint8_t zero[32];
        uint8_t msg[108];
        const uint8_t *prev = it->n_audit ? it->audit[it->n_audit - 1u].mac : zero;
        mac_message(msg, prev, a);
        slc_hmac_sha256(it->key, it->key_len, msg, sizeof(msg), a->mac);
    }
    it->log_grade = it->grade;
    it->n_audit++;
}

ballum_status_t ballum_interp_set_audit_key(ballum_interp_t *it, const uint8_t *key, size_t len) {
    if (!it || !key || len == 0u || len > AUDIT_KEY_MAX) return BALLUM_STATUS_INVALID_ARGUMENT;
    memset(it->key, 0, sizeof(it->key));
    memcpy(it->key, key, len);
    it->key_len = len;
    return BALLUM_STATUS_OK;
}

int ballum_interp_crypto_grade(const ballum_interp_t *it) { return it ? it->grade : 0; }
size_t ballum_interp_audit_count(const ballum_interp_t *it) { return it ? it->n_audit : 0u; }

int ballum_interp_audit_at(const ballum_interp_t *it, size_t i, ballum_audit_entry_t *out) {
    if (!it || !out || i >= it->n_audit) return 0;
    *out = it->audit[i];
    return 1;
}

int ballum_interp_audit_head(const ballum_interp_t *it, uint8_t out[32]) {
    if (!it || !out || it->n_audit == 0u) return 0;
    static const char tag[] = "BALLUM-AUDIT-HEAD";
    const ballum_audit_entry_t *last = &it->audit[it->n_audit - 1u];
    uint8_t n[8];
    for (int i = 0; i < 8; ++i) n[i] = (uint8_t)((uint64_t)it->n_audit >> (56 - 8 * i));
    slc_sha256_ctx c;
    slc_sha256_init(&c);
    slc_sha256_update(&c, tag, sizeof(tag) - 1u);
    slc_sha256_update(&c, n, sizeof(n));
    slc_sha256_update(&c, it->log_grade == BALLUM_CRYPTO_CHAIN ? last->mac : last->digest, 32);
    slc_sha256_final(&c, out);
    return 1;
}

int ballum_interp_audit_verify(const ballum_interp_t *it, const uint8_t *key, size_t len) {
    if (!it || !key || len == 0u || len > AUDIT_KEY_MAX || it->n_audit == 0u ||
        it->log_grade != BALLUM_CRYPTO_CHAIN)
        return 0;
    static const uint8_t zero[32];
    for (size_t i = 0; i < it->n_audit; ++i) {
        uint8_t msg[108], mac[32];
        const ballum_audit_entry_t *a = &it->audit[i];
        if (a->seq != (uint64_t)i + 1u) return 0;
        mac_message(msg, i ? it->audit[i - 1u].mac : zero, a);
        slc_hmac_sha256(key, len, msg, sizeof(msg), mac);
        if (!slc_ct_equal(mac, a->mac, 32)) return 0;
    }
    return 1;
}

/* ---- alteration -------------------------------------------------------------- */

/* The band gate. Every axis must be inside its window; a score outside 0..1000
 * is a caller error, not a pass. */
static ballum_alter_result_t gate(const ballum_interp_t *it, const ballum_filter_scores_t *s) {
    for (int i = 0; i < (int)BALLUM_FILTER__COUNT; ++i) {
        int v = s->score[i];
        if (v < BALLUM_SCORE_MIN || v > BALLUM_SCORE_MAX) return BALLUM_ALTER_INVALID;
    }
    for (int i = 0; i < (int)BALLUM_FILTER__COUNT; ++i)
        if (s->score[i] < it->band.axis[i].low || s->score[i] > it->band.axis[i].high)
            return BALLUM_ALTER_BLOCKED;
    return BALLUM_ALTER_APPLIED;
}

/* Common preconditions for any alteration of element `index`. */
static ballum_alter_result_t precheck(ballum_interp_t *it, size_t index,
                                      const ballum_filter_scores_t *scores) {
    if (!it) return BALLUM_ALTER_INVALID;
    if (it->mode == BALLUM_INTERP_NOTATION) return BALLUM_ALTER_MODE_NOTATION;   /* never writes */
    if (!it->in_frame || !it->work || !scores || index >= it->n_elems) return BALLUM_ALTER_INVALID;
    ballum_alter_result_t g = gate(it, scores);
    if (g == BALLUM_ALTER_BLOCKED) it->stats.alterations_blocked++;
    return g;
}

static ballum_color_t over(ballum_color_t fg, ballum_color_t bg) {
    if (fg.a == 255) { fg.a = 255; return fg; }
    int a = fg.a;
    ballum_color_t o;
    o.r = (uint8_t)((fg.r * a + bg.r * (255 - a)) / 255);
    o.g = (uint8_t)((fg.g * a + bg.g * (255 - a)) / 255);
    o.b = (uint8_t)((fg.b * a + bg.b * (255 - a)) / 255);
    o.a = 255;
    return o;
}

static int same_rgb(ballum_color_t a, ballum_color_t b) {
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

static int absdiff(int a, int b) { return a > b ? a - b : b - a; }

ballum_status_t ballum_interp_end_frame(ballum_interp_t *it, ballum_color_t *dst) {
    if (!it || !dst) return BALLUM_STATUS_INVALID_ARGUMENT;
    if (!it->in_frame) return BALLUM_STATUS_NOT_INITIALIZED;
    size_t n = (size_t)it->w * it->h;
    const ballum_color_t *from = it->work ? it->work : it->src;     /* NOTATION: the input itself */
    if (dst == it->src || dst == it->work ||
        (dst < it->src + n && it->src < dst + n) ||
        (it->work && dst < it->work + n && it->work < dst + n))
        return BALLUM_STATUS_INVALID_ARGUMENT;                       /* no aliasing */
    memcpy(dst, from, n * sizeof(ballum_color_t));
    it->in_frame = 0;
    return BALLUM_STATUS_OK;
}

void ballum_interp_stats(const ballum_interp_t *it, ballum_interp_stats_t *out) {
    if (!out) return;
    if (it) *out = it->stats; else memset(out, 0, sizeof(*out));
}

/* ======================================================================== *
 * The 23 contact points
 * ======================================================================== */

static const struct { const char *name; int confidence; } k_contacts[BALLUM_CONTACT__COUNT] = {
    { "color-fg", 1000 },   { "color-bg", 1000 },    { "opacity", 1000 },    { "brightness", 1000 },
    { "contrast", 1000 },   { "saturation", 1000 },  { "visibility", 1000 }, { "effect", 1000 },
    { "blur", 1000 },       { "underline", 1000 },   { "strikethrough", 1000 },{ "weight", 900 },
    { "crypto-grade", 1000 },{ "size", 800 },        { "slant", 800 },       { "text", 600 },
    { "spacing", 700 },     { "line-height", 700 },  { "font-family", 600 }, { "outline", 900 },
    { "position-x", 1000 }, { "position-y", 1000 },  { "z-order", 1000 },
};

const char *ballum_contact_name(ballum_contact_t p) {
    return ((unsigned)p < (unsigned)BALLUM_CONTACT__COUNT) ? k_contacts[p].name : "unknown";
}

int ballum_contact_implemented(ballum_contact_t p) {
    return ((unsigned)p < (unsigned)BALLUM_CONTACT__COUNT) ? 1 : -1;
}

int ballum_contact_confidence(ballum_contact_t p) {
    return ((unsigned)p < (unsigned)BALLUM_CONTACT__COUNT) ? k_contacts[p].confidence : -1;
}

int ballum_contact_available(const ballum_interp_t *it, ballum_contact_t p) {
    if ((unsigned)p >= (unsigned)BALLUM_CONTACT__COUNT) return -1;
    if (p == BALLUM_CONTACT_TEXT || p == BALLUM_CONTACT_FONT_FAMILY) return (it && it->have_glyphs) ? 1 : 0;
    return 1;
}

ballum_filter_scores_t ballum_filter_scores_capped(const ballum_filter_scores_t *base, ballum_contact_t p) {
    ballum_filter_scores_t out;
    memset(&out, 0, sizeof(out));
    if (!base) return out;                                   /* all zero: fails any floor */
    out = *base;
    int cap = ballum_contact_confidence(p);
    if (cap >= 0 && out.score[BALLUM_FILTER_CONFIDENCE] > cap) out.score[BALLUM_FILTER_CONFIDENCE] = cap;
    return out;
}

int ballum_contact_by_name(const char *name, ballum_contact_t *out) {
    if (!name || !out) return 0;
    for (int i = 0; i < (int)BALLUM_CONTACT__COUNT; ++i)
        if (strcmp(name, k_contacts[i].name) == 0) { *out = (ballum_contact_t)i; return 1; }
    return 0;
}

ballum_status_t ballum_interp_set_glyph_source(ballum_interp_t *it, const ballum_glyph_source_t *src) {
    if (!it) return BALLUM_STATUS_INVALID_ARGUMENT;
    if (!src || !src->render) { memset(&it->glyphs, 0, sizeof(it->glyphs)); it->have_glyphs = 0; return BALLUM_STATUS_OK; }
    it->glyphs = *src;
    it->have_glyphs = 1;
    return BALLUM_STATUS_OK;
}

static int clamp255(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

/* Run `fn` over every pixel of the element rectangle. Returns pixels changed. */
typedef ballum_color_t (*pixel_fn)(ballum_color_t p, const ballum_element_t *e, int32_t v);

static uint64_t map_rect(ballum_interp_t *it, const ballum_element_t *e, pixel_fn fn, int32_t v) {
    uint64_t changed = 0;
    for (uint32_t y = 0; y < e->rect.height; ++y) {
        ballum_color_t *row = it->work + (size_t)(e->rect.y + y) * it->w + e->rect.x;
        for (uint32_t x = 0; x < e->rect.width; ++x) {
            ballum_color_t n = fn(row[x], e, v);
            n.a = 255;
            if (!same_rgb(n, row[x])) ++changed;
            row[x] = n;
        }
    }
    return changed;
}

static int gray_of(ballum_color_t p) { return (299 * p.r + 587 * p.g + 114 * p.b) / 1000; }

static ballum_color_t px_opacity(ballum_color_t p, const ballum_element_t *e, int32_t v) {
    ballum_color_t n;
    n.r = (uint8_t)(e->bg.r + (p.r - e->bg.r) * v / 100);
    n.g = (uint8_t)(e->bg.g + (p.g - e->bg.g) * v / 100);
    n.b = (uint8_t)(e->bg.b + (p.b - e->bg.b) * v / 100);
    n.a = 255; return n;
}
static ballum_color_t px_brightness(ballum_color_t p, const ballum_element_t *e, int32_t v) {
    (void)e; int d = (int)v * 255 / 100; ballum_color_t n;
    n.r = (uint8_t)clamp255(p.r + d); n.g = (uint8_t)clamp255(p.g + d); n.b = (uint8_t)clamp255(p.b + d);
    n.a = 255; return n;
}
static ballum_color_t px_contrast(ballum_color_t p, const ballum_element_t *e, int32_t v) {
    (void)e; int f = 100 + (int)v; ballum_color_t n;
    n.r = (uint8_t)clamp255(128 + (p.r - 128) * f / 100);
    n.g = (uint8_t)clamp255(128 + (p.g - 128) * f / 100);
    n.b = (uint8_t)clamp255(128 + (p.b - 128) * f / 100);
    n.a = 255; return n;
}
static ballum_color_t px_saturation(ballum_color_t p, const ballum_element_t *e, int32_t v) {
    (void)e; int g = gray_of(p); ballum_color_t n;
    n.r = (uint8_t)clamp255(g + (p.r - g) * (int)v / 100);
    n.g = (uint8_t)clamp255(g + (p.g - g) * (int)v / 100);
    n.b = (uint8_t)clamp255(g + (p.b - g) * (int)v / 100);
    n.a = 255; return n;
}
static ballum_color_t px_effect(ballum_color_t p, const ballum_element_t *e, int32_t v) {
    (void)e; ballum_color_t n = p;
    if (v == 1) { n.r = (uint8_t)(255 - p.r); n.g = (uint8_t)(255 - p.g); n.b = (uint8_t)(255 - p.b); }
    else if (v == 2) { uint8_t g = (uint8_t)gray_of(p); n.r = n.g = n.b = g; }
    else if (v == 3) {
        n.r = (uint8_t)clamp255((393 * p.r + 769 * p.g + 189 * p.b) / 1000);
        n.g = (uint8_t)clamp255((349 * p.r + 686 * p.g + 168 * p.b) / 1000);
        n.b = (uint8_t)clamp255((272 * p.r + 534 * p.g + 131 * p.b) / 1000);
    }
    n.a = 255; return n;
}

/* Ink coverage of a pixel, 0 (paper) .. 255 (full ink), for an element with a
 * known opaque background and a distinguishable ink colour. */
static int ink_span(const ballum_element_t *e) {
    return absdiff(e->fg.r, e->bg.r) + absdiff(e->fg.g, e->bg.g) + absdiff(e->fg.b, e->bg.b);
}
static int coverage(ballum_color_t p, const ballum_element_t *e, int span) {
    int c = (absdiff(p.r, e->bg.r) + absdiff(p.g, e->bg.g) + absdiff(p.b, e->bg.b)) * 255 / span;
    return c > 255 ? 255 : c;
}
static ballum_color_t mix(ballum_color_t paper, ballum_color_t ink, int cov) {
    ballum_color_t n;
    n.r = (uint8_t)(paper.r + (ink.r - paper.r) * cov / 255);
    n.g = (uint8_t)(paper.g + (ink.g - paper.g) * cov / 255);
    n.b = (uint8_t)(paper.b + (ink.b - paper.b) * cov / 255);
    n.a = 255; return n;
}

static uint64_t op_color_text(ballum_interp_t *it, const ballum_element_t *e, ballum_color_t c, int paper) {
    int span = ink_span(e);
    uint64_t changed = 0;
    for (uint32_t y = 0; y < e->rect.height; ++y) {
        ballum_color_t *row = it->work + (size_t)(e->rect.y + y) * it->w + e->rect.x;
        for (uint32_t x = 0; x < e->rect.width; ++x) {
            int cov = coverage(row[x], e, span);
            ballum_color_t n = paper ? mix(c, e->fg, cov) : mix(e->bg, c, cov);
            if (!same_rgb(n, row[x])) ++changed;
            row[x] = n;
        }
    }
    return changed;
}

static uint64_t op_fill(ballum_interp_t *it, const ballum_element_t *e, ballum_color_t c) {
    uint64_t changed = 0;
    for (uint32_t y = 0; y < e->rect.height; ++y) {
        ballum_color_t *row = it->work + (size_t)(e->rect.y + y) * it->w + e->rect.x;
        for (uint32_t x = 0; x < e->rect.width; ++x) {
            ballum_color_t n = over(c, row[x]);
            if (!same_rgb(n, row[x])) ++changed;
            row[x] = n;
        }
    }
    return changed;
}

/* A horizontal line of `thick` rows starting `top` rows into the element. */
static uint64_t op_line(ballum_interp_t *it, const ballum_element_t *e, uint32_t top, int32_t thick, ballum_color_t c) {
    uint64_t changed = 0;
    for (int32_t t = 0; t < thick; ++t) {
        uint32_t y = top + (uint32_t)t;
        if (y >= e->rect.height) break;
        ballum_color_t *row = it->work + (size_t)(e->rect.y + y) * it->w + e->rect.x;
        for (uint32_t x = 0; x < e->rect.width; ++x) {
            ballum_color_t n = over(c, row[x]);
            if (!same_rgb(n, row[x])) ++changed;
            row[x] = n;
        }
    }
    return changed;
}

static void restore_rect(ballum_interp_t *it, const ballum_element_t *e) {
    for (uint32_t y = 0; y < e->rect.height; ++y) {
        size_t o = (size_t)(e->rect.y + y) * it->w + e->rect.x;
        memcpy(it->work + o, it->src + o, (size_t)e->rect.width * sizeof(ballum_color_t));
    }
}

static uint64_t count_diff(const ballum_interp_t *it, const ballum_element_t *e) {
    uint64_t n = 0;
    for (uint32_t y = 0; y < e->rect.height; ++y) {
        size_t o = (size_t)(e->rect.y + y) * it->w + e->rect.x;
        for (uint32_t x = 0; x < e->rect.width; ++x)
            n += !same_rgb(it->work[o + x], it->src[o + x]);
    }
    return n;
}

/* Separable box blur of radius r, edges clamped to the element rectangle. */
static uint64_t op_blur(ballum_interp_t *it, const ballum_element_t *e, int r, int *ok) {
    size_t w = e->rect.width, h = e->rect.height;
    ballum_color_t *tmp = (ballum_color_t *)malloc(w * h * sizeof(ballum_color_t));
    if (!tmp) { *ok = 0; return 0; }
    uint64_t changed = 0;
    int taps = 2 * r + 1;
    for (size_t y = 0; y < h; ++y) {                       /* horizontal into tmp */
        const ballum_color_t *row = it->work + (size_t)(e->rect.y + y) * it->w + e->rect.x;
        for (size_t x = 0; x < w; ++x) {
            int sr = 0, sg = 0, sb = 0;
            for (int k = -r; k <= r; ++k) {
                long xx = (long)x + k; if (xx < 0) xx = 0; if (xx >= (long)w) xx = (long)w - 1;
                sr += row[xx].r; sg += row[xx].g; sb += row[xx].b;
            }
            tmp[y * w + x].r = (uint8_t)(sr / taps); tmp[y * w + x].g = (uint8_t)(sg / taps);
            tmp[y * w + x].b = (uint8_t)(sb / taps); tmp[y * w + x].a = 255;
        }
    }
    for (size_t y = 0; y < h; ++y) {                       /* vertical back into the frame */
        ballum_color_t *row = it->work + (size_t)(e->rect.y + y) * it->w + e->rect.x;
        for (size_t x = 0; x < w; ++x) {
            int sr = 0, sg = 0, sb = 0;
            for (int k = -r; k <= r; ++k) {
                long yy = (long)y + k; if (yy < 0) yy = 0; if (yy >= (long)h) yy = (long)h - 1;
                const ballum_color_t *t = &tmp[(size_t)yy * w + x];
                sr += t->r; sg += t->g; sb += t->b;
            }
            ballum_color_t n; n.r = (uint8_t)(sr / taps); n.g = (uint8_t)(sg / taps);
            n.b = (uint8_t)(sb / taps); n.a = 255;
            if (!same_rgb(n, row[x])) ++changed;
            row[x] = n;
        }
    }
    free(tmp);
    return changed;
}

/* Grow (n > 0) or shrink (n < 0) the ink by |n| pixels, inside the rectangle. */
static uint64_t op_weight(ballum_interp_t *it, const ballum_element_t *e, int n, int *ok) {
    size_t w = e->rect.width, h = e->rect.height;
    uint8_t *a = (uint8_t *)malloc(w * h * 2u);
    if (!a) { *ok = 0; return 0; }
    uint8_t *b = a + w * h, *orig = NULL;
    int span = ink_span(e);
    for (size_t y = 0; y < h; ++y) {
        const ballum_color_t *row = it->work + (size_t)(e->rect.y + y) * it->w + e->rect.x;
        for (size_t x = 0; x < w; ++x) a[y * w + x] = (uint8_t)coverage(row[x], e, span);
    }
    orig = (uint8_t *)malloc(w * h);
    if (!orig) { free(a); *ok = 0; return 0; }
    memcpy(orig, a, w * h);
    int steps = n < 0 ? -n : n, grow = n > 0;
    for (int s = 0; s < steps; ++s) {
        for (size_t y = 0; y < h; ++y)
            for (size_t x = 0; x < w; ++x) {
                int v = a[y * w + x];
                if (x > 0)     { int q = a[y * w + x - 1]; v = grow ? (q > v ? q : v) : (q < v ? q : v); }
                if (x + 1 < w) { int q = a[y * w + x + 1]; v = grow ? (q > v ? q : v) : (q < v ? q : v); }
                if (y > 0)     { int q = a[(y - 1) * w + x]; v = grow ? (q > v ? q : v) : (q < v ? q : v); }
                if (y + 1 < h) { int q = a[(y + 1) * w + x]; v = grow ? (q > v ? q : v) : (q < v ? q : v); }
                b[y * w + x] = (uint8_t)v;
            }
        memcpy(a, b, w * h);
    }
    uint64_t changed = 0;
    for (size_t y = 0; y < h; ++y) {
        ballum_color_t *row = it->work + (size_t)(e->rect.y + y) * it->w + e->rect.x;
        for (size_t x = 0; x < w; ++x) {
            if (a[y * w + x] == orig[y * w + x]) continue;      /* untouched pixels stay exactly as drawn */
            ballum_color_t nn = mix(e->bg, e->fg, a[y * w + x]);
            if (!same_rgb(nn, row[x])) ++changed;
            row[x] = nn;
        }
    }
    free(a); free(orig);
    return changed;
}

static int has_ink(const ballum_element_t *e) {
    return e->kind == BALLUM_ELEM_TEXT && e->bg.a == 255 && e->fg.a == 255 && ink_span(e) > 0;
}
static int has_paper(const ballum_element_t *e) { return e->bg.a == 255; }

/* ---- pixel operations that move or rebuild the element's own pixels --------- */

static ballum_color_t *snap_rect(const ballum_interp_t *it, const ballum_element_t *e) {
    size_t w = e->rect.width, h = e->rect.height;
    ballum_color_t *t = (ballum_color_t *)malloc(w * h * sizeof(ballum_color_t));
    if (!t) return NULL;
    for (size_t y = 0; y < h; ++y)
        memcpy(t + y * w, it->work + (size_t)(e->rect.y + y) * it->w + e->rect.x, w * sizeof(ballum_color_t));
    return t;
}

/* Write `n` into the element at (x, y) and count it if it differs. */
static void put_px(ballum_interp_t *it, const ballum_element_t *e, uint32_t x, uint32_t y,
                   ballum_color_t n, uint64_t *changed) {
    ballum_color_t *d = it->work + (size_t)(e->rect.y + y) * it->w + e->rect.x + x;
    n.a = 255;
    if (!same_rgb(n, *d)) ++*changed;
    *d = n;
}

static long floordiv(long a, long b) { long q = a / b; return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q; }

/* Bilinear sample of the snapshot at (cx, cy) given in 1/256 pixel; outside = paper. */
static ballum_color_t sample_bilinear(const ballum_color_t *t, long w, long h, long cx, long cy, ballum_color_t paper) {
    long x0 = floordiv(cx, 256), y0 = floordiv(cy, 256);
    long fx = cx - x0 * 256, fy = cy - y0 * 256;
    long rr = 0, gg = 0, bb = 0;
    for (int j = 0; j < 2; ++j)
        for (int i = 0; i < 2; ++i) {
            long x = x0 + i, y = y0 + j;
            long wgt = (i ? fx : 256 - fx) * (j ? fy : 256 - fy);          /* of 65536 */
            ballum_color_t c = (x < 0 || y < 0 || x >= w || y >= h) ? paper : t[y * w + x];
            rr += c.r * wgt; gg += c.g * wgt; bb += c.b * wgt;
        }
    ballum_color_t o;
    o.r = (uint8_t)(rr / 65536); o.g = (uint8_t)(gg / 65536); o.b = (uint8_t)(bb / 65536); o.a = 255;
    return o;
}

static uint64_t op_size(ballum_interp_t *it, const ballum_element_t *e, int pct, int *ok) {
    ballum_color_t *t = snap_rect(it, e);
    if (!t) { *ok = 0; return 0; }
    long w = e->rect.width, h = e->rect.height;
    uint64_t changed = 0;
    for (long y = 0; y < h; ++y)
        for (long x = 0; x < w; ++x) {
            long cx = ((2 * x + 1 - w) * 128 * 100) / pct + w * 128 - 128;
            long cy = ((2 * y + 1 - h) * 128 * 100) / pct + h * 128 - 128;
            put_px(it, e, (uint32_t)x, (uint32_t)y, sample_bilinear(t, w, h, cx, cy, e->bg), &changed);
        }
    free(t);
    return changed;
}

/* Move the content by (dx, dy), with the shear `shear_pct` (x shift grows toward
 * the top, baseline at the bottom row). Exposed area takes the paper colour. */
static uint64_t op_shift_shear(ballum_interp_t *it, const ballum_element_t *e, long dx, long dy,
                               long shear_pct, int *ok) {
    ballum_color_t *t = snap_rect(it, e);
    if (!t) { *ok = 0; return 0; }
    long w = e->rect.width, h = e->rect.height;
    uint64_t changed = 0;
    for (long y = 0; y < h; ++y) {
        long sh = ((h - 1 - y) * shear_pct) / 100;
        for (long x = 0; x < w; ++x) {
            long sx = x - dx - sh, sy = y - dy;
            ballum_color_t c = (sx < 0 || sy < 0 || sx >= w || sy >= h) ? e->bg : t[sy * w + sx];
            put_px(it, e, (uint32_t)x, (uint32_t)y, c, &changed);
        }
    }
    free(t);
    return changed;
}

/* Cut the element at blank columns (horizontal) or rows and re-space the pieces
 * by `extra` pixels. The first piece keeps its place. */
static uint64_t op_respace(ballum_interp_t *it, const ballum_element_t *e, long extra, int horizontal, int *ok) {
    ballum_color_t *t = snap_rect(it, e);
    if (!t) { *ok = 0; return 0; }
    long w = e->rect.width, h = e->rect.height;
    long len = horizontal ? w : h, cross = horizontal ? h : w;
    uint8_t *ink = (uint8_t *)calloc((size_t)len, 1);
    if (!ink) { free(t); *ok = 0; return 0; }
    for (long i = 0; i < len; ++i)
        for (long j = 0; j < cross; ++j) {
            ballum_color_t c = horizontal ? t[j * w + i] : t[i * w + j];
            if (!same_rgb(c, e->bg)) { ink[i] = 1; break; }
        }
    uint64_t changed = 0;
    long pos = -1;                       /* next free position along the axis, -1 until the first piece */
    long i = 0;
    /* paper first */
    for (long a = 0; a < h; ++a) for (long b = 0; b < w; ++b) put_px(it, e, (uint32_t)b, (uint32_t)a, e->bg, &changed);
    long prev_end = 0;
    while (i < len) {
        while (i < len && !ink[i]) ++i;
        if (i >= len) break;
        long s = i;
        while (i < len && ink[i]) ++i;
        long en = i;                                           /* piece is [s, en) */
        long dst;
        if (pos < 0) dst = s;                                  /* first piece stays */
        else { long gap = s - prev_end + extra; if (gap < 0) gap = 0; dst = pos + gap; }
        for (long k = 0; k < en - s; ++k) {
            long d = dst + k;
            if (d < 0 || d >= len) continue;                    /* clipped to the element */
            for (long j = 0; j < cross; ++j) {
                ballum_color_t c = horizontal ? t[j * w + s + k] : t[(s + k) * w + j];
                if (horizontal) put_px(it, e, (uint32_t)d, (uint32_t)j, c, &changed);
                else            put_px(it, e, (uint32_t)j, (uint32_t)d, c, &changed);
            }
        }
        pos = dst + (en - s);
        prev_end = en;
    }
    free(ink); free(t);
    return changed;
}

/* Paint a ring of colour `c`, `n` pixels wide, around the ink. */
static uint64_t op_outline(ballum_interp_t *it, const ballum_element_t *e, int n, ballum_color_t c, int *ok) {
    size_t w = e->rect.width, h = e->rect.height;
    uint8_t *a = (uint8_t *)malloc(w * h * 3u);
    if (!a) { *ok = 0; return 0; }
    uint8_t *b = a + w * h, *orig = b + w * h;
    int span = ink_span(e);
    for (size_t y = 0; y < h; ++y) {
        const ballum_color_t *row = it->work + (size_t)(e->rect.y + y) * it->w + e->rect.x;
        for (size_t x = 0; x < w; ++x) a[y * w + x] = (uint8_t)coverage(row[x], e, span);
    }
    memcpy(orig, a, w * h);
    for (int s = 0; s < n; ++s) {
        for (size_t y = 0; y < h; ++y)
            for (size_t x = 0; x < w; ++x) {
                int v = a[y * w + x];
                if (x > 0     && a[y * w + x - 1] > v) v = a[y * w + x - 1];
                if (x + 1 < w && a[y * w + x + 1] > v) v = a[y * w + x + 1];
                if (y > 0     && a[(y - 1) * w + x] > v) v = a[(y - 1) * w + x];
                if (y + 1 < h && a[(y + 1) * w + x] > v) v = a[(y + 1) * w + x];
                b[y * w + x] = (uint8_t)v;
            }
        memcpy(a, b, w * h);
    }
    uint64_t changed = 0;
    for (size_t y = 0; y < h; ++y)
        for (size_t x = 0; x < w; ++x)
            if (orig[y * w + x] == 0 && a[y * w + x] > 0)
                put_px(it, e, (uint32_t)x, (uint32_t)y, over(c, it->work[(size_t)(e->rect.y + y) * it->w + e->rect.x + x]), &changed);
    free(a);
    return changed;
}

/* ---- text through a glyph source ------------------------------------------- */

static uint32_t next_cp(const unsigned char **p, const unsigned char *end) {
    const unsigned char *s = *p;
    uint32_t c = *s++;
    int extra = 0;
    uint32_t min = 0;
    if (c < 0x80u) { *p = s; return c; }
    else if ((c & 0xE0u) == 0xC0u) { c &= 0x1Fu; extra = 1; min = 0x80u; }
    else if ((c & 0xF0u) == 0xE0u) { c &= 0x0Fu; extra = 2; min = 0x800u; }
    else if ((c & 0xF8u) == 0xF0u) { c &= 0x07u; extra = 3; min = 0x10000u; }
    else { *p = s; return 0xFFFDu; }
    for (int i = 0; i < extra; ++i) {
        if (s >= end || (*s & 0xC0u) != 0x80u) { *p = s; return 0xFFFDu; }
        c = (c << 6) | (*s++ & 0x3Fu);
    }
    *p = s;
    return (c < min || c > 0x10FFFFu || (c >= 0xD800u && c <= 0xDFFFu)) ? 0xFFFDu : c;
}

static uint64_t op_render_text(ballum_interp_t *it, const ballum_element_t *e, const char *text, size_t len,
                               int family, int *ok) {
    uint32_t H = e->rect.height, W = e->rect.width;
    size_t cap = (size_t)H * H * 4u;
    if (cap < 64u) cap = 64u;
    uint8_t *cov = (uint8_t *)malloc(cap);
    if (!cov) { *ok = 0; return 0; }
    uint64_t changed = 0;
    for (uint32_t y = 0; y < H; ++y)
        for (uint32_t x = 0; x < W; ++x) put_px(it, e, x, y, e->bg, &changed);
    long pen = 0;
    const unsigned char *p = (const unsigned char *)text, *end = p + len;
    while (p < end && pen < (long)W) {
        uint32_t cp = next_cp(&p, end);
        uint32_t gw = 0, gh = 0; int32_t adv = 0;
        if (!it->glyphs.render(it->glyphs.user, cp, family, H, cov, cap, &gw, &gh, &adv)) {
            if (!it->glyphs.render(it->glyphs.user, 0xFFFDu, family, H, cov, cap, &gw, &gh, &adv)) continue;
        }
        if (gh > H || gw == 0 || gh == 0 || (size_t)gw * gh > cap || adv < 0 || adv > (int32_t)(4u * H)) continue;
        for (uint32_t gy = 0; gy < gh; ++gy)
            for (uint32_t gx = 0; gx < gw; ++gx) {
                long x = pen + gx;
                if (x >= (long)W) break;
                uint8_t c = cov[(size_t)gy * gw + gx];
                if (c) put_px(it, e, (uint32_t)x, gy, mix(e->bg, e->fg, c), &changed);
            }
        pen += adv;
    }
    free(cov);
    return changed;
}

/* ---- the dispatcher: one change applied to the work frame ------------------- */

/* `text` is used only by TEXT. Validation comes before any pixel is written, so a
 * refused change leaves the frame untouched. Returns APPLIED, INVALID or UNSUPPORTED. */
static ballum_alter_result_t run_change(ballum_interp_t *it, size_t index, const ballum_change_t *ch,
                                        const char *text, size_t text_len, uint64_t *changed_out) {
    ballum_element_t *e = &it->elems[index];
    int32_t v = ch->value;
    uint64_t changed = 0;
    int ok = 1;

    switch (ch->point) {
    case BALLUM_CONTACT_COLOR_FG:
        if (e->kind == BALLUM_ELEM_FILL) { changed = op_fill(it, e, ch->color); break; }
        if (!has_ink(e)) return BALLUM_ALTER_UNSUPPORTED;
        changed = op_color_text(it, e, ch->color, 0);
        break;
    case BALLUM_CONTACT_COLOR_BG:
        if (!has_ink(e)) return BALLUM_ALTER_UNSUPPORTED;
        changed = op_color_text(it, e, ch->color, 1);
        break;
    case BALLUM_CONTACT_OPACITY:
        if (v < 0 || v > 100) return BALLUM_ALTER_INVALID;
        if (!has_paper(e)) return BALLUM_ALTER_UNSUPPORTED;
        changed = map_rect(it, e, px_opacity, v);
        break;
    case BALLUM_CONTACT_BRIGHTNESS:
        if (v < -100 || v > 100) return BALLUM_ALTER_INVALID;
        changed = map_rect(it, e, px_brightness, v);
        break;
    case BALLUM_CONTACT_CONTRAST:
        if (v < -100 || v > 100) return BALLUM_ALTER_INVALID;
        changed = map_rect(it, e, px_contrast, v);
        break;
    case BALLUM_CONTACT_SATURATION:
        if (v < 0 || v > 200) return BALLUM_ALTER_INVALID;
        changed = map_rect(it, e, px_saturation, v);
        break;
    case BALLUM_CONTACT_VISIBILITY:
        if (v != 0 && v != 1) return BALLUM_ALTER_INVALID;
        if (v == 1) { restore_rect(it, e); changed = count_diff(it, e); break; }
        if (!has_paper(e)) return BALLUM_ALTER_UNSUPPORTED;
        changed = op_fill(it, e, e->bg);
        break;
    case BALLUM_CONTACT_EFFECT:
        if (v < 0 || v > 3) return BALLUM_ALTER_INVALID;
        changed = map_rect(it, e, px_effect, v);
        break;
    case BALLUM_CONTACT_BLUR:
        if (v < 1 || v > 8) return BALLUM_ALTER_INVALID;
        changed = op_blur(it, e, (int)v, &ok);
        break;
    case BALLUM_CONTACT_UNDERLINE:
        if (v < 1 || v > 4) return BALLUM_ALTER_INVALID;
        changed = op_line(it, e, e->rect.height > (uint32_t)v ? e->rect.height - (uint32_t)v : 0u, v, ch->color);
        break;
    case BALLUM_CONTACT_STRIKETHROUGH:
        if (v < 1 || v > 4) return BALLUM_ALTER_INVALID;
        changed = op_line(it, e, e->rect.height / 2u, v, ch->color);
        break;
    case BALLUM_CONTACT_WEIGHT:
        if (v < -3 || v > 3) return BALLUM_ALTER_INVALID;
        if (!has_ink(e)) return BALLUM_ALTER_UNSUPPORTED;
        changed = op_weight(it, e, (int)v, &ok);
        break;
    case BALLUM_CONTACT_OUTLINE:
        if (v < 1 || v > 3) return BALLUM_ALTER_INVALID;
        if (!has_ink(e)) return BALLUM_ALTER_UNSUPPORTED;
        changed = op_outline(it, e, (int)v, ch->color, &ok);
        break;
    case BALLUM_CONTACT_POSITION_X:
        if (v < -(int32_t)e->rect.width || v > (int32_t)e->rect.width) return BALLUM_ALTER_INVALID;
        if (!has_paper(e)) return BALLUM_ALTER_UNSUPPORTED;
        changed = op_shift_shear(it, e, v, 0, 0, &ok);
        break;
    case BALLUM_CONTACT_POSITION_Y:
        if (v < -(int32_t)e->rect.height || v > (int32_t)e->rect.height) return BALLUM_ALTER_INVALID;
        if (!has_paper(e)) return BALLUM_ALTER_UNSUPPORTED;
        changed = op_shift_shear(it, e, 0, v, 0, &ok);
        break;
    case BALLUM_CONTACT_SLANT:
        if (v < -100 || v > 100) return BALLUM_ALTER_INVALID;
        if (!has_paper(e)) return BALLUM_ALTER_UNSUPPORTED;
        changed = op_shift_shear(it, e, 0, 0, v, &ok);
        break;
    case BALLUM_CONTACT_SIZE:
        if (v < 25 || v > 400) return BALLUM_ALTER_INVALID;
        if (!has_paper(e)) return BALLUM_ALTER_UNSUPPORTED;
        changed = op_size(it, e, (int)v, &ok);
        break;
    case BALLUM_CONTACT_SPACING:
        if (v < -4 || v > 32) return BALLUM_ALTER_INVALID;
        if (!has_paper(e)) return BALLUM_ALTER_UNSUPPORTED;
        changed = op_respace(it, e, v, 1, &ok);
        break;
    case BALLUM_CONTACT_LINE_HEIGHT:
        if (v < -8 || v > 64) return BALLUM_ALTER_INVALID;
        if (!has_paper(e)) return BALLUM_ALTER_UNSUPPORTED;
        changed = op_respace(it, e, v, 0, &ok);
        break;
    case BALLUM_CONTACT_TEXT:
        if (!it->have_glyphs || !has_ink(e)) return BALLUM_ALTER_UNSUPPORTED;
        if (!text || text_len > MAX_TEXT_BYTES) return BALLUM_ALTER_INVALID;
        changed = op_render_text(it, e, text, text_len, it->cur_family[index], &ok);
        if (ok) { it->cur_text[index] = text; it->cur_len[index] = text_len; }
        break;
    case BALLUM_CONTACT_FONT_FAMILY:
        if (v < 0 || v > 15) return BALLUM_ALTER_INVALID;
        if (!it->have_glyphs || !has_ink(e)) return BALLUM_ALTER_UNSUPPORTED;
        if (!it->cur_text[index] || it->cur_len[index] == 0u) return BALLUM_ALTER_UNSUPPORTED;   /* characters unknown */
        changed = op_render_text(it, e, it->cur_text[index], it->cur_len[index], (int)v, &ok);
        if (ok) it->cur_family[index] = (int)v;
        break;
    case BALLUM_CONTACT_Z_ORDER:
        if (v < 0 || v > 255) return BALLUM_ALTER_INVALID;
        e->z = (uint32_t)v;                                  /* the caller rebuilds in the new order */
        break;
    default:
        return BALLUM_ALTER_UNSUPPORTED;
    }
    if (!ok) return BALLUM_ALTER_INVALID;                    /* an allocation failed: nothing was drawn */
    *changed_out = changed;
    return BALLUM_ALTER_APPLIED;
}

/* ---- the operation log and the z-ordered rebuild ---------------------------- */

typedef struct { uint32_t z; size_t seq; } order_t;

static int cmp_order(const void *a, const void *b) {
    const order_t *x = (const order_t *)a, *y = (const order_t *)b;
    if (x->z != y->z) return x->z < y->z ? -1 : 1;
    return x->seq < y->seq ? -1 : (x->seq > y->seq ? 1 : 0);
}

static void reset_text_state(ballum_interp_t *it) {
    for (size_t i = 0; i < it->n_elems; ++i) {
        it->cur_text[i] = it->elems[i].text;
        it->cur_len[i] = it->elems[i].text_len;
        it->cur_family[i] = 0;
    }
}

static void replay_one(ballum_interp_t *it, const op_t *op) {
    uint64_t ignored = 0;
    if (op->kind == OP_RAW) {
        const ballum_element_t *e = &it->elems[op->index];
        for (uint32_t y = 0; y < e->rect.height; ++y) {
            ballum_color_t *row = it->work + (size_t)(e->rect.y + y) * it->w + e->rect.x;
            for (uint32_t x = 0; x < e->rect.width; ++x) row[x] = over(op->raw[(size_t)y * e->rect.width + x], row[x]);
        }
    } else if (op->ch.point != BALLUM_CONTACT_Z_ORDER) {
        run_change(it, op->index, &op->ch, op->text, op->text_len, &ignored);
    }
}

/* Rebuild the frame from the program frame: the logged alterations applied in
 * order of element z, then of when they were made. Without a z change the order
 * is just the order made, so the result equals what was painted as it went. */
static int rebuild(ballum_interp_t *it) {
    order_t *ord = (order_t *)malloc((it->n_ops ? it->n_ops : 1u) * sizeof(order_t));
    if (!ord) return 0;
    for (size_t i = 0; i < it->n_ops; ++i) { ord[i].z = it->elems[it->ops[i].index].z; ord[i].seq = i; }
    qsort(ord, it->n_ops, sizeof(order_t), cmp_order);
    memcpy(it->work, it->src, (size_t)it->w * it->h * sizeof(ballum_color_t));
    reset_text_state(it);
    for (size_t i = 0; i < it->n_ops; ++i) replay_one(it, &it->ops[ord[i].seq]);
    free(ord);
    return 1;
}

/* Is there room to keep one more alteration? */
static int ops_room(const ballum_interp_t *it, size_t text_len, size_t raw_bytes) {
    return it->n_ops < OPS_CAP && it->op_arena_used + text_len + 1u <= OP_ARENA_BYTES &&
           it->raw_bytes + raw_bytes <= RAW_BYTES_CAP;
}

static op_t *log_op(ballum_interp_t *it, int kind, size_t index, const ballum_change_t *ch) {
    op_t *o = &it->ops[it->n_ops++];
    memset(o, 0, sizeof(*o));
    o->kind = kind; o->index = index;
    if (ch) o->ch = *ch;
    return o;
}

/* ---- public alteration entry points ------------------------------------------ */

static ballum_alter_result_t apply_common(ballum_interp_t *it, size_t index, const ballum_change_t *ch,
                                          const char *text, size_t text_len,
                                          const ballum_filter_scores_t *scores) {
    if (!it || !ch) return BALLUM_ALTER_INVALID;
    if ((unsigned)ch->point >= (unsigned)BALLUM_CONTACT__COUNT) return BALLUM_ALTER_INVALID;

    /* The session-level point: it changes how alterations are recorded. */
    if (ch->point == BALLUM_CONTACT_CRYPTO_GRADE) {
        if (it->mode == BALLUM_INTERP_NOTATION) return BALLUM_ALTER_MODE_NOTATION;
        if (!scores) return BALLUM_ALTER_INVALID;
        ballum_alter_result_t g = gate(it, scores);
        if (g == BALLUM_ALTER_BLOCKED) it->stats.alterations_blocked++;
        if (g != BALLUM_ALTER_APPLIED) return g;
        int v = ch->value;
        if (v < BALLUM_CRYPTO_NONE || v > BALLUM_CRYPTO_CHAIN) return BALLUM_ALTER_INVALID;
        if (v == BALLUM_CRYPTO_CHAIN && it->key_len == 0u) return BALLUM_ALTER_INVALID;
        if (v != BALLUM_CRYPTO_NONE && it->n_audit > 0u && v != it->log_grade) return BALLUM_ALTER_INVALID;
        if (v != BALLUM_CRYPTO_NONE && !it->audit) {
            it->audit = (ballum_audit_entry_t *)calloc(AUDIT_CAP, sizeof(ballum_audit_entry_t));
            if (!it->audit) return BALLUM_ALTER_INVALID;
        }
        it->grade = v;
        it->stats.alterations_applied++;
        return BALLUM_ALTER_APPLIED;
    }

    ballum_alter_result_t g = precheck(it, index, scores);
    if (g != BALLUM_ALTER_APPLIED) return g;
    if (audit_full(it)) return BALLUM_ALTER_AUDIT_FULL;
    if (!ops_room(it, text_len, 0u)) return BALLUM_ALTER_FRAME_FULL;

    /* Keep a TEXT string in the frame arena so the logged operation can replay it. */
    const char *kept = NULL;
    size_t arena_before = it->op_arena_used;
    if (ch->point == BALLUM_CONTACT_TEXT && text) {
        memcpy(it->op_arena + it->op_arena_used, text, text_len);
        it->op_arena[it->op_arena_used + text_len] = '\0';
        kept = it->op_arena + it->op_arena_used;
        it->op_arena_used += text_len + 1u;
    }

    uint64_t changed = 0;
    uint32_t old_z = it->elems[index].z;
    ballum_alter_result_t r = run_change(it, index, ch, kept, text_len, &changed);
    if (r != BALLUM_ALTER_APPLIED) { it->op_arena_used = arena_before; return r; }

    op_t *o = log_op(it, OP_CONTACT, index, ch);
    o->text = kept; o->text_len = text_len;
    if (ch->point == BALLUM_CONTACT_Z_ORDER && !rebuild(it)) {
        it->n_ops--;                                   /* could not rebuild: back out the z change */
        it->elems[index].z = old_z;
        return BALLUM_ALTER_INVALID;
    }
    it->stats.pixels_altered += changed;
    it->stats.alterations_applied++;
    audit_record(it, index, (int)ch->point);
    return BALLUM_ALTER_APPLIED;
}

ballum_alter_result_t ballum_interp_apply(ballum_interp_t *it, size_t index, const ballum_change_t *ch,
                                          const ballum_filter_scores_t *scores) {
    if (ch && ch->point == BALLUM_CONTACT_TEXT) return BALLUM_ALTER_INVALID;   /* TEXT has its own entry point */
    return apply_common(it, index, ch, NULL, 0u, scores);
}

ballum_alter_result_t ballum_interp_apply_text(ballum_interp_t *it, size_t index, const char *text,
                                               size_t text_len, const ballum_filter_scores_t *scores) {
    if (!text || text_len > MAX_TEXT_BYTES) return BALLUM_ALTER_INVALID;
    ballum_change_t ch;
    memset(&ch, 0, sizeof(ch));
    ch.point = BALLUM_CONTACT_TEXT;
    return apply_common(it, index, &ch, text, text_len, scores);
}

ballum_alter_result_t ballum_interp_alter(ballum_interp_t *it, size_t index, const ballum_color_t *replacement,
                                          size_t replacement_count, const ballum_filter_scores_t *scores) {
    ballum_alter_result_t g = precheck(it, index, scores);
    if (g != BALLUM_ALTER_APPLIED) return g;
    const ballum_element_t *e = &it->elems[index];
    if (!replacement || replacement_count != (size_t)e->rect.width * e->rect.height) return BALLUM_ALTER_INVALID;
    if (audit_full(it)) return BALLUM_ALTER_AUDIT_FULL;
    size_t bytes = replacement_count * sizeof(ballum_color_t);
    if (!ops_room(it, 0u, bytes)) return BALLUM_ALTER_FRAME_FULL;
    ballum_color_t *copy = (ballum_color_t *)malloc(bytes);
    if (!copy) return BALLUM_ALTER_INVALID;
    memcpy(copy, replacement, bytes);

    op_t *o = log_op(it, OP_RAW, index, NULL);
    o->raw = copy; o->raw_n = replacement_count;
    it->raw_bytes += bytes;
    for (uint32_t y = 0; y < e->rect.height; ++y) {
        ballum_color_t *row = it->work + (size_t)(e->rect.y + y) * it->w + e->rect.x;
        for (uint32_t x = 0; x < e->rect.width; ++x) {
            ballum_color_t n = over(copy[(size_t)y * e->rect.width + x], row[x]);
            if (!same_rgb(n, row[x])) it->stats.pixels_altered++;
            row[x] = n;
        }
    }
    it->stats.alterations_applied++;
    audit_record(it, index, -1);
    return BALLUM_ALTER_APPLIED;
}

size_t ballum_interp_recolor_text(ballum_interp_t *it, const char *match, ballum_color_t fg,
                                  const ballum_filter_scores_t *scores) {
    if (!it || !match || !scores || it->mode != BALLUM_INTERP_ALTERATION || !it->in_frame) return 0;
    size_t mlen = strlen(match), done = 0;
    for (size_t i = 0; i < it->n_elems; ++i) {
        const ballum_element_t *e = &it->elems[i];
        if (e->kind != BALLUM_ELEM_TEXT || !e->glyph_known || e->text_len != mlen ||
            !e->text || memcmp(e->text, match, mlen) != 0)
            continue;
        ballum_change_t c;
        memset(&c, 0, sizeof(c));
        c.point = BALLUM_CONTACT_COLOR_FG; c.color = fg;
        if (ballum_interp_apply(it, i, &c, scores) == BALLUM_ALTER_APPLIED) ++done;
    }
    return done;
}

/* ---- colours off the frame ------------------------------------------------------- */

int ballum_element_infer_colors(const ballum_color_t *frame, uint32_t fw, uint32_t fh, ballum_element_t *e) {
    if (!frame || !e || fw == 0 || fh == 0) return 0;
    if ((uint64_t)e->rect.x + e->rect.width > fw || (uint64_t)e->rect.y + e->rect.height > fh ||
        e->rect.width == 0 || e->rect.height == 0)
        return 0;
    int set = 0;
    if (e->bg.a != 255) {
        /* the ring just outside the element, or inside it where there is no room */
        int64_t x0 = (int64_t)e->rect.x - 1, y0 = (int64_t)e->rect.y - 1;
        int64_t x1 = (int64_t)e->rect.x + e->rect.width, y1 = (int64_t)e->rect.y + e->rect.height;
        if (x0 < 0 || y0 < 0 || x1 >= (int64_t)fw || y1 >= (int64_t)fh) {
            x0 = e->rect.x; y0 = e->rect.y; x1 = (int64_t)e->rect.x + e->rect.width - 1; y1 = (int64_t)e->rect.y + e->rect.height - 1;
        }
        enum { MAXC = 16 };
        ballum_color_t col[MAXC]; unsigned cnt[MAXC]; int nc = 0; unsigned total = 0;
        int64_t perim = 2 * ((x1 - x0 + 1) + (y1 - y0 + 1));
        int64_t step = perim > 4096 ? perim / 4096 : 1, k = 0;
        for (int64_t y = y0; y <= y1; ++y) {
            for (int64_t x = x0; x <= x1; ++x) {
                if (y != y0 && y != y1 && x != x0 && x != x1) { x = x1 - 1; continue; }      /* only the ring */
                if ((k++ % step) != 0) continue;
                ballum_color_t p = frame[(size_t)y * fw + (size_t)x];
                int i = 0;
                while (i < nc && !same_rgb(col[i], p)) ++i;
                if (i == nc && nc < MAXC) { col[nc] = p; cnt[nc] = 0; ++nc; }
                if (i < nc) cnt[i]++;
                ++total;
            }
        }
        int best = -1; unsigned bc = 0;
        for (int i = 0; i < nc; ++i) if (cnt[i] > bc) { bc = cnt[i]; best = i; }
        if (best >= 0 && total > 0 && bc * 100u >= total * 60u) {
            e->bg = col[best]; e->bg.a = 255; ++set;
        }
    }
    if (e->fg.a != 255 && e->bg.a == 255) {
        int far = 0; ballum_color_t pick = e->bg;
        for (uint32_t y = 0; y < e->rect.height; ++y) {
            const ballum_color_t *row = frame + (size_t)(e->rect.y + y) * fw + e->rect.x;
            for (uint32_t x = 0; x < e->rect.width; ++x) {
                int d = absdiff(row[x].r, e->bg.r) + absdiff(row[x].g, e->bg.g) + absdiff(row[x].b, e->bg.b);
                if (d > far) { far = d; pick = row[x]; }
            }
        }
        if (far > 0) { e->fg = pick; e->fg.a = 255; ++set; }
    }
    return set;
}

/* ---- input follows the picture ----------------------------------------------------- */

int ballum_interp_map_point(const ballum_interp_t *it, int32_t x, int32_t y, int32_t *ox, int32_t *oy) {
    if (ox) *ox = x;
    if (oy) *oy = y;
    if (!it || !ox || !oy || it->n_ops == 0) return 0;

    /* the topmost element that was moved and holds the point: by z, then by its last move */
    long best = -1; uint32_t best_z = 0; size_t best_seq = 0;
    for (size_t i = 0; i < it->n_ops; ++i) {
        const op_t *o = &it->ops[i];
        if (o->kind != OP_CONTACT) continue;
        ballum_contact_t pt = o->ch.point;
        if (pt != BALLUM_CONTACT_POSITION_X && pt != BALLUM_CONTACT_POSITION_Y &&
            pt != BALLUM_CONTACT_SLANT && pt != BALLUM_CONTACT_SIZE)
            continue;
        const ballum_element_t *e = &it->elems[o->index];
        if (x < (int32_t)e->rect.x || y < (int32_t)e->rect.y ||
            (int64_t)x >= (int64_t)e->rect.x + e->rect.width || (int64_t)y >= (int64_t)e->rect.y + e->rect.height)
            continue;
        if (best < 0 || e->z > best_z || (e->z == best_z && i > best_seq)) { best = (long)o->index; best_z = e->z; best_seq = i; }
    }
    if (best < 0) return 0;

    const ballum_element_t *e = &it->elems[best];
    long w = e->rect.width, h = e->rect.height;
    long lx = (long)x - e->rect.x, ly = (long)y - e->rect.y;
    for (size_t i = it->n_ops; i-- > 0; ) {                              /* undo, last move first */
        const op_t *o = &it->ops[i];
        if (o->kind != OP_CONTACT || (long)o->index != best) continue;
        long v = o->ch.value;
        switch (o->ch.point) {
        case BALLUM_CONTACT_POSITION_X: lx -= v; break;
        case BALLUM_CONTACT_POSITION_Y: ly -= v; break;
        case BALLUM_CONTACT_SLANT:      lx -= ((h - 1 - ly) * v) / 100; break;
        case BALLUM_CONTACT_SIZE: {
            /* the forward pass samples source coordinate c (in 1/256 pixel) for a destination pixel */
            long cx = ((2 * lx + 1 - w) * 128 * 100) / v + w * 128 - 128;
            long cy = ((2 * ly + 1 - h) * 128 * 100) / v + h * 128 - 128;
            lx = floordiv(cx + 128, 256);
            ly = floordiv(cy + 128, 256);
            break;
        }
        default: break;
        }
    }
    if (lx < 0) lx = 0;
    if (ly < 0) ly = 0;
    if (lx >= w) lx = w - 1;
    if (ly >= h) ly = h - 1;
    *ox = (int32_t)(e->rect.x + lx);
    *oy = (int32_t)(e->rect.y + ly);
    return 1;
}
