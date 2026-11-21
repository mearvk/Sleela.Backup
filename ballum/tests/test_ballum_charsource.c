/* Tests for character sourcing: charsets, pixel maps, PGM atlases, drawing
 * through the interpreter, and recognising glyph images. */
#include "sleela_ballum/ballum_charsource.h"
#include "sleela_ballum/ballum_interp.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CW 5u
#define CH 7u

/* A deterministic 5x7 pattern per code point, different for every code point. */
static uint8_t pattern_px(uint32_t cp, uint32_t x, uint32_t y) {
    uint32_t h = cp * 2654435761u ^ (x * 40503u + y * 9973u + 1u) * 2246822519u;
    h ^= h >> 13; h *= 3266489917u; h ^= h >> 16;
    return (h & 3u) == 0u ? 255 : 0;
}
static void make_cell(uint32_t cp, uint8_t *out) {
    for (uint32_t y = 0; y < CH; ++y) for (uint32_t x = 0; x < CW; ++x) out[y * CW + x] = pattern_px(cp, x, y);
    out[0] = 255; out[CW * CH - 1] = 255;                  /* ink in two opposite corners: a full-size bounding box */
}

/* An atlas of `n` cells, `cols` per row, dark ink on white. */
static ballum_color_t *make_atlas(const uint32_t *cps, size_t n, uint32_t cols, uint32_t *w, uint32_t *h) {
    uint32_t rows = (uint32_t)((n + cols - 1) / cols);
    *w = cols * CW; *h = rows * CH;
    ballum_color_t *px = (ballum_color_t *)malloc((size_t)*w * *h * sizeof(ballum_color_t));
    for (size_t i = 0; i < (size_t)*w * *h; ++i) { px[i].r = px[i].g = px[i].b = 255; px[i].a = 255; }
    for (size_t i = 0; i < n; ++i) {
        uint8_t cell[CW * CH]; make_cell(cps[i], cell);
        uint32_t cx = (uint32_t)(i % cols) * CW, cy = (uint32_t)(i / cols) * CH;
        for (uint32_t y = 0; y < CH; ++y) for (uint32_t x = 0; x < CW; ++x) {
            uint8_t v = (uint8_t)(255 - cell[y * CW + x]);
            ballum_color_t *p = &px[(size_t)(cy + y) * *w + cx + x];
            p->r = p->g = p->b = v;
        }
    }
    return px;
}

int main(void) {
    /* ---- charsets ---------------------------------------------------------------- */
    assert(ballum_charset_size(BALLUM_CHARSET_ASCII) == 95 && ballum_charset_size(BALLUM_CHARSET_LATIN1) == 191);
    assert(ballum_charset_size(BALLUM_CHARSET_DIGITS) == 10 && ballum_charset_size(BALLUM_CHARSET_ALNUM) == 62);
    assert(ballum_charset_size((ballum_charset_t)9) == 0);
    assert(ballum_charset_at(BALLUM_CHARSET_ASCII, 0) == ' ' && ballum_charset_at(BALLUM_CHARSET_ASCII, 94) == '~');
    assert(ballum_charset_at(BALLUM_CHARSET_ASCII, 95) == 0);
    assert(ballum_charset_at(BALLUM_CHARSET_LATIN1, 95) == 0xA0 && ballum_charset_at(BALLUM_CHARSET_LATIN1, 190) == 0xFF);
    assert(ballum_charset_at(BALLUM_CHARSET_ALNUM, 0) == '0' && ballum_charset_at(BALLUM_CHARSET_ALNUM, 10) == 'A' &&
           ballum_charset_at(BALLUM_CHARSET_ALNUM, 36) == 'a' && ballum_charset_at(BALLUM_CHARSET_ALNUM, 61) == 'z');
    assert(ballum_charset_contains(BALLUM_CHARSET_DIGITS, '7') && !ballum_charset_contains(BALLUM_CHARSET_DIGITS, 'x'));
    assert(ballum_charset_contains(BALLUM_CHARSET_LATIN1, 0xE9) && !ballum_charset_contains(BALLUM_CHARSET_ASCII, 0xE9));
    for (int i = 0; i < (int)BALLUM_CHARSET__COUNT; ++i) {
        ballum_charset_t back;
        assert(ballum_charset_by_name(ballum_charset_name((ballum_charset_t)i), &back) && (int)back == i);
        for (size_t k = 0; k < ballum_charset_size((ballum_charset_t)i); ++k)           /* no duplicates */
            for (size_t j = 0; j < k; ++j)
                assert(ballum_charset_at((ballum_charset_t)i, j) != ballum_charset_at((ballum_charset_t)i, k));
    }
    assert(!ballum_charset_by_name("klingon", &(ballum_charset_t){0}) && !ballum_charset_by_name(NULL, &(ballum_charset_t){0}));
    assert(strcmp(ballum_charset_name((ballum_charset_t)9), "unknown") == 0);

    /* ---- add_glyph validation -------------------------------------------------------- */
    ballum_charsource_t *cs = ballum_charsource_create();
    assert(cs && ballum_charsource_glyph_count(cs) == 0);
    uint8_t cell[CW * CH]; make_cell('A', cell);
    assert(ballum_charsource_add_glyph(NULL, 'A', 0, cell, CW, CH, 6) == BALLUM_STATUS_INVALID_ARGUMENT);
    assert(ballum_charsource_add_glyph(cs, 'A', 0, NULL, CW, CH, 6) == BALLUM_STATUS_INVALID_ARGUMENT);
    assert(ballum_charsource_add_glyph(cs, 'A', 0, cell, 0, CH, 6) == BALLUM_STATUS_INVALID_ARGUMENT);
    assert(ballum_charsource_add_glyph(cs, 'A', 0, cell, 257, CH, 6) == BALLUM_STATUS_INVALID_ARGUMENT);
    assert(ballum_charsource_add_glyph(cs, 'A', 16, cell, CW, CH, 6) == BALLUM_STATUS_INVALID_ARGUMENT);
    assert(ballum_charsource_add_glyph(cs, 'A', 0, cell, CW, CH, -1) == BALLUM_STATUS_INVALID_ARGUMENT);
    assert(ballum_charsource_add_glyph(cs, 'A', 0, cell, CW, CH, 4 * CH + 1) == BALLUM_STATUS_INVALID_ARGUMENT);
    assert(ballum_charsource_glyph_count(cs) == 0);                                  /* refusals add nothing */
    assert(ballum_charsource_add_glyph(cs, 'A', 0, cell, CW, CH, 6) == BALLUM_STATUS_OK);
    assert(ballum_charsource_add_glyph(cs, 'A', 0, cell, CW, CH, 6) == BALLUM_STATUS_OK);
    assert(ballum_charsource_glyph_count(cs) == 1);                                  /* same cp + family replaces */
    assert(ballum_charsource_add_glyph(cs, 'A', 1, cell, CW, CH, 6) == BALLUM_STATUS_OK);
    assert(ballum_charsource_glyph_count(cs) == 2);
    ballum_charsource_clear(cs);
    assert(ballum_charsource_glyph_count(cs) == 0);

    /* ---- a pixel map of a known charset -------------------------------------------------- */
    uint32_t digits[10];
    for (size_t i = 0; i < 10; ++i) digits[i] = ballum_charset_at(BALLUM_CHARSET_DIGITS, i);
    uint32_t aw, ah;
    ballum_color_t *atlas = make_atlas(digits, 10, 4, &aw, &ah);                     /* 4 columns, 3 rows: 12 cells */
    ballum_pixmap_font_t pm; memset(&pm, 0, sizeof(pm));
    pm.pixels = atlas; pm.width = aw; pm.height = ah; pm.cell_w = CW; pm.cell_h = CH;
    pm.charset = BALLUM_CHARSET_DIGITS; pm.family = 0; pm.cover = BALLUM_COVER_DARK_INK;
    size_t added = 99;
    assert(ballum_charsource_add_pixmap(cs, &pm, &added) == BALLUM_STATUS_OK && added == 10);
    assert(ballum_charsource_glyph_count(cs) == 10);

    /* refusals */
    ballum_pixmap_font_t bad = pm;
    bad.charset = BALLUM_CHARSET_ASCII;                                              assert(ballum_charsource_add_pixmap(cs, &bad, &added) == BALLUM_STATUS_INVALID_ARGUMENT && added == 0);
    bad = pm; bad.cell_w = 0;                                                        assert(ballum_charsource_add_pixmap(cs, &bad, NULL) == BALLUM_STATUS_INVALID_ARGUMENT);
    bad = pm; bad.cell_w = aw + 1;                                                   assert(ballum_charsource_add_pixmap(cs, &bad, NULL) == BALLUM_STATUS_INVALID_ARGUMENT);
    bad = pm; bad.cover = (ballum_cover_t)7;                                         assert(ballum_charsource_add_pixmap(cs, &bad, NULL) == BALLUM_STATUS_INVALID_ARGUMENT);
    bad = pm; bad.pixels = NULL;                                                     assert(ballum_charsource_add_pixmap(cs, &bad, NULL) == BALLUM_STATUS_INVALID_ARGUMENT);
    bad = pm; bad.family = 99;                                                       assert(ballum_charsource_add_pixmap(cs, &bad, NULL) == BALLUM_STATUS_INVALID_ARGUMENT);
    assert(ballum_charsource_add_pixmap(cs, NULL, NULL) == BALLUM_STATUS_INVALID_ARGUMENT);
    assert(ballum_charsource_glyph_count(cs) == 10);

    /* an explicit code point list, in a second family */
    uint32_t cps[3] = { 0x41, 0x42, 0x20AC };
    uint32_t bw, bh;
    ballum_color_t *atlas2 = make_atlas(cps, 3, 3, &bw, &bh);
    memset(&pm, 0, sizeof(pm));
    pm.pixels = atlas2; pm.width = bw; pm.height = bh; pm.cell_w = CW; pm.cell_h = CH;
    pm.codepoints = cps; pm.n_codepoints = 3; pm.family = 1; pm.cover = BALLUM_COVER_DARK_INK;
    assert(ballum_charsource_add_pixmap(cs, &pm, &added) == BALLUM_STATUS_OK && added == 3);
    assert(ballum_charsource_glyph_count(cs) == 13);
    free(atlas2);

    /* light ink on a dark page, and alpha coverage, give the same glyphs */
    {
        ballum_charsource_t *c2 = ballum_charsource_create();
        ballum_color_t *inv = (ballum_color_t *)malloc((size_t)aw * ah * sizeof(ballum_color_t));
        for (size_t i = 0; i < (size_t)aw * ah; ++i) { inv[i] = atlas[i]; inv[i].r = inv[i].g = inv[i].b = (uint8_t)(255 - atlas[i].r); inv[i].a = (uint8_t)(255 - atlas[i].r); }
        ballum_pixmap_font_t p2; memset(&p2, 0, sizeof(p2));
        p2.pixels = inv; p2.width = aw; p2.height = ah; p2.cell_w = CW; p2.cell_h = CH; p2.charset = BALLUM_CHARSET_DIGITS;
        p2.cover = BALLUM_COVER_LIGHT_INK;
        assert(ballum_charsource_add_pixmap(c2, &p2, &added) == BALLUM_STATUS_OK && added == 10);
        ballum_charsource_t *c3 = ballum_charsource_create();
        p2.cover = BALLUM_COVER_ALPHA;
        assert(ballum_charsource_add_pixmap(c3, &p2, &added) == BALLUM_STATUS_OK && added == 10);
        uint8_t want[CW * CH]; make_cell('7', want);
        ballum_glyph_match_t m;
        assert(ballum_charsource_identify(c2, want, CW, CH, 0, &m) && m.codepoint == '7' && m.score == 1000);
        assert(ballum_charsource_identify(c3, want, CW, CH, 0, &m) && m.codepoint == '7' && m.score == 1000);
        ballum_charsource_destroy(c2); ballum_charsource_destroy(c3); free(inv);
    }
    free(atlas);

    /* ---- recognising ------------------------------------------------------------------------ */
    for (size_t i = 0; i < 10; ++i) {                              /* every digit identifies as itself, exactly */
        uint8_t g[CW * CH]; make_cell(digits[i], g);
        ballum_glyph_match_t m;
        assert(ballum_charsource_identify(cs, g, CW, CH, 0, &m));
        assert(m.codepoint == digits[i] && m.score == 1000 && m.margin > 100);
    }
    {
        /* surrounded by blank pixels: the match trims both to their ink */
        uint8_t big[20 * 20]; memset(big, 0, sizeof(big));
        uint8_t g[CW * CH]; make_cell('3', g);
        for (uint32_t y = 0; y < CH; ++y) for (uint32_t x = 0; x < CW; ++x) big[(6 + y) * 20 + 9 + x] = g[y * CW + x];
        ballum_glyph_match_t m;
        assert(ballum_charsource_identify(cs, big, 20, 20, 0, &m) && m.codepoint == '3' && m.score == 1000);

        /* a few wrong pixels lower the score but not the answer */
        uint8_t noisy[CW * CH]; memcpy(noisy, g, sizeof(g));
        noisy[CW + 1] = noisy[CW + 1] ? 0 : 255; noisy[2 * CW + 3] = noisy[2 * CW + 3] ? 0 : 255;
        assert(ballum_charsource_identify(cs, noisy, CW, CH, 0, &m) && m.codepoint == '3' && m.score < 1000 && m.score > 800);
        /* faint antialiasing scales the score smoothly */
        uint8_t faint[CW * CH]; for (size_t i = 0; i < sizeof(g); ++i) faint[i] = g[i] ? 200 : 0;
        assert(ballum_charsource_identify(cs, faint, CW, CH, 0, &m) && m.codepoint == '3' && m.score < 1000 && m.score > 700);
        /* family filter */
        assert(ballum_charsource_identify(cs, g, CW, CH, 0, &m) && m.family == 0);
        uint8_t a[CW * CH]; make_cell(0x41, a);
        assert(ballum_charsource_identify(cs, a, CW, CH, 1, &m) && m.codepoint == 0x41 && m.family == 1);
        assert(!ballum_charsource_identify(cs, a, CW, CH, 0, &m) || m.codepoint != 0x41);       /* not held in family 0 */
        assert(ballum_charsource_identify(cs, a, CW, CH, -1, &m) && m.codepoint == 0x41);       /* any family */
        /* a shape size nothing has */
        uint8_t odd[9 * 9]; memset(odd, 255, sizeof(odd));
        assert(!ballum_charsource_identify(cs, odd, 9, 9, -1, &m));
        /* bad arguments */
        assert(!ballum_charsource_identify(NULL, g, CW, CH, 0, &m) && !ballum_charsource_identify(cs, NULL, CW, CH, 0, &m));
        assert(!ballum_charsource_identify(cs, g, 0, CH, 0, &m) && !ballum_charsource_identify(cs, g, CW, CH, 0, NULL));
        assert(!ballum_charsource_identify(cs, g, 5000, CH, 0, &m));
    }
    {
        /* ambiguity is reported, not hidden: two characters with identical pixels */
        ballum_charsource_t *amb = ballum_charsource_create();
        uint8_t g[CW * CH]; make_cell('O', g);
        assert(ballum_charsource_add_glyph(amb, 'O', 0, g, CW, CH, 6) == BALLUM_STATUS_OK);
        assert(ballum_charsource_add_glyph(amb, '0', 0, g, CW, CH, 6) == BALLUM_STATUS_OK);
        ballum_glyph_match_t m;
        assert(ballum_charsource_identify(amb, g, CW, CH, 0, &m) && m.score == 1000 && m.margin == 0 && m.codepoint == '0');
        ballum_charsource_destroy(amb);
    }
    {
        /* a blank image matches a blank glyph (a space), and only that */
        uint8_t blank[CW * CH]; memset(blank, 0, sizeof(blank));
        ballum_glyph_match_t m;
        assert(!ballum_charsource_identify(cs, blank, CW, CH, 0, &m));                   /* nothing blank held yet */
        assert(ballum_charsource_add_glyph(cs, ' ', 0, blank, CW, CH, CW) == BALLUM_STATUS_OK);
        assert(ballum_charsource_identify(cs, blank, CW, CH, 0, &m) && m.codepoint == ' ' && m.score == 1000);
        uint8_t g[CW * CH]; make_cell('5', g);
        assert(ballum_charsource_identify(cs, g, CW, CH, 0, &m) && m.codepoint == '5');   /* a blank glyph never matches ink */
    }

    /* ---- the interpreter draws with it, and what it draws is recognised ----------------------- */
    {
        enum { FW = 64, FH = 24 };
        static ballum_color_t frame[FW * FH], out[FW * FH];
        ballum_color_t white = { 255, 255, 255, 255 }, black = { 0, 0, 0, 255 };
        for (size_t i = 0; i < (size_t)FW * FH; ++i) frame[i] = white;
        ballum_interp_t *it = ballum_interp_create(BALLUM_INTERP_ALTERATION, FW, FH, NULL);
        assert(it && ballum_interp_begin_frame(it, frame) == BALLUM_STATUS_OK);
        ballum_element_t e; memset(&e, 0, sizeof(e));
        e.kind = BALLUM_ELEM_TEXT; e.rect.x = 4; e.rect.y = 4; e.rect.width = 40; e.rect.height = 10;
        e.fg = black; e.bg = white;
        assert(ballum_interp_submit(it, &e) == BALLUM_STATUS_OK);
        ballum_glyph_source_t gs = ballum_charsource_glyph_source(cs);
        assert(ballum_contact_available(it, BALLUM_CONTACT_TEXT) == 0);
        assert(ballum_interp_set_glyph_source(it, &gs) == BALLUM_STATUS_OK);
        ballum_filter_scores_t ok = { { 900, 900, 900, 900 } };
        assert(ballum_interp_apply_text(it, 0, "314", 3, &ok) == BALLUM_ALTER_APPLIED);
        assert(ballum_interp_end_frame(it, out) == BALLUM_STATUS_OK);
        /* each glyph sits at pen = index * cell width, top on the element's top row */
        const char *want = "314";
        for (int gi = 0; gi < 3; ++gi) {
            uint8_t got[CW * CH];
            for (uint32_t y = 0; y < CH; ++y) for (uint32_t x = 0; x < CW; ++x)
                got[y * CW + x] = (uint8_t)(255 - out[(size_t)(4 + y) * FW + 4 + (uint32_t)gi * CW + x].r);
            uint8_t ref[CW * CH]; make_cell((uint32_t)want[gi], ref);
            assert(memcmp(got, ref, sizeof(ref)) == 0);                                 /* pixel-exact against the atlas */
            ballum_glyph_match_t m;
            assert(ballum_charsource_identify(cs, got, CW, CH, 0, &m) && m.codepoint == (uint32_t)want[gi] && m.score == 1000);
        }
        ballum_interp_destroy(it);

        /* a line shorter than the cell cannot be drawn: never scaled silently */
        ballum_interp_t *it2 = ballum_interp_create(BALLUM_INTERP_ALTERATION, FW, FH, NULL);
        assert(ballum_interp_begin_frame(it2, frame) == BALLUM_STATUS_OK);
        e.rect.height = CH - 1;
        assert(ballum_interp_submit(it2, &e) == BALLUM_STATUS_OK);
        assert(ballum_interp_set_glyph_source(it2, &gs) == BALLUM_STATUS_OK);
        assert(ballum_interp_apply_text(it2, 0, "3", 1, &ok) == BALLUM_ALTER_APPLIED);   /* applied, but nothing could be drawn */
        assert(ballum_interp_end_frame(it2, out) == BALLUM_STATUS_OK);
        assert(memcmp(out, frame, sizeof(frame)) == 0);
        ballum_interp_destroy(it2);
    }

    /* ---- PGM atlases ------------------------------------------------------------------------ */
    {
        uint32_t cols = 4, aw2 = cols * CW, ah2 = 3 * CH;
        size_t n = (size_t)aw2 * ah2;
        uint8_t *file = (uint8_t *)malloc(n + 64);
        int hdr = snprintf((char *)file, 64, "P5\n# a comment\n%u %u\n255\n", aw2, ah2);
        for (uint32_t i = 0; i < 10; ++i) {
            uint8_t g[CW * CH]; make_cell(digits[i], g);
            uint32_t cx = (i % cols) * CW, cy = (i / cols) * CH;
            for (uint32_t y = 0; y < CH; ++y) for (uint32_t x = 0; x < CW; ++x) file[(size_t)hdr + (size_t)(cy + y) * aw2 + cx + x] = (uint8_t)(255 - g[y * CW + x]);
        }
        for (uint32_t i = 10; i < 12; ++i) {                                              /* the two unused cells: blank paper */
            uint32_t cx = (i % cols) * CW, cy = (i / cols) * CH;
            for (uint32_t y = 0; y < CH; ++y) for (uint32_t x = 0; x < CW; ++x) file[(size_t)hdr + (size_t)(cy + y) * aw2 + cx + x] = 255;
        }
        ballum_charsource_t *pg = ballum_charsource_create();
        size_t got = 0;
        assert(ballum_charsource_add_pgm(pg, file, (size_t)hdr + n, CW, CH, BALLUM_CHARSET_DIGITS, 0, BALLUM_COVER_DARK_INK, &got) == BALLUM_STATUS_OK && got == 10);
        uint8_t ref[CW * CH]; make_cell('9', ref);
        ballum_glyph_match_t m;
        assert(ballum_charsource_identify(pg, ref, CW, CH, 0, &m) && m.codepoint == '9' && m.score == 1000);
        /* refusals: truncated, wrong magic, 16-bit, alpha cover, text-mode, nonsense sizes */
        assert(ballum_charsource_add_pgm(pg, file, (size_t)hdr + n - 1, CW, CH, BALLUM_CHARSET_DIGITS, 0, BALLUM_COVER_DARK_INK, NULL) == BALLUM_STATUS_INVALID_ARGUMENT);
        assert(ballum_charsource_add_pgm(pg, file, (size_t)hdr + n, CW, CH, BALLUM_CHARSET_DIGITS, 0, BALLUM_COVER_ALPHA, NULL) == BALLUM_STATUS_INVALID_ARGUMENT);
        const uint8_t p2[] = "P2\n2 2\n255\n0 0 0 0\n";
        assert(ballum_charsource_add_pgm(pg, p2, sizeof(p2) - 1, 1, 1, BALLUM_CHARSET_DIGITS, 0, BALLUM_COVER_DARK_INK, NULL) == BALLUM_STATUS_INVALID_ARGUMENT);
        const uint8_t deep[] = "P5\n2 2\n65535\n\0\0\0\0\0\0\0\0";
        assert(ballum_charsource_add_pgm(pg, deep, sizeof(deep) - 1, 1, 1, BALLUM_CHARSET_DIGITS, 0, BALLUM_COVER_DARK_INK, NULL) == BALLUM_STATUS_INVALID_ARGUMENT);
        const uint8_t huge[] = "P5\n999999 999999\n255\nxxxx";
        assert(ballum_charsource_add_pgm(pg, huge, sizeof(huge) - 1, 1, 1, BALLUM_CHARSET_DIGITS, 0, BALLUM_COVER_DARK_INK, NULL) != BALLUM_STATUS_OK);
        assert(ballum_charsource_add_pgm(NULL, file, (size_t)hdr + n, CW, CH, BALLUM_CHARSET_DIGITS, 0, BALLUM_COVER_DARK_INK, NULL) == BALLUM_STATUS_INVALID_ARGUMENT);
        assert(ballum_charsource_add_pgm(pg, NULL, 100, CW, CH, BALLUM_CHARSET_DIGITS, 0, BALLUM_COVER_DARK_INK, NULL) == BALLUM_STATUS_INVALID_ARGUMENT);
        assert(ballum_charsource_glyph_count(pg) == 10);
        ballum_charsource_destroy(pg); free(file);
    }

    ballum_charsource_destroy(cs);
    ballum_charsource_destroy(NULL);
    puts("test_ballum_charsource: ok");
    return 0;
}
