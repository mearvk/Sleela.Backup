/*
 * ballum/src/ballum_charsource.c -- character sourcing (see ballum_charsource.h).
 */
#include "sleela_ballum/ballum_charsource.h"

#include <stdlib.h>
#include <string.h>

#define MAX_CELL    256u
#define MAX_GLYPHS  65536u

/* ---- charsets ---------------------------------------------------------------- */

static const char *const k_set_names[BALLUM_CHARSET__COUNT] = { "ascii", "latin1", "digits", "alnum" };

const char *ballum_charset_name(ballum_charset_t cs) {
    return ((unsigned)cs < (unsigned)BALLUM_CHARSET__COUNT) ? k_set_names[cs] : "unknown";
}

int ballum_charset_by_name(const char *name, ballum_charset_t *out) {
    if (!name || !out) return 0;
    for (int i = 0; i < (int)BALLUM_CHARSET__COUNT; ++i)
        if (strcmp(name, k_set_names[i]) == 0) { *out = (ballum_charset_t)i; return 1; }
    return 0;
}

size_t ballum_charset_size(ballum_charset_t cs) {
    switch (cs) {
    case BALLUM_CHARSET_ASCII:  return 95u;
    case BALLUM_CHARSET_LATIN1: return 95u + 96u;
    case BALLUM_CHARSET_DIGITS: return 10u;
    case BALLUM_CHARSET_ALNUM:  return 62u;
    default:                    return 0u;
    }
}

uint32_t ballum_charset_at(ballum_charset_t cs, size_t i) {
    if (i >= ballum_charset_size(cs)) return 0u;
    switch (cs) {
    case BALLUM_CHARSET_ASCII:  return 0x20u + (uint32_t)i;
    case BALLUM_CHARSET_LATIN1: return i < 95u ? 0x20u + (uint32_t)i : 0xA0u + (uint32_t)(i - 95u);
    case BALLUM_CHARSET_DIGITS: return 0x30u + (uint32_t)i;
    case BALLUM_CHARSET_ALNUM:
        if (i < 10u) return 0x30u + (uint32_t)i;
        if (i < 36u) return 0x41u + (uint32_t)(i - 10u);
        return 0x61u + (uint32_t)(i - 36u);
    default: return 0u;
    }
}

int ballum_charset_contains(ballum_charset_t cs, uint32_t cp) {
    size_t n = ballum_charset_size(cs);
    for (size_t i = 0; i < n; ++i) if (ballum_charset_at(cs, i) == cp) return 1;
    return 0;
}

/* ---- the source ---------------------------------------------------------------- */

typedef struct {
    uint32_t cp;
    int      family;
    uint32_t cell_w, cell_h;
    int32_t  advance;
    uint8_t *cell;          /* cell_w * cell_h coverage, what render() returns */
    uint32_t tw, th;        /* ink bounding box; 0 x 0 for a blank glyph        */
    uint8_t *tight;         /* tw * th coverage cropped to the ink              */
} glyph_t;

struct ballum_charsource {
    glyph_t *g;
    size_t   n, cap;
};

ballum_charsource_t *ballum_charsource_create(void) {
    return (ballum_charsource_t *)calloc(1, sizeof(ballum_charsource_t));
}

static void free_glyph(glyph_t *g) { free(g->cell); free(g->tight); memset(g, 0, sizeof(*g)); }

void ballum_charsource_clear(ballum_charsource_t *cs) {
    if (!cs) return;
    for (size_t i = 0; i < cs->n; ++i) free_glyph(&cs->g[i]);
    cs->n = 0;
}

void ballum_charsource_destroy(ballum_charsource_t *cs) {
    if (!cs) return;
    ballum_charsource_clear(cs);
    free(cs->g);
    free(cs);
}

size_t ballum_charsource_glyph_count(const ballum_charsource_t *cs) { return cs ? cs->n : 0u; }

/* Ink bounding box of a coverage bitmap; returns 0 when there is no ink. */
static int ink_box(const uint8_t *c, uint32_t w, uint32_t h, uint32_t *x0, uint32_t *y0, uint32_t *x1, uint32_t *y1) {
    uint32_t ax = w, ay = h, bx = 0, by = 0;
    int any = 0;
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x)
            if (c[(size_t)y * w + x]) {
                any = 1;
                if (x < ax) ax = x;
                if (y < ay) ay = y;
                if (x > bx) bx = x;
                if (y > by) by = y;
            }
    if (!any) return 0;
    *x0 = ax; *y0 = ay; *x1 = bx + 1u; *y1 = by + 1u;
    return 1;
}

ballum_status_t ballum_charsource_add_glyph(ballum_charsource_t *cs, uint32_t cp, int family,
                                            const uint8_t *cov, uint32_t w, uint32_t h, int32_t advance) {
    if (!cs || !cov || w == 0 || h == 0 || w > MAX_CELL || h > MAX_CELL || family < 0 || family > 15 ||
        advance < 0 || advance > (int32_t)(4u * h))
        return BALLUM_STATUS_INVALID_ARGUMENT;

    glyph_t *slot = NULL;
    for (size_t i = 0; i < cs->n; ++i)
        if (cs->g[i].cp == cp && cs->g[i].family == family) { slot = &cs->g[i]; free_glyph(slot); break; }
    if (!slot) {
        if (cs->n >= MAX_GLYPHS) return BALLUM_STATUS_BUFFER_TOO_SMALL;
        if (cs->n == cs->cap) {
            size_t nc = cs->cap ? cs->cap * 2u : 128u;
            glyph_t *ng = (glyph_t *)realloc(cs->g, nc * sizeof(glyph_t));
            if (!ng) return BALLUM_STATUS_BACKEND_ERROR;
            cs->g = ng; cs->cap = nc;
        }
        slot = &cs->g[cs->n];
    }
    memset(slot, 0, sizeof(*slot));
    slot->cp = cp; slot->family = family; slot->cell_w = w; slot->cell_h = h; slot->advance = advance;
    slot->cell = (uint8_t *)malloc((size_t)w * h);
    if (!slot->cell) return BALLUM_STATUS_BACKEND_ERROR;
    memcpy(slot->cell, cov, (size_t)w * h);

    uint32_t x0, y0, x1, y1;
    if (ink_box(cov, w, h, &x0, &y0, &x1, &y1)) {
        slot->tw = x1 - x0; slot->th = y1 - y0;
        slot->tight = (uint8_t *)malloc((size_t)slot->tw * slot->th);
        if (!slot->tight) { free(slot->cell); memset(slot, 0, sizeof(*slot)); return BALLUM_STATUS_BACKEND_ERROR; }
        for (uint32_t y = 0; y < slot->th; ++y)
            memcpy(slot->tight + (size_t)y * slot->tw, cov + (size_t)(y0 + y) * w + x0, slot->tw);
    }
    if (slot == &cs->g[cs->n]) cs->n++;
    return BALLUM_STATUS_OK;
}

/* ---- pixel maps ------------------------------------------------------------------ */

static uint8_t coverage_of(ballum_color_t p, ballum_cover_t mode) {
    int luma = (299 * p.r + 587 * p.g + 114 * p.b) / 1000;
    switch (mode) {
    case BALLUM_COVER_ALPHA:     return p.a;
    case BALLUM_COVER_DARK_INK:  return (uint8_t)(255 - luma);
    case BALLUM_COVER_LIGHT_INK: return (uint8_t)luma;
    }
    return 0;
}

ballum_status_t ballum_charsource_add_pixmap(ballum_charsource_t *cs, const ballum_pixmap_font_t *pm, size_t *added) {
    if (added) *added = 0;
    if (!cs || !pm || !pm->pixels) return BALLUM_STATUS_INVALID_ARGUMENT;
    if (pm->cell_w == 0 || pm->cell_h == 0 || pm->cell_w > MAX_CELL || pm->cell_h > MAX_CELL ||
        pm->width < pm->cell_w || pm->height < pm->cell_h || pm->family < 0 || pm->family > 15 ||
        (unsigned)pm->cover > (unsigned)BALLUM_COVER_LIGHT_INK)
        return BALLUM_STATUS_INVALID_ARGUMENT;
    if ((uint64_t)pm->width * pm->height > ((uint64_t)1 << 26)) return BALLUM_STATUS_BUFFER_TOO_SMALL;

    size_t cols = pm->width / pm->cell_w, rows = pm->height / pm->cell_h, cells = cols * rows;
    size_t n = pm->codepoints ? pm->n_codepoints : ballum_charset_size(pm->charset);
    if (n == 0 || n > cells) return BALLUM_STATUS_INVALID_ARGUMENT;      /* more characters than cells */
    if (cs->n + n > MAX_GLYPHS) return BALLUM_STATUS_BUFFER_TOO_SMALL;

    uint8_t *cell = (uint8_t *)malloc((size_t)pm->cell_w * pm->cell_h);
    if (!cell) return BALLUM_STATUS_BACKEND_ERROR;
    size_t done = 0;
    ballum_status_t st = BALLUM_STATUS_OK;
    for (size_t i = 0; i < n && st == BALLUM_STATUS_OK; ++i) {
        size_t cx = (i % cols) * pm->cell_w, cy = (i / cols) * pm->cell_h;
        for (uint32_t y = 0; y < pm->cell_h; ++y)
            for (uint32_t x = 0; x < pm->cell_w; ++x)
                cell[(size_t)y * pm->cell_w + x] =
                    coverage_of(pm->pixels[(cy + y) * pm->width + cx + x], pm->cover);
        uint32_t cp = pm->codepoints ? pm->codepoints[i] : ballum_charset_at(pm->charset, i);
        st = ballum_charsource_add_glyph(cs, cp, pm->family, cell, pm->cell_w, pm->cell_h, (int32_t)pm->cell_w);
        if (st == BALLUM_STATUS_OK) ++done;
    }
    free(cell);
    if (added) *added = done;
    return st;
}

/* Skip PGM whitespace and # comments; returns the next token as a number or -1. */
static long pgm_number(const uint8_t *d, size_t len, size_t *pos) {
    size_t p = *pos;
    for (;;) {
        while (p < len && (d[p] == ' ' || d[p] == '\t' || d[p] == '\n' || d[p] == '\r')) ++p;
        if (p < len && d[p] == '#') { while (p < len && d[p] != '\n') ++p; continue; }
        break;
    }
    if (p >= len || d[p] < '0' || d[p] > '9') return -1;
    long v = 0;
    while (p < len && d[p] >= '0' && d[p] <= '9') {
        v = v * 10 + (d[p] - '0');
        if (v > 1000000) return -1;
        ++p;
    }
    *pos = p;
    return v;
}

ballum_status_t ballum_charsource_add_pgm(ballum_charsource_t *cs, const uint8_t *d, size_t len,
                                          uint32_t cell_w, uint32_t cell_h, ballum_charset_t charset,
                                          int family, ballum_cover_t cover, size_t *added) {
    if (added) *added = 0;
    if (!cs || !d || len < 8 || d[0] != 'P' || d[1] != '5') return BALLUM_STATUS_INVALID_ARGUMENT;
    if (cover != BALLUM_COVER_DARK_INK && cover != BALLUM_COVER_LIGHT_INK) return BALLUM_STATUS_INVALID_ARGUMENT;
    size_t pos = 2;
    long w = pgm_number(d, len, &pos), h = pgm_number(d, len, &pos), maxv = pgm_number(d, len, &pos);
    if (w <= 0 || h <= 0 || maxv != 255) return BALLUM_STATUS_INVALID_ARGUMENT;     /* 8-bit only */
    if (pos >= len) return BALLUM_STATUS_INVALID_ARGUMENT;
    ++pos;                                                                          /* the one whitespace byte */
    if ((uint64_t)w * (uint64_t)h > ((uint64_t)1 << 26)) return BALLUM_STATUS_BUFFER_TOO_SMALL;
    if ((uint64_t)len - pos < (uint64_t)w * (uint64_t)h) return BALLUM_STATUS_INVALID_ARGUMENT;   /* truncated */

    ballum_color_t *px = (ballum_color_t *)malloc((size_t)w * (size_t)h * sizeof(ballum_color_t));
    if (!px) return BALLUM_STATUS_BACKEND_ERROR;
    for (size_t i = 0; i < (size_t)w * (size_t)h; ++i) { px[i].r = px[i].g = px[i].b = d[pos + i]; px[i].a = 255; }
    ballum_pixmap_font_t pm;
    memset(&pm, 0, sizeof(pm));
    pm.pixels = px; pm.width = (uint32_t)w; pm.height = (uint32_t)h; pm.cell_w = cell_w; pm.cell_h = cell_h;
    pm.charset = charset; pm.family = family; pm.cover = cover;
    ballum_status_t st = ballum_charsource_add_pixmap(cs, &pm, added);
    free(px);
    return st;
}

/* ---- drawing -------------------------------------------------------------------- */

static const glyph_t *find_glyph(const ballum_charsource_t *cs, uint32_t cp, int family) {
    for (size_t i = 0; i < cs->n; ++i)
        if (cs->g[i].cp == cp && cs->g[i].family == family) return &cs->g[i];
    return NULL;
}

static int cs_render(void *user, uint32_t cp, int family, uint32_t height,
                     uint8_t *cov, size_t cap, uint32_t *w, uint32_t *h, int32_t *adv) {
    const ballum_charsource_t *cs = (const ballum_charsource_t *)user;
    if (!cs || !cov || !w || !h || !adv) return 0;
    const glyph_t *g = find_glyph(cs, cp, family);
    if (!g) g = find_glyph(cs, cp, 0);                         /* a family without it falls back to family 0 */
    if (!g || g->cell_h > height || (size_t)g->cell_w * g->cell_h > cap) return 0;   /* never scaled silently */
    memcpy(cov, g->cell, (size_t)g->cell_w * g->cell_h);
    *w = g->cell_w; *h = g->cell_h; *adv = g->advance;
    return 1;
}

ballum_glyph_source_t ballum_charsource_glyph_source(ballum_charsource_t *cs) {
    ballum_glyph_source_t s;
    s.render = cs_render;
    s.user = cs;
    return s;
}

/* ---- recognising ------------------------------------------------------------------ */

int ballum_charsource_identify(const ballum_charsource_t *cs, const uint8_t *cov, uint32_t w, uint32_t h,
                               int family, ballum_glyph_match_t *out) {
    if (!cs || !cov || !out || w == 0 || h == 0 || w > 4096u || h > 4096u) return 0;
    uint32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    int has = ink_box(cov, w, h, &x0, &y0, &x1, &y1);
    uint32_t tw = has ? x1 - x0 : 0u, th = has ? y1 - y0 : 0u;

    int best = -1, best_score = -1; uint32_t best_cp = 0; int best_fam = 0;
    /* best score per DIFFERENT character, to report the margin */
    int second = 0;
    for (size_t i = 0; i < cs->n; ++i) {
        const glyph_t *g = &cs->g[i];
        if (family >= 0 && g->family != family) continue;
        if (g->tw != tw || g->th != th) continue;                   /* a different shape size is not a match */
        int score;
        if (!has) {
            score = 1000;                                           /* blank against blank */
        } else {
            uint64_t diff = 0;
            for (uint32_t y = 0; y < th; ++y)
                for (uint32_t x = 0; x < tw; ++x) {
                    int a = cov[(size_t)(y0 + y) * w + x0 + x], b = g->tight[(size_t)y * tw + x];
                    diff += (uint64_t)(a > b ? a - b : b - a);
                }
            score = 1000 - (int)(diff * 1000u / ((uint64_t)255u * tw * th));
        }
        if (score > best_score || (score == best_score && best >= 0 && g->cp < best_cp)) {
            if (best >= 0 && best_cp != g->cp && best_score > second) second = best_score;
            best = (int)i; best_score = score; best_cp = g->cp; best_fam = g->family;
        } else if (g->cp != best_cp && score > second) {
            second = score;
        }
    }
    if (best < 0) return 0;
    out->codepoint = best_cp;
    out->family = best_fam;
    out->score = best_score;
    out->margin = best_score - second;
    return 1;
}
