/*
 * ballum/src/ballum_xdraw.c -- what the program drew (see ballum_xdraw.h).
 *
 * All offsets below are BODY-relative: the X11 protocol document's offset minus 4.
 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "sleela_ballum/ballum_xdraw.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_WINDOWS     512
#define MAX_PIXMAPS     512
#define MAX_PICTURES    1024
#define MAX_GCS         512
#define MAX_FONTS       16
#define MAX_ALIASES     256
#define GLYPH_SLOTS     16384            /* power of two */
#define GLYPH_BYTES_CAP (16u * 1024u * 1024u)
#define ELEMS_PER_DRAWABLE 512
#define ELEM_TEXT       160
#define MAX_FONT_CHARS  65536u

enum { OP_CREATE_WINDOW = 1, OP_DESTROY_WINDOW = 4, OP_REPARENT = 7, OP_MAP = 8, OP_UNMAP = 10,
       OP_CONFIGURE = 12, OP_OPEN_FONT = 45, OP_CLOSE_FONT = 46, OP_QUERY_FONT = 47,
       OP_CREATE_PIXMAP = 53, OP_FREE_PIXMAP = 54, OP_CREATE_GC = 55, OP_CHANGE_GC = 56, OP_FREE_GC = 60,
       OP_CLEAR_AREA = 61, OP_COPY_AREA = 62, OP_POLY_FILL_RECT = 70, OP_PUT_IMAGE = 72,
       OP_POLY_TEXT8 = 74, OP_POLY_TEXT16 = 75, OP_IMAGE_TEXT8 = 76, OP_IMAGE_TEXT16 = 77 };

enum { R_CREATE_PICTURE = 4, R_FREE_PICTURE = 7, R_COMPOSITE = 8, R_CREATE_GLYPHSET = 17,
       R_REFERENCE_GLYPHSET = 18, R_FREE_GLYPHSET = 19, R_ADD_GLYPHS = 20, R_FREE_GLYPHS = 22,
       R_COMPOSITE_GLYPHS8 = 23, R_COMPOSITE_GLYPHS16 = 24, R_COMPOSITE_GLYPHS32 = 25,
       R_FILL_RECTANGLES = 26, R_CREATE_SOLID_FILL = 33 };

typedef struct { int32_t x, y, w, h; } irect;

typedef struct {
    int            kind;
    irect          r;
    ballum_color_t fg, bg;
    char           text[ELEM_TEXT];
    size_t         tlen;
    int            known;
} xelem_t;

typedef struct { xelem_t *e; size_t n, cap; } elist_t;

typedef struct { int used; uint32_t id, parent; int32_t x, y, w, h; int mapped; uint32_t order; elist_t el; } xwin_t;
typedef struct { int used; uint32_t id; int32_t w, h; elist_t el; } xpix_t;
typedef struct { int used; uint32_t id; int solid; ballum_color_t color; uint32_t drawable; } xpic_t;
typedef struct { int used; uint32_t id; ballum_color_t fg, bg; uint32_t font; } xgc_t;
typedef struct {
    int used; uint32_t id; int have;
    int ascent, descent, max_width;
    uint16_t min_char, max_char, default_char; uint8_t min_b1, max_b1;
    uint16_t *widths; size_t nwidths;
} xfont_t;
typedef struct {
    int used;                            /* 0 free, 1 live, 2 deleted */
    uint32_t gs, gid;
    int w, h, x, y, offx, offy;
    uint8_t *cov;                        /* w*h coverage, NULL for a blank glyph */
    int ident;                           /* 0 not tried, 1 tried */
    uint32_t cp; int known;
} xglyph_t;

struct ballum_xdraw {
    pthread_mutex_t lock;
    const ballum_charsource_t *chars;
    xwin_t   win[MAX_WINDOWS];  uint32_t next_order;
    xpix_t   pix[MAX_PIXMAPS];
    xpic_t   pic[MAX_PICTURES];
    xgc_t    gc[MAX_GCS];
    xfont_t  font[MAX_FONTS];
    struct { int used; uint32_t id, root; } alias[MAX_ALIASES];
    xglyph_t *glyph;                     /* GLYPH_SLOTS */
    size_t   glyph_bytes;
    uint64_t last_draw_ms;
    ballum_xdraw_stats_t st;
};

/* ---- little helpers -------------------------------------------------------------- */

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}
static uint16_t r16(const uint8_t *p, int be) { return be ? (uint16_t)((p[0] << 8) | p[1]) : (uint16_t)((p[1] << 8) | p[0]); }
static int16_t  s16(const uint8_t *p, int be) { return (int16_t)r16(p, be); }
static uint32_t r32(const uint8_t *p, int be) {
    return be ? ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]
              : ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0];
}
static ballum_color_t pixel_rgb(uint32_t p) {
    ballum_color_t c; c.r = (uint8_t)(p >> 16); c.g = (uint8_t)(p >> 8); c.b = (uint8_t)p; c.a = 255; return c;
}
static ballum_color_t render_rgb(const uint8_t *p, int be) {
    ballum_color_t c;
    c.r = (uint8_t)(r16(p, be) >> 8); c.g = (uint8_t)(r16(p + 2, be) >> 8);
    c.b = (uint8_t)(r16(p + 4, be) >> 8); c.a = (uint8_t)(r16(p + 6, be) >> 8);
    return c;
}
static irect mk(int32_t x, int32_t y, int32_t w, int32_t h) { irect r = { x, y, w, h }; return r; }
static int rect_empty(irect r) { return r.w <= 0 || r.h <= 0; }
static irect intersect(irect a, irect b) {
    int64_t x0 = a.x > b.x ? a.x : b.x, y0 = a.y > b.y ? a.y : b.y;
    int64_t x1 = (int64_t)a.x + a.w < (int64_t)b.x + b.w ? (int64_t)a.x + a.w : (int64_t)b.x + b.w;
    int64_t y1 = (int64_t)a.y + a.h < (int64_t)b.y + b.h ? (int64_t)a.y + a.h : (int64_t)b.y + b.h;
    if (x1 <= x0 || y1 <= y0) return mk(0, 0, 0, 0);
    return mk((int32_t)x0, (int32_t)y0, (int32_t)(x1 - x0), (int32_t)(y1 - y0));
}
static int inside(irect inner, irect outer) {
    return inner.x >= outer.x && inner.y >= outer.y &&
           (int64_t)inner.x + inner.w <= (int64_t)outer.x + outer.w &&
           (int64_t)inner.y + inner.h <= (int64_t)outer.y + outer.h;
}

static size_t utf8_put(char *out, size_t cap, uint32_t cp) {
    if (cp >= 0xD800u && cp <= 0xDFFFu) cp = 0xFFFDu;
    if (cp > 0x10FFFFu) cp = 0xFFFDu;
    if (cp < 0x80u) { if (cap < 1) return 0; out[0] = (char)cp; return 1; }
    if (cp < 0x800u) { if (cap < 2) return 0; out[0] = (char)(0xC0u | (cp >> 6)); out[1] = (char)(0x80u | (cp & 0x3Fu)); return 2; }
    if (cp < 0x10000u) { if (cap < 3) return 0; out[0] = (char)(0xE0u | (cp >> 12)); out[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu)); out[2] = (char)(0x80u | (cp & 0x3Fu)); return 3; }
    if (cap < 4) return 0;
    out[0] = (char)(0xF0u | (cp >> 18)); out[1] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
    out[2] = (char)(0x80u | ((cp >> 6) & 0x3Fu)); out[3] = (char)(0x80u | (cp & 0x3Fu));
    return 4;
}

/* ---- tables ------------------------------------------------------------------------ */

static xwin_t *find_win(ballum_xdraw_t *x, uint32_t id) {
    for (int i = 0; i < MAX_WINDOWS; ++i) if (x->win[i].used && x->win[i].id == id) return &x->win[i];
    return NULL;
}
static xpix_t *find_pix(ballum_xdraw_t *x, uint32_t id) {
    for (int i = 0; i < MAX_PIXMAPS; ++i) if (x->pix[i].used && x->pix[i].id == id) return &x->pix[i];
    return NULL;
}
static xpic_t *find_pic(ballum_xdraw_t *x, uint32_t id) {
    for (int i = 0; i < MAX_PICTURES; ++i) if (x->pic[i].used && x->pic[i].id == id) return &x->pic[i];
    return NULL;
}
static xgc_t *find_gc(ballum_xdraw_t *x, uint32_t id) {
    for (int i = 0; i < MAX_GCS; ++i) if (x->gc[i].used && x->gc[i].id == id) return &x->gc[i];
    return NULL;
}
static xfont_t *find_font(ballum_xdraw_t *x, uint32_t id) {
    for (int i = 0; i < MAX_FONTS; ++i) if (x->font[i].used && x->font[i].id == id) return &x->font[i];
    return NULL;
}
static void *slot(void *base, size_t stride, int n, int *free_out) {
    char *b = (char *)base;
    for (int i = 0; i < n; ++i) if (!*(int *)(b + (size_t)i * stride)) { *free_out = i; return b + (size_t)i * stride; }
    return NULL;
}

static elist_t *drawable_list(ballum_xdraw_t *x, uint32_t id) {
    xwin_t *w = find_win(x, id);
    if (w) return &w->el;
    xpix_t *p = find_pix(x, id);
    return p ? &p->el : NULL;
}

static void elist_free(elist_t *l) { free(l->e); memset(l, 0, sizeof(*l)); }

static void elist_push(ballum_xdraw_t *x, elist_t *l, const xelem_t *e) {
    if (!l) return;
    if (l->n >= ELEMS_PER_DRAWABLE) {                           /* drop the oldest */
        memmove(l->e, l->e + 1, (l->n - 1) * sizeof(xelem_t));
        l->n--; x->st.dropped++;
    }
    if (l->n == l->cap) {
        size_t nc = l->cap ? l->cap * 2u : 16u;
        if (nc > ELEMS_PER_DRAWABLE) nc = ELEMS_PER_DRAWABLE;
        xelem_t *ne = (xelem_t *)realloc(l->e, nc * sizeof(xelem_t));
        if (!ne) { x->st.dropped++; return; }
        l->e = ne; l->cap = nc;
    }
    l->e[l->n++] = *e;
    x->st.elements++;
}

/* The new drawing is opaque over r: elements wholly inside it are gone. */
static void obscure(elist_t *l, irect r) {
    if (!l) return;
    size_t k = 0;
    for (size_t i = 0; i < l->n; ++i) if (!inside(l->e[i].r, r)) l->e[k++] = l->e[i];
    l->n = k;
}

static void add_elem(ballum_xdraw_t *x, uint32_t drawable, int kind, irect r, ballum_color_t fg, ballum_color_t bg,
                     const char *text, size_t tlen, int known, int opaque) {
    elist_t *l = drawable_list(x, drawable);
    if (!l || rect_empty(r)) return;
    if (opaque) obscure(l, r);
    xelem_t e;
    memset(&e, 0, sizeof(e));
    e.kind = kind; e.r = r; e.fg = fg; e.bg = bg; e.known = known;
    if (text && tlen) {
        if (tlen >= ELEM_TEXT) { tlen = ELEM_TEXT - 1u; e.known = 0; }
        memcpy(e.text, text, tlen); e.tlen = tlen;
    }
    elist_push(x, l, &e);
}

/* Absolute origin of a window in root coordinates, and whether it is on screen. */
static int win_origin(ballum_xdraw_t *x, const xwin_t *w, int32_t *ox, int32_t *oy) {
    int64_t sx = 0, sy = 0;
    const xwin_t *c = w;
    for (int depth = 0; c && depth < 32; ++depth) {
        if (!c->mapped) return 0;
        sx += c->x; sy += c->y;
        if (c->parent == 0) break;
        const xwin_t *p = find_win(x, c->parent);
        if (!p) break;                                           /* a root window: origin (0,0) */
        c = p;
    }
    if (sx < -100000 || sx > 100000 || sy < -100000 || sy > 100000) return 0;
    *ox = (int32_t)sx; *oy = (int32_t)sy;
    return 1;
}

/* ---- create / destroy ------------------------------------------------------------------ */

ballum_xdraw_t *ballum_xdraw_create(void) {
    ballum_xdraw_t *x = (ballum_xdraw_t *)calloc(1, sizeof(*x));
    if (!x) return NULL;
    x->glyph = (xglyph_t *)calloc(GLYPH_SLOTS, sizeof(xglyph_t));
    if (!x->glyph || pthread_mutex_init(&x->lock, NULL) != 0) { free(x->glyph); free(x); return NULL; }
    return x;
}

void ballum_xdraw_destroy(ballum_xdraw_t *x) {
    if (!x) return;
    for (int i = 0; i < MAX_WINDOWS; ++i) elist_free(&x->win[i].el);
    for (int i = 0; i < MAX_PIXMAPS; ++i) elist_free(&x->pix[i].el);
    for (int i = 0; i < MAX_FONTS; ++i) free(x->font[i].widths);
    for (int i = 0; i < GLYPH_SLOTS; ++i) free(x->glyph[i].cov);
    free(x->glyph);
    pthread_mutex_destroy(&x->lock);
    free(x);
}

void ballum_xdraw_set_charsource(ballum_xdraw_t *x, const ballum_charsource_t *cs) {
    if (!x) return;
    pthread_mutex_lock(&x->lock);
    x->chars = cs;
    for (int i = 0; i < GLYPH_SLOTS; ++i) x->glyph[i].ident = 0;      /* identify again with the new source */
    pthread_mutex_unlock(&x->lock);
}

void ballum_xdraw_stats(ballum_xdraw_t *x, ballum_xdraw_stats_t *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!x) return;
    pthread_mutex_lock(&x->lock);
    *out = x->st;
    pthread_mutex_unlock(&x->lock);
}

/* ---- glyph store ------------------------------------------------------------------------- */

static uint32_t glyph_root(ballum_xdraw_t *x, uint32_t gs) {
    for (int i = 0; i < MAX_ALIASES; ++i) if (x->alias[i].used && x->alias[i].id == gs) return x->alias[i].root;
    return gs;
}
static size_t glyph_hash(uint32_t gs, uint32_t gid) {
    uint32_t h = gs * 2654435761u ^ (gid + 0x9E3779B9u) * 2246822519u;
    h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
    return h & (GLYPH_SLOTS - 1u);
}
static xglyph_t *glyph_find(ballum_xdraw_t *x, uint32_t gs, uint32_t gid) {
    size_t i = glyph_hash(gs, gid);
    for (size_t n = 0; n < GLYPH_SLOTS; ++n, i = (i + 1u) & (GLYPH_SLOTS - 1u)) {
        xglyph_t *g = &x->glyph[i];
        if (g->used == 0) return NULL;
        if (g->used == 1 && g->gs == gs && g->gid == gid) return g;
    }
    return NULL;
}
static void glyph_release(ballum_xdraw_t *x, xglyph_t *g) {
    if (g->cov) { x->glyph_bytes -= (size_t)g->w * g->h; free(g->cov); }
    memset(g, 0, sizeof(*g));
    g->used = 2;                                                 /* a tombstone keeps probe chains intact */
}
static xglyph_t *glyph_slot(ballum_xdraw_t *x, uint32_t gs, uint32_t gid) {
    xglyph_t *old = glyph_find(x, gs, gid);
    if (old) { glyph_release(x, old); old->used = 1; old->gs = gs; old->gid = gid; return old; }
    size_t i = glyph_hash(gs, gid);
    for (size_t n = 0; n < GLYPH_SLOTS; ++n, i = (i + 1u) & (GLYPH_SLOTS - 1u)) {
        xglyph_t *g = &x->glyph[i];
        if (g->used != 1) { memset(g, 0, sizeof(*g)); g->used = 1; g->gs = gs; g->gid = gid; return g; }
    }
    return NULL;
}

static void free_glyphs_of(ballum_xdraw_t *x, uint32_t gs) {
    for (int i = 0; i < GLYPH_SLOTS; ++i)
        if (x->glyph[i].used == 1 && x->glyph[i].gs == gs) glyph_release(x, &x->glyph[i]);
}

/* ---- window requests -------------------------------------------------------------------- */

static void do_create_window(ballum_xdraw_t *x, const ballum_xreq_t *r) {
    if (r->body_len < 20) return;
    int be = r->big_endian, fi;
    uint32_t id = r32(r->body, be);
    if (find_win(x, id)) return;
    xwin_t *w = (xwin_t *)slot(x->win, sizeof(xwin_t), MAX_WINDOWS, &fi);
    if (!w) { x->st.dropped++; return; }
    memset(w, 0, sizeof(*w));
    w->used = 1; w->id = id; w->parent = r32(r->body + 4, be);
    w->x = s16(r->body + 8, be); w->y = s16(r->body + 10, be);
    w->w = r16(r->body + 12, be); w->h = r16(r->body + 14, be);
    w->order = ++x->next_order;
}

static void do_destroy_window(ballum_xdraw_t *x, uint32_t id) {
    xwin_t *w = find_win(x, id);
    if (!w) return;
    elist_free(&w->el);
    memset(w, 0, sizeof(*w));
}

/* ---- fonts ------------------------------------------------------------------------------- */

static void do_open_font(ballum_xdraw_t *x, const ballum_xreq_t *r) {
    if (r->body_len < 4) return;
    uint32_t id = r32(r->body, r->big_endian);
    if (find_font(x, id)) return;
    int fi;
    xfont_t *f = (xfont_t *)slot(x->font, sizeof(xfont_t), MAX_FONTS, &fi);
    if (!f) { x->st.dropped++; return; }
    memset(f, 0, sizeof(*f));
    f->used = 1; f->id = id;
}

static void do_close_font(ballum_xdraw_t *x, uint32_t id) {
    xfont_t *f = find_font(x, id);
    if (!f) return;
    free(f->widths);
    memset(f, 0, sizeof(*f));
}

/* QueryFont reply: ascent, descent and every character's width. */
static void query_font_reply(ballum_xdraw_t *x, const ballum_xreply_t *rep) {
    xfont_t *f = find_font(x, rep->saved);
    if (!f || rep->truncated || rep->len < 60) return;
    int be = rep->big_endian;
    const uint8_t *m = rep->bytes;
    uint32_t nchars = r32(m + 56, be);
    uint16_t nprops = r16(m + 46, be);
    size_t chars_at = 60u + (size_t)nprops * 8u;
    if (nchars > MAX_FONT_CHARS || chars_at + (size_t)nchars * 12u > rep->len) return;
    uint16_t *wd = (uint16_t *)malloc((nchars ? nchars : 1u) * sizeof(uint16_t));
    if (!wd) return;
    for (uint32_t i = 0; i < nchars; ++i) wd[i] = (uint16_t)s16(m + chars_at + (size_t)i * 12u + 4u, be);
    free(f->widths);
    f->widths = wd; f->nwidths = nchars;
    f->min_char = r16(m + 40, be); f->max_char = r16(m + 42, be); f->default_char = r16(m + 44, be);
    f->min_b1 = m[49]; f->max_b1 = m[50];
    f->ascent = s16(m + 52, be); f->descent = s16(m + 54, be);
    f->max_width = s16(m + 28, be);                               /* max-bounds character-width */
    f->have = 1;
}

/* Width of a code in a core font, or -1 if the font is not known. */
static int font_width(const xfont_t *f, uint32_t code16, int two_byte) {
    if (!f || !f->have) return -1;
    uint32_t b1 = two_byte ? (code16 >> 8) & 0xFFu : 0u, b2 = code16 & 0xFFu;
    if (b1 < f->min_b1 || b1 > f->max_b1 || b2 < f->min_char || b2 > f->max_char) return f->max_width;
    size_t span = (size_t)f->max_char - f->min_char + 1u;
    size_t idx = (size_t)(b1 - f->min_b1) * span + (b2 - f->min_char);
    return idx < f->nwidths ? (int)(int16_t)f->widths[idx] : f->max_width;
}

/* ---- graphics contexts ----------------------------------------------------------------------- */

static void gc_apply(xgc_t *g, uint32_t mask, const uint8_t *v, size_t len, int be) {
    size_t off = 0;
    for (int bit = 0; bit < 23; ++bit) {
        if (!(mask & (1u << bit))) continue;
        if (off + 4 > len) return;
        uint32_t val = r32(v + off, be);
        off += 4;
        if (bit == 2) g->fg = pixel_rgb(val);
        else if (bit == 3) g->bg = pixel_rgb(val);
        else if (bit == 14) g->font = val;
    }
}

static void do_create_gc(ballum_xdraw_t *x, const ballum_xreq_t *r) {
    if (r->body_len < 12) return;
    int be = r->big_endian, fi;
    uint32_t id = r32(r->body, be);
    xgc_t *g = find_gc(x, id);
    if (!g) {
        g = (xgc_t *)slot(x->gc, sizeof(xgc_t), MAX_GCS, &fi);
        if (!g) { x->st.dropped++; return; }
        memset(g, 0, sizeof(*g));
        g->used = 1; g->id = id;
    }
    g->fg = pixel_rgb(0); g->bg = pixel_rgb(0xFFFFFFu); g->font = 0;
    gc_apply(g, r32(r->body + 8, be), r->body + 12, r->body_len - 12, be);
}

static void do_change_gc(ballum_xdraw_t *x, const ballum_xreq_t *r) {
    if (r->body_len < 8) return;
    xgc_t *g = find_gc(x, r32(r->body, r->big_endian));
    if (g) gc_apply(g, r32(r->body + 4, r->big_endian), r->body + 8, r->body_len - 8, r->big_endian);
}

/* ---- core drawing ---------------------------------------------------------------------------- */

static void text_run(ballum_xdraw_t *x, uint32_t drawable, const xgc_t *gc, int32_t px, int32_t py,
                     const uint8_t *s, size_t n, int two_byte, int be, int paint_bg) {
    (void)be;
    const xfont_t *f = find_font(x, gc ? gc->font : 0u);
    if (!f || !f->have) { x->st.text_unsized++; return; }
    char text[ELEM_TEXT];
    size_t tl = 0; int known = 1; int64_t width = 0;
    for (size_t i = 0; i < n; ++i) {
        uint32_t code = two_byte ? ((uint32_t)s[2 * i] << 8) | s[2 * i + 1] : s[i];
        int wd = font_width(f, code, two_byte);
        if (wd < 0) return;
        width += wd;
        size_t put = utf8_put(text + tl, sizeof(text) - 1 - tl, code);
        if (put == 0) known = 0; else tl += put;
    }
    irect r = mk(px, py - f->ascent, (int32_t)width, f->ascent + f->descent);
    add_elem(x, drawable, BALLUM_ELEM_TEXT, r, gc->fg, paint_bg ? gc->bg : (ballum_color_t){ 0, 0, 0, 0 },
             text, tl, known, paint_bg);
}

static void do_image_text(ballum_xdraw_t *x, const ballum_xreq_t *r, int two_byte) {
    if (r->body_len < 12) return;
    int be = r->big_endian;
    size_t n = r->data, bytes = two_byte ? n * 2u : n;
    if (r->body_len < 12 + bytes) return;
    xgc_t *gc = find_gc(x, r32(r->body + 4, be));
    if (!gc) return;
    text_run(x, r32(r->body, be), gc, s16(r->body + 8, be), s16(r->body + 10, be), r->body + 12, n, two_byte, be, 1);
}

static void do_poly_text(ballum_xdraw_t *x, const ballum_xreq_t *r, int two_byte) {
    if (r->body_len < 12) return;
    int be = r->big_endian;
    uint32_t drawable = r32(r->body, be);
    xgc_t *gc0 = find_gc(x, r32(r->body + 4, be));
    if (!gc0) return;
    xgc_t gc = *gc0;                                            /* font switches stay local to this request */
    int32_t px = s16(r->body + 8, be), py = s16(r->body + 10, be);
    size_t p = 12;
    while (p + 2 <= r->body_len) {
        uint8_t n = r->body[p];
        if (n == 0) break;
        if (n == 255) {                                         /* font change: 4 bytes, most significant first */
            if (p + 5 > r->body_len) break;
            gc.font = ((uint32_t)r->body[p + 1] << 24) | ((uint32_t)r->body[p + 2] << 16) |
                      ((uint32_t)r->body[p + 3] << 8) | r->body[p + 4];
            p += 5;
            continue;
        }
        int8_t delta = (int8_t)r->body[p + 1];
        size_t bytes = two_byte ? (size_t)n * 2u : (size_t)n;
        if (p + 2 + bytes > r->body_len) break;
        px += delta;
        const xfont_t *f = find_font(x, gc.font);
        text_run(x, drawable, &gc, px, py, r->body + p + 2, n, two_byte, be, 0);
        if (f && f->have) {
            for (size_t i = 0; i < n; ++i) {
                uint32_t code = two_byte ? ((uint32_t)r->body[p + 2 + 2 * i] << 8) | r->body[p + 3 + 2 * i] : r->body[p + 2 + i];
                int wd = font_width(f, code, two_byte);
                if (wd > 0) px += wd;
            }
        }
        p += 2 + bytes;
    }
}

static void do_fill_rects(ballum_xdraw_t *x, const ballum_xreq_t *r) {
    if (r->body_len < 8) return;
    int be = r->big_endian;
    xgc_t *gc = find_gc(x, r32(r->body + 4, be));
    if (!gc) return;
    uint32_t d = r32(r->body, be);
    for (size_t p = 8, k = 0; p + 8 <= r->body_len && k < 64; p += 8, ++k)
        add_elem(x, d, BALLUM_ELEM_FILL, mk(s16(r->body + p, be), s16(r->body + p + 2, be), r16(r->body + p + 4, be), r16(r->body + p + 6, be)),
                 gc->fg, gc->fg, NULL, 0, 0, 1);
}

static void do_put_image(ballum_xdraw_t *x, const ballum_xreq_t *r) {
    if (r->body_len < 16) return;
    int be = r->big_endian;
    ballum_color_t none = { 0, 0, 0, 0 };
    add_elem(x, r32(r->body, be), BALLUM_ELEM_IMAGE,
             mk(s16(r->body + 12, be), s16(r->body + 14, be), r16(r->body + 8, be), r16(r->body + 10, be)),
             none, none, NULL, 0, 0, 1);
}

static void do_clear_area(ballum_xdraw_t *x, const ballum_xreq_t *r) {
    if (r->body_len < 12) return;
    int be = r->big_endian;
    elist_t *l = drawable_list(x, r32(r->body, be));
    xwin_t *w = find_win(x, r32(r->body, be));
    if (!l || !w) return;
    int32_t cw = r16(r->body + 8, be), ch = r16(r->body + 10, be);
    irect a = mk(s16(r->body + 4, be), s16(r->body + 6, be), cw ? cw : w->w - s16(r->body + 4, be), ch ? ch : w->h - s16(r->body + 6, be));
    obscure(l, a);
}

/* Copy the elements of src inside sr to dst at (dx, dy); the destination region is
 * opaque. A source without recorded elements becomes one image element. */
static void copy_region(ballum_xdraw_t *x, uint32_t src, uint32_t dst, irect sr, int32_t dx, int32_t dy) {
    elist_t *sl = drawable_list(x, src), *dl = drawable_list(x, dst);
    if (!dl || rect_empty(sr)) return;
    xelem_t tmp[ELEMS_PER_DRAWABLE];
    size_t nt = 0;
    if (sl) {
        for (size_t i = 0; i < sl->n && nt < ELEMS_PER_DRAWABLE; ++i) {
            irect c = intersect(sl->e[i].r, sr);
            if (rect_empty(c)) continue;
            tmp[nt] = sl->e[i];
            if (c.x != sl->e[i].r.x || c.y != sl->e[i].r.y || c.w != sl->e[i].r.w || c.h != sl->e[i].r.h) tmp[nt].known = 0;   /* clipped: may be partial */
            tmp[nt].r = mk(c.x - sr.x + dx, c.y - sr.y + dy, c.w, c.h);
            ++nt;
        }
    }
    irect dr = mk(dx, dy, sr.w, sr.h);
    obscure(dl, dr);
    ballum_color_t none = { 0, 0, 0, 0 };
    add_elem(x, dst, BALLUM_ELEM_IMAGE, dr, none, none, NULL, 0, 0, 0);
    for (size_t i = 0; i < nt; ++i) elist_push(x, dl, &tmp[i]);
}

static void do_copy_area(ballum_xdraw_t *x, const ballum_xreq_t *r) {
    if (r->body_len < 24) return;
    int be = r->big_endian;
    irect sr = mk(s16(r->body + 12, be), s16(r->body + 14, be), r16(r->body + 20, be), r16(r->body + 22, be));
    copy_region(x, r32(r->body, be), r32(r->body + 4, be), sr, s16(r->body + 16, be), s16(r->body + 18, be));
}

/* ---- RENDER ------------------------------------------------------------------------------------ */

static void r_create_picture(ballum_xdraw_t *x, const ballum_xreq_t *r) {
    if (r->body_len < 16) return;
    int fi;
    uint32_t id = r32(r->body, r->big_endian);
    xpic_t *p = find_pic(x, id);
    if (!p) { p = (xpic_t *)slot(x->pic, sizeof(xpic_t), MAX_PICTURES, &fi); if (!p) { x->st.dropped++; return; } }
    memset(p, 0, sizeof(*p));
    p->used = 1; p->id = id; p->drawable = r32(r->body + 4, r->big_endian);
}

static void r_create_solid(ballum_xdraw_t *x, const ballum_xreq_t *r) {
    if (r->body_len < 12) return;
    int fi;
    uint32_t id = r32(r->body, r->big_endian);
    xpic_t *p = find_pic(x, id);
    if (!p) { p = (xpic_t *)slot(x->pic, sizeof(xpic_t), MAX_PICTURES, &fi); if (!p) { x->st.dropped++; return; } }
    memset(p, 0, sizeof(*p));
    p->used = 1; p->id = id; p->solid = 1; p->color = render_rgb(r->body + 4, r->big_endian);
}

static void r_glyphset(ballum_xdraw_t *x, const ballum_xreq_t *r, int reference) {
    if (r->body_len < 8) return;
    int be = r->big_endian;
    uint32_t id = r32(r->body, be);
    if (!reference) return;                                    /* a new set needs no state */
    uint32_t root = glyph_root(x, r32(r->body + 4, be));
    for (int i = 0; i < MAX_ALIASES; ++i)
        if (!x->alias[i].used) { x->alias[i].used = 1; x->alias[i].id = id; x->alias[i].root = root; return; }
    x->st.dropped++;
}

static void r_add_glyphs(ballum_xdraw_t *x, const ballum_xreq_t *r) {
    if (r->body_len < 8 || r->truncated) return;               /* a truncated upload is lost, not guessed at */
    int be = r->big_endian;
    uint32_t gs = glyph_root(x, r32(r->body, be));
    uint32_t n = r32(r->body + 4, be);
    if (n == 0 || n > 4096u) return;
    size_t ids_at = 8, infos_at = ids_at + (size_t)n * 4u, data_at = infos_at + (size_t)n * 12u;
    if (data_at > r->body_len) return;
    size_t remaining = r->body_len - data_at;

    /* The glyph set's pixel format is not tracked; it is the one whose total size
     * matches the data that follows. */
    size_t tot_a8 = 0, tot_a1 = 0, tot_argb = 0;
    for (uint32_t i = 0; i < n; ++i) {
        const uint8_t *gi = r->body + infos_at + (size_t)i * 12u;
        size_t w = r16(gi, be), h = r16(gi + 2, be);
        tot_a8 += ((w + 3u) & ~3u) * h;
        tot_a1 += (((w + 7u) / 8u + 3u) & ~3u) * h;
        tot_argb += w * 4u * h;
    }
    int fmt = (tot_a8 == remaining) ? 1 : (tot_argb == remaining ? 2 : (tot_a1 == remaining ? 3 : 0));

    size_t off = data_at;
    for (uint32_t i = 0; i < n; ++i) {
        const uint8_t *gi = r->body + infos_at + (size_t)i * 12u;
        int w = r16(gi, be), h = r16(gi + 2, be);
        xglyph_t *g = glyph_slot(x, gs, r32(r->body + ids_at + (size_t)i * 4u, be));
        size_t stride = fmt == 1 ? (((size_t)w + 3u) & ~3u) : fmt == 2 ? (size_t)w * 4u : (((size_t)w + 7u) / 8u + 3u) & ~3u;
        size_t bytes = stride * (size_t)h;
        if (!g) { x->st.dropped++; off += fmt ? bytes : 0; continue; }
        g->w = w; g->h = h; g->x = s16(gi + 4, be); g->y = s16(gi + 6, be);
        g->offx = s16(gi + 8, be); g->offy = s16(gi + 10, be);
        if (fmt && w > 0 && h > 0 && (size_t)w * h <= 65536u && off + bytes <= r->body_len &&
            x->glyph_bytes + (size_t)w * h <= GLYPH_BYTES_CAP) {
            g->cov = (uint8_t *)malloc((size_t)w * h);
            if (g->cov) {
                x->glyph_bytes += (size_t)w * h;
                const uint8_t *d = r->body + off;
                for (int yy = 0; yy < h; ++yy)
                    for (int xx = 0; xx < w; ++xx) {
                        uint8_t c;
                        if (fmt == 1) c = d[(size_t)yy * stride + xx];
                        else if (fmt == 2) c = d[(size_t)yy * stride + (size_t)xx * 4u + (be ? 0u : 3u)];
                        else c = ((d[(size_t)yy * stride + (size_t)xx / 8u] >> (be ? 7 - (xx & 7) : (xx & 7))) & 1u) ? 255 : 0;
                        g->cov[(size_t)yy * w + xx] = c;
                    }
            }
        }
        off += fmt ? bytes : 0;
        x->st.glyphs++;
    }
}

/* Decide what character a stored glyph is (once). Blank glyphs with an advance are spaces. */
static void identify_glyph(ballum_xdraw_t *x, xglyph_t *g) {
    if (g->ident) return;
    g->ident = 1; g->known = 0; g->cp = 0xFFFDu;
    if (g->w == 0 || g->h == 0) { g->cp = ' '; g->known = 1; return; }
    if (!g->cov || !x->chars) return;
    ballum_glyph_match_t m;
    if (ballum_charsource_identify(x->chars, g->cov, (uint32_t)g->w, (uint32_t)g->h, -1, &m) &&
        m.score >= BALLUM_XDRAW_MIN_SCORE && m.margin >= BALLUM_XDRAW_MIN_MARGIN) {
        g->cp = m.codepoint; g->known = 1; x->st.glyphs_known++;
    }
}

typedef struct {
    int have; irect r; char text[ELEM_TEXT]; size_t tl; int known;
} run_t;

static void flush_run(ballum_xdraw_t *x, uint32_t drawable, run_t *run, ballum_color_t fg) {
    if (run->have && !rect_empty(run->r)) {
        ballum_color_t unknown_bg = { 0, 0, 0, 0 };
        add_elem(x, drawable, BALLUM_ELEM_TEXT, run->r, fg, unknown_bg, run->text, run->tl, run->known, 0);
    }
    memset(run, 0, sizeof(*run));
}

static void r_composite_glyphs(ballum_xdraw_t *x, const ballum_xreq_t *r, int size) {
    if (r->body_len < 24 || r->truncated) return;
    int be = r->big_endian;
    xpic_t *src = find_pic(x, r32(r->body + 4, be)), *dst = find_pic(x, r32(r->body + 8, be));
    if (!dst || dst->solid) return;
    ballum_color_t fg = { 0, 0, 0, 0 };
    if (src && src->solid) fg = src->color;
    uint32_t gs = glyph_root(x, r32(r->body + 16, be));
    int32_t pen_x = 0, pen_y = 0;
    run_t run; memset(&run, 0, sizeof(run));
    size_t p = 24;
    while (p + 8 <= r->body_len) {
        uint8_t len = r->body[p];
        if (len == 255) {                                      /* switch glyph set */
            if (p + 12 > r->body_len) break;
            gs = glyph_root(x, r32(r->body + p + 8, be));
            p += 12;
            continue;
        }
        pen_x += s16(r->body + p + 4, be); pen_y += s16(r->body + p + 6, be);
        p += 8;
        size_t bytes = (size_t)len * (size_t)size;
        if (p + bytes > r->body_len) break;
        for (uint8_t i = 0; i < len; ++i) {
            const uint8_t *q = r->body + p + (size_t)i * size;
            uint32_t gid = size == 1 ? q[0] : size == 2 ? r16(q, be) : r32(q, be);
            xglyph_t *g = glyph_find(x, gs, gid);
            if (!g) { run.known = 0; continue; }
            identify_glyph(x, g);
            if (g->w > 0 && g->h > 0) {
                irect box = mk(pen_x - g->x, pen_y - g->y, g->w, g->h);
                if (run.have) {
                    int32_t mid = box.y + box.h / 2, run_mid = run.r.y + run.r.h / 2;
                    int32_t dy = mid > run_mid ? mid - run_mid : run_mid - mid;
                    int32_t gap = box.x - (run.r.x + run.r.w);
                    int32_t limit = run.r.h > 8 ? run.r.h : 8;
                    if (dy > run.r.h / 2 || gap > limit || gap < -(run.r.w + 2)) flush_run(x, dst->drawable, &run, fg);
                }
                if (!run.have) { run.have = 1; run.r = box; run.known = 1; run.tl = 0; }
                else {
                    int32_t x1 = run.r.x + run.r.w > box.x + box.w ? run.r.x + run.r.w : box.x + box.w;
                    int32_t y1 = run.r.y + run.r.h > box.y + box.h ? run.r.y + run.r.h : box.y + box.h;
                    if (box.x < run.r.x) run.r.x = box.x;
                    if (box.y < run.r.y) run.r.y = box.y;
                    run.r.w = x1 - run.r.x; run.r.h = y1 - run.r.y;
                }
            } else if (!run.have) {
                pen_x += g->offx; pen_y += g->offy;           /* a leading space starts no run */
                continue;
            }
            size_t put = utf8_put(run.text + run.tl, sizeof(run.text) - 1 - run.tl, g->cp);
            if (put == 0) run.known = 0; else run.tl += put;
            if (!g->known) run.known = 0;
            pen_x += g->offx; pen_y += g->offy;
        }
        p += (bytes + 3u) & ~(size_t)3u;
    }
    flush_run(x, dst->drawable, &run, fg);
}

static void r_fill_rectangles(ballum_xdraw_t *x, const ballum_xreq_t *r) {
    if (r->body_len < 16) return;
    int be = r->big_endian;
    xpic_t *dst = find_pic(x, r32(r->body + 4, be));
    if (!dst || dst->solid) return;
    ballum_color_t c = render_rgb(r->body + 8, be);
    for (size_t p = 16, k = 0; p + 8 <= r->body_len && k < 64; p += 8, ++k)
        add_elem(x, dst->drawable, BALLUM_ELEM_FILL,
                 mk(s16(r->body + p, be), s16(r->body + p + 2, be), r16(r->body + p + 4, be), r16(r->body + p + 6, be)),
                 c, c, NULL, 0, 0, c.a == 255);
}

static void r_composite(ballum_xdraw_t *x, const ballum_xreq_t *r) {
    if (r->body_len < 32) return;
    int be = r->big_endian;
    xpic_t *src = find_pic(x, r32(r->body + 4, be)), *dst = find_pic(x, r32(r->body + 12, be));
    uint32_t mask = r32(r->body + 8, be);
    if (!src || !dst || dst->solid) return;
    int32_t sx = s16(r->body + 16, be), sy = s16(r->body + 18, be);
    int32_t dx = s16(r->body + 24, be), dy = s16(r->body + 26, be);
    int32_t w = r16(r->body + 28, be), h = r16(r->body + 30, be);
    if (src->solid) {
        add_elem(x, dst->drawable, mask ? BALLUM_ELEM_IMAGE : BALLUM_ELEM_FILL, mk(dx, dy, w, h),
                 src->color, src->color, NULL, 0, 0, !mask && src->color.a == 255);
        return;
    }
    copy_region(x, src->drawable, dst->drawable, mk(sx, sy, w, h), dx, dy);
}

static void do_render(ballum_xdraw_t *x, const ballum_xreq_t *r) {
    switch (r->data) {
    case R_CREATE_PICTURE:       r_create_picture(x, r); break;
    case R_FREE_PICTURE: {
        if (r->body_len >= 4) { xpic_t *p = find_pic(x, r32(r->body, r->big_endian)); if (p) memset(p, 0, sizeof(*p)); }
        break;
    }
    case R_COMPOSITE:            r_composite(x, r); break;
    case R_CREATE_GLYPHSET:      r_glyphset(x, r, 0); break;
    case R_REFERENCE_GLYPHSET:   r_glyphset(x, r, 1); break;
    case R_FREE_GLYPHSET:
        if (r->body_len >= 4) free_glyphs_of(x, glyph_root(x, r32(r->body, r->big_endian)));
        break;
    case R_ADD_GLYPHS:           r_add_glyphs(x, r); break;
    case R_FREE_GLYPHS:
        if (r->body_len >= 8) {
            uint32_t gs = glyph_root(x, r32(r->body, r->big_endian));
            for (size_t p = 4; p + 4 <= r->body_len; p += 4) {
                xglyph_t *g = glyph_find(x, gs, r32(r->body + p, r->big_endian));
                if (g) glyph_release(x, g);
            }
        }
        break;
    case R_COMPOSITE_GLYPHS8:    r_composite_glyphs(x, r, 1); break;
    case R_COMPOSITE_GLYPHS16:   r_composite_glyphs(x, r, 2); break;
    case R_COMPOSITE_GLYPHS32:   r_composite_glyphs(x, r, 4); break;
    case R_FILL_RECTANGLES:      r_fill_rectangles(x, r); break;
    case R_CREATE_SOLID_FILL:    r_create_solid(x, r); break;
    default: break;
    }
}

/* ---- the wire callbacks --------------------------------------------------------------------------- */

static int is_drawing(uint8_t op, const char *ext, uint8_t minor) {
    if (ext) return strcmp(ext, "RENDER") == 0 && (minor == R_COMPOSITE || (minor >= R_COMPOSITE_GLYPHS8 && minor <= R_FILL_RECTANGLES));
    return op == OP_CLEAR_AREA || op == OP_COPY_AREA || op == OP_POLY_FILL_RECT || op == OP_PUT_IMAGE ||
           (op >= OP_POLY_TEXT8 && op <= OP_IMAGE_TEXT16);
}

int ballum_xdraw_on_request(void *user, const ballum_xreq_t *r, uint32_t *save) {
    ballum_xdraw_t *x = (ballum_xdraw_t *)user;
    if (!x || !r) return 0;
    int want = 0;
    pthread_mutex_lock(&x->lock);
    x->st.requests++;
    if (is_drawing(r->opcode, r->ext, r->data)) { x->st.drawing++; x->last_draw_ms = now_ms(); }
    int be = r->big_endian;
    if (r->ext) {
        if (strcmp(r->ext, "RENDER") == 0) do_render(x, r);
    } else {
        switch (r->opcode) {
        case OP_CREATE_WINDOW:  do_create_window(x, r); break;
        case OP_DESTROY_WINDOW: if (r->body_len >= 4) do_destroy_window(x, r32(r->body, be)); break;
        case OP_REPARENT:
            if (r->body_len >= 12) { xwin_t *w = find_win(x, r32(r->body, be));
                if (w) { w->parent = r32(r->body + 4, be); w->x = s16(r->body + 8, be); w->y = s16(r->body + 10, be); } }
            break;
        case OP_MAP:   if (r->body_len >= 4) { xwin_t *w = find_win(x, r32(r->body, be)); if (w) w->mapped = 1; } break;
        case OP_UNMAP: if (r->body_len >= 4) { xwin_t *w = find_win(x, r32(r->body, be)); if (w) w->mapped = 0; } break;
        case OP_CONFIGURE:
            if (r->body_len >= 8) {
                xwin_t *w = find_win(x, r32(r->body, be));
                uint16_t mask = r16(r->body + 4, be);
                size_t off = 8;
                for (int bit = 0; w && bit < 7; ++bit) {
                    if (!(mask & (1u << bit))) continue;
                    if (off + 4 > r->body_len) break;
                    int32_t v = (int32_t)r32(r->body + off, be);
                    if (bit == 0) w->x = (int16_t)v; else if (bit == 1) w->y = (int16_t)v;
                    else if (bit == 2) w->w = (int32_t)(uint16_t)v; else if (bit == 3) w->h = (int32_t)(uint16_t)v;
                    off += 4;
                }
            }
            break;
        case OP_OPEN_FONT:  do_open_font(x, r); break;
        case OP_CLOSE_FONT: if (r->body_len >= 4) do_close_font(x, r32(r->body, be)); break;
        case OP_QUERY_FONT:
            if (r->body_len >= 4) { *save = r32(r->body, be); want = 1; }
            break;
        case OP_CREATE_PIXMAP:
            if (r->body_len >= 12) {
                int fi; uint32_t id = r32(r->body, be);
                xpix_t *p = find_pix(x, id);
                if (!p) { p = (xpix_t *)slot(x->pix, sizeof(xpix_t), MAX_PIXMAPS, &fi); if (!p) { x->st.dropped++; break; } }
                elist_free(&p->el);
                p->used = 1; p->id = id; p->w = r16(r->body + 8, be); p->h = r16(r->body + 10, be);
            }
            break;
        case OP_FREE_PIXMAP:
            if (r->body_len >= 4) { xpix_t *p = find_pix(x, r32(r->body, be)); if (p) { elist_free(&p->el); memset(p, 0, sizeof(*p)); } }
            break;
        case OP_CREATE_GC:      do_create_gc(x, r); break;
        case OP_CHANGE_GC:      do_change_gc(x, r); break;
        case OP_FREE_GC:        if (r->body_len >= 4) { xgc_t *g = find_gc(x, r32(r->body, be)); if (g) memset(g, 0, sizeof(*g)); } break;
        case OP_CLEAR_AREA:     do_clear_area(x, r); break;
        case OP_COPY_AREA:      do_copy_area(x, r); break;
        case OP_POLY_FILL_RECT: do_fill_rects(x, r); break;
        case OP_PUT_IMAGE:      do_put_image(x, r); break;
        case OP_POLY_TEXT8:     do_poly_text(x, r, 0); break;
        case OP_POLY_TEXT16:    do_poly_text(x, r, 1); break;
        case OP_IMAGE_TEXT8:    do_image_text(x, r, 0); break;
        case OP_IMAGE_TEXT16:   do_image_text(x, r, 1); break;
        default: break;
        }
    }
    pthread_mutex_unlock(&x->lock);
    return want;
}

void ballum_xdraw_on_reply(void *user, const ballum_xreply_t *rep) {
    ballum_xdraw_t *x = (ballum_xdraw_t *)user;
    if (!x || !rep) return;
    pthread_mutex_lock(&x->lock);
    if (!rep->ext && rep->opcode == OP_QUERY_FONT) query_font_reply(x, rep);
    pthread_mutex_unlock(&x->lock);
}

void ballum_xdraw_on_event(void *user, const uint8_t *ev, size_t len, int be) {
    ballum_xdraw_t *x = (ballum_xdraw_t *)user;
    if (!x || !ev || len < 32) return;
    uint8_t type = ev[0] & 0x7Fu;
    pthread_mutex_lock(&x->lock);
    xwin_t *w = (type >= 17 && type <= 22) ? find_win(x, r32(ev + 8, be)) : NULL;
    if (w) {
        if (type == 17) do_destroy_window(x, w->id);                   /* DestroyNotify */
        else if (type == 18) w->mapped = 0;                             /* UnmapNotify   */
        else if (type == 19) w->mapped = 1;                             /* MapNotify     */
        else if (type == 22) {                                          /* ConfigureNotify: where the server put it */
            w->x = s16(ev + 16, be); w->y = s16(ev + 18, be);
            w->w = r16(ev + 20, be); w->h = r16(ev + 22, be);
        }
    }
    pthread_mutex_unlock(&x->lock);
}

/* ---- the snapshot ---------------------------------------------------------------------------------- */

static int cmp_win_order(const void *a, const void *b) {
    const xwin_t *x = *(const xwin_t *const *)a, *y = *(const xwin_t *const *)b;
    return x->order < y->order ? -1 : (x->order > y->order ? 1 : 0);
}

size_t ballum_xdraw_snapshot(ballum_xdraw_t *x, uint32_t vw, uint32_t vh, uint64_t now, uint32_t settle_ms,
                             ballum_element_t *out, size_t cap, char *text, size_t text_cap, int *settled) {
    if (settled) *settled = 0;
    if (!x || !out || cap == 0 || vw == 0 || vh == 0) return 0;
    pthread_mutex_lock(&x->lock);
    if (settled) *settled = (now >= x->last_draw_ms && now - x->last_draw_ms >= settle_ms) || x->last_draw_ms == 0;

    const xwin_t *order[MAX_WINDOWS];
    size_t nw = 0;
    for (int i = 0; i < MAX_WINDOWS; ++i) if (x->win[i].used && x->win[i].mapped) order[nw++] = &x->win[i];
    qsort(order, nw, sizeof(order[0]), cmp_win_order);

    size_t n = 0, tused = 0;
    uint32_t z = 0;
    irect view = mk(0, 0, (int32_t)vw, (int32_t)vh);
    for (size_t wi = 0; wi < nw && n < cap; ++wi) {
        const xwin_t *w = order[wi];
        int32_t ox, oy;
        if (!win_origin(x, w, &ox, &oy)) continue;
        irect wr = intersect(mk(ox, oy, w->w, w->h), view);                 /* the window, on screen */
        for (size_t ei = 0; ei < w->el.n && n < cap; ++ei) {
            const xelem_t *e = &w->el.e[ei];
            irect abs = mk(e->r.x + ox, e->r.y + oy, e->r.w, e->r.h);
            irect c = intersect(abs, wr);
            if (rect_empty(c)) continue;
            ballum_element_t *o = &out[n];
            memset(o, 0, sizeof(*o));
            o->kind = (ballum_elem_kind_t)e->kind;
            o->rect.x = (uint32_t)c.x; o->rect.y = (uint32_t)c.y; o->rect.width = (uint32_t)c.w; o->rect.height = (uint32_t)c.h;
            o->fg = e->fg; o->bg = e->bg; o->z = z++;
            o->glyph_known = e->known && c.x == abs.x && c.y == abs.y && c.w == abs.w && c.h == abs.h;   /* a clipped run may be partial */
            if (e->kind == BALLUM_ELEM_TEXT && e->tlen && text && tused + e->tlen + 1u <= text_cap) {
                memcpy(text + tused, e->text, e->tlen);
                text[tused + e->tlen] = '\0';
                o->text = text + tused; o->text_len = e->tlen;
                tused += e->tlen + 1u;
            } else if (e->kind == BALLUM_ELEM_TEXT) {
                o->glyph_known = 0;
            }
            ++n;
        }
    }
    pthread_mutex_unlock(&x->lock);
    return n;
}
