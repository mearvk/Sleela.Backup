/* Tests for the Ballum interpreter: the basis-stable mode, the pixel-identity
 * invariants, the filter band, the 23 contact points and the audit trail. */
#include "sleela_ballum/ballum_interp.h"
#include "slcrypto.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 64u
#define H 32u
#define N ((size_t)W * H)

static const ballum_color_t WHITE = { 255, 255, 255, 255 };
static const ballum_color_t BLACK = { 0, 0, 0, 255 };
static const ballum_color_t RED = { 255, 0, 0, 255 };

static ballum_color_t g_src[N], g_dst[N];

static ballum_rect_t R(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    ballum_rect_t r = { x, y, w, h }; return r;
}
static int same(ballum_color_t a, ballum_color_t b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

/* White page, one glyph-like shape in the element (8,8,16,16): a 6x6 black
 * square at (13,13), plus a 50% gray antialias ring one pixel around it. */
static void make_scene(void) {
    for (size_t i = 0; i < N; ++i) g_src[i] = WHITE;
    ballum_color_t gray = { 128, 128, 128, 255 };
    for (uint32_t y = 12; y < 20; ++y)
        for (uint32_t x = 12; x < 20; ++x) g_src[(size_t)y * W + x] = gray;
    for (uint32_t y = 13; y < 19; ++y)
        for (uint32_t x = 13; x < 19; ++x) g_src[(size_t)y * W + x] = BLACK;
}

static ballum_element_t text_elem(const char *s, int known) {
    ballum_element_t e; memset(&e, 0, sizeof(e));
    e.kind = BALLUM_ELEM_TEXT; e.rect = R(8, 8, 16, 16);
    e.fg = BLACK; e.bg = WHITE; e.text = s; e.text_len = strlen(s); e.glyph_known = known;
    return e;
}

static ballum_filter_scores_t scores(int m, int c, int t, int f) {
    ballum_filter_scores_t s; s.score[0] = m; s.score[1] = c; s.score[2] = t; s.score[3] = f; return s;
}
static ballum_filter_scores_t good(void) { return scores(900, 900, 900, 900); }

/* Every pixel outside `r` must be identical to the source. */
static int outside_unchanged(ballum_rect_t r) {
    for (uint32_t y = 0; y < H; ++y)
        for (uint32_t x = 0; x < W; ++x) {
            int in = x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height;
            if (!in && memcmp(&g_dst[(size_t)y * W + x], &g_src[(size_t)y * W + x], sizeof(ballum_color_t)) != 0) return 0;
        }
    return 1;
}
static size_t ink_pixels(const ballum_color_t *f) {
    size_t n = 0;
    for (uint32_t y = 8; y < 24; ++y) for (uint32_t x = 8; x < 24; ++x) n += !same(f[(size_t)y * W + x], WHITE);
    return n;
}

static ballum_interp_t *begin(ballum_interp_mode_t mode, const ballum_element_t *e) {
    ballum_interp_t *it = ballum_interp_create(mode, W, H, NULL);
    assert(it);
    assert(ballum_interp_begin_frame(it, g_src) == BALLUM_STATUS_OK);
    if (e) assert(ballum_interp_submit(it, e) == BALLUM_STATUS_OK);
    return it;
}


/* A tiny glyph source for the tests: every printable character is a box whose
 * width is 4 + 2*family and whose height is half the line; a space is blank. */
static int box_glyph(void *user, uint32_t cp, int family, uint32_t height,
                     uint8_t *cov, size_t cap, uint32_t *w, uint32_t *h, int32_t *adv) {
    int *calls = (int *)user; if (calls) ++*calls;
    if (cp == 0xFFFDu) { *w = 2; *h = 2; memset(cov, 255, 4); *adv = 3; return 1; }
    if (cp == ' ') { *w = 1; *h = 1; cov[0] = 0; *adv = 4; return 1; }
    uint32_t gw = 4u + 2u * (uint32_t)family, gh = height / 2u;
    if ((size_t)gw * gh > cap) return 0;
    memset(cov, 255, (size_t)gw * gh);
    *w = gw; *h = gh; *adv = (int32_t)gw + 1;
    return 1;
}

static ballum_element_t block_elem(ballum_rect_t r, ballum_elem_kind_t kind) {
    ballum_element_t e; memset(&e, 0, sizeof(e));
    e.kind = kind; e.rect = r; e.fg = BLACK; e.bg = WHITE;
    return e;
}

int main(void) {
    make_scene();
    ballum_filter_scores_t ok = good();
    ballum_element_t el = text_elem("A", 1);

    /* ---- creation ------------------------------------------------------------ */
    assert(ballum_interp_create((ballum_interp_mode_t)7, W, H, NULL) == NULL);
    assert(ballum_interp_create(BALLUM_INTERP_NOTATION, 0, H, NULL) == NULL);
    assert(ballum_interp_create(BALLUM_INTERP_NOTATION, 0xFFFFFFFFu, 0xFFFFFFFFu, NULL) == NULL);
    assert(strcmp(ballum_interp_mode_name(BALLUM_INTERP_NOTATION), "notation") == 0);
    assert(strcmp(ballum_interp_mode_name(BALLUM_INTERP_ALTERATION), "alteration") == 0);
    assert(ballum_interp_mode(NULL) == BALLUM_INTERP_NOTATION);          /* NULL reads as the safe mode */

    /* ---- NOTATION: output is the input, byte for byte, whatever is asked ---- */
    {
        ballum_interp_t *it = begin(BALLUM_INTERP_NOTATION, &el);
        assert(ballum_interp_mode(it) == BALLUM_INTERP_NOTATION);
        ballum_color_t rep[16 * 16]; for (int i = 0; i < 256; ++i) rep[i] = RED;
        assert(ballum_interp_alter(it, 0, rep, 256, &ok) == BALLUM_ALTER_MODE_NOTATION);
        ballum_change_t c = { BALLUM_CONTACT_COLOR_FG, 0, RED };
        assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_MODE_NOTATION);
        c.point = BALLUM_CONTACT_CRYPTO_GRADE; c.value = 1;
        assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_MODE_NOTATION);
        assert(ballum_interp_recolor_text(it, "A", RED, &ok) == 0);
        assert(ballum_interp_element_count(it) == 1);                    /* it still observes */
        assert(ballum_interp_end_frame(it, g_dst) == BALLUM_STATUS_OK);
        assert(memcmp(g_dst, g_src, sizeof(g_src)) == 0);
        ballum_interp_stats_t st; ballum_interp_stats(it, &st);
        assert(st.frames == 1 && st.elements == 1 && st.alterations_applied == 0 && st.pixels_altered == 0);
        ballum_interp_destroy(it);
    }

    /* ---- ALTERATION with nothing altered: still pixel-identical -------------- */
    {
        ballum_interp_t *it = begin(BALLUM_INTERP_ALTERATION, &el);
        assert(ballum_interp_end_frame(it, g_dst) == BALLUM_STATUS_OK);
        assert(memcmp(g_dst, g_src, sizeof(g_src)) == 0);
        ballum_interp_destroy(it);
    }

    /* ---- frame protocol ------------------------------------------------------- */
    {
        ballum_interp_t *it = ballum_interp_create(BALLUM_INTERP_ALTERATION, W, H, NULL);
        assert(ballum_interp_submit(it, &el) == BALLUM_STATUS_NOT_INITIALIZED);
        assert(ballum_interp_end_frame(it, g_dst) == BALLUM_STATUS_NOT_INITIALIZED);
        assert(ballum_interp_begin_frame(it, NULL) == BALLUM_STATUS_INVALID_ARGUMENT);
        assert(ballum_interp_begin_frame(it, g_src) == BALLUM_STATUS_OK);
        ballum_element_t bad = el;
        bad.rect = R(60, 0, 16, 16);                                      assert(ballum_interp_submit(it, &bad) == BALLUM_STATUS_INVALID_ARGUMENT);
        bad.rect = R(0, 0, 0, 5);                                         assert(ballum_interp_submit(it, &bad) == BALLUM_STATUS_INVALID_ARGUMENT);
        bad.rect = R(0xFFFFFFF0u, 0, 0x20u, 4);                           assert(ballum_interp_submit(it, &bad) == BALLUM_STATUS_INVALID_ARGUMENT);
        bad = el; bad.kind = (ballum_elem_kind_t)9;                       assert(ballum_interp_submit(it, &bad) == BALLUM_STATUS_INVALID_ARGUMENT);
        bad = el; bad.text = NULL; bad.text_len = 3;                      assert(ballum_interp_submit(it, &bad) == BALLUM_STATUS_INVALID_ARGUMENT);
        bad = el; bad.text_len = 100000;                                  assert(ballum_interp_submit(it, &bad) == BALLUM_STATUS_INVALID_ARGUMENT);
        assert(ballum_interp_element_count(it) == 0);                    /* rejected elements are not recorded */
        assert(ballum_interp_submit(it, &el) == BALLUM_STATUS_OK);
        ballum_element_t back;
        assert(ballum_interp_element_at(it, 0, &back) && back.text_len == 1 && memcmp(back.text, "A", 1) == 0);
        assert(back.text != el.text);                                    /* the interpreter owns its copy */
        assert(!ballum_interp_element_at(it, 1, &back));
        assert(ballum_interp_end_frame(it, g_src) == BALLUM_STATUS_INVALID_ARGUMENT);          /* alias */
        assert(ballum_interp_end_frame(it, g_src + 5) == BALLUM_STATUS_INVALID_ARGUMENT);      /* overlap */
        assert(ballum_interp_end_frame(it, NULL) == BALLUM_STATUS_INVALID_ARGUMENT);
        assert(ballum_interp_end_frame(it, g_dst) == BALLUM_STATUS_OK);
        assert(ballum_interp_end_frame(it, g_dst) == BALLUM_STATUS_NOT_INITIALIZED);           /* one end per begin */
        ballum_interp_destroy(it);
    }

    /* ---- the 23 contact points ------------------------------------------------ */
    assert((int)BALLUM_CONTACT__COUNT == 23);
    {
        int implemented = 0;
        for (int i = 0; i < 23; ++i) {
            const char *nm = ballum_contact_name((ballum_contact_t)i);
            ballum_contact_t back;
            assert(strcmp(nm, "unknown") != 0 && ballum_contact_by_name(nm, &back) && (int)back == i);
            for (int j = 0; j < i; ++j) assert(strcmp(nm, ballum_contact_name((ballum_contact_t)j)) != 0);
            int imp = ballum_contact_implemented((ballum_contact_t)i);
            assert(imp == 0 || imp == 1);
            implemented += imp;
        }
        assert(implemented == 23);
        assert(ballum_contact_implemented((ballum_contact_t)99) == -1);
        assert(strcmp(ballum_contact_name((ballum_contact_t)99), "unknown") == 0);
        ballum_contact_t dummy;
        assert(!ballum_contact_by_name("nope", &dummy) && !ballum_contact_by_name(NULL, &dummy));
    }

    /* without a glyph source only TEXT and FONT_FAMILY are unavailable, and they refuse cleanly */
    {
        ballum_interp_t *it = begin(BALLUM_INTERP_ALTERATION, &el);
        for (int i = 0; i < 23; ++i) {
            int want = (i == (int)BALLUM_CONTACT_TEXT || i == (int)BALLUM_CONTACT_FONT_FAMILY) ? 0 : 1;
            assert(ballum_contact_available(it, (ballum_contact_t)i) == want);
        }
        assert(ballum_contact_available(it, (ballum_contact_t)99) == -1);
        ballum_change_t c = { BALLUM_CONTACT_FONT_FAMILY, 1, RED };
        assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_UNSUPPORTED);
        assert(ballum_interp_apply_text(it, 0, "Z", 1, &ok) == BALLUM_ALTER_UNSUPPORTED);
        c.point = BALLUM_CONTACT_TEXT;
        assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_INVALID);       /* TEXT has its own entry point */
        assert(ballum_interp_apply_text(it, 0, NULL, 1, &ok) == BALLUM_ALTER_INVALID);
        assert(ballum_interp_apply_text(it, 0, "x", 100000, &ok) == BALLUM_ALTER_INVALID);
        assert(ballum_interp_end_frame(it, g_dst) == BALLUM_STATUS_OK);
        assert(memcmp(g_dst, g_src, sizeof(g_src)) == 0);
        ballum_interp_destroy(it);
    }

    /* invariant: whatever implemented point is applied, only the element rect can change */
    {
        struct { ballum_contact_t p; int v; } cases[] = {
            { BALLUM_CONTACT_COLOR_FG, 0 }, { BALLUM_CONTACT_COLOR_BG, 0 }, { BALLUM_CONTACT_OPACITY, 40 },
            { BALLUM_CONTACT_BRIGHTNESS, -50 }, { BALLUM_CONTACT_CONTRAST, 60 }, { BALLUM_CONTACT_SATURATION, 0 },
            { BALLUM_CONTACT_VISIBILITY, 0 }, { BALLUM_CONTACT_EFFECT, 1 }, { BALLUM_CONTACT_BLUR, 3 },
            { BALLUM_CONTACT_UNDERLINE, 2 }, { BALLUM_CONTACT_STRIKETHROUGH, 1 }, { BALLUM_CONTACT_WEIGHT, 2 },
            { BALLUM_CONTACT_OUTLINE, 2 }, { BALLUM_CONTACT_POSITION_X, 5 }, { BALLUM_CONTACT_POSITION_Y, -7 },
            { BALLUM_CONTACT_SLANT, 60 }, { BALLUM_CONTACT_SIZE, 250 }, { BALLUM_CONTACT_SIZE, 40 },
            { BALLUM_CONTACT_SPACING, 9 }, { BALLUM_CONTACT_LINE_HEIGHT, 9 }, { BALLUM_CONTACT_Z_ORDER, 7 },
        };
        for (size_t k = 0; k < sizeof(cases) / sizeof(cases[0]); ++k) {
            ballum_interp_t *it = begin(BALLUM_INTERP_ALTERATION, &el);
            ballum_change_t c = { cases[k].p, cases[k].v, RED };
            assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_APPLIED);
            assert(ballum_interp_end_frame(it, g_dst) == BALLUM_STATUS_OK);
            assert(outside_unchanged(el.rect));
            ballum_interp_destroy(it);
        }
    }

    /* COLOR_FG keeps the glyph shape: ink recolours in proportion to coverage */
    {
        ballum_interp_t *it = begin(BALLUM_INTERP_ALTERATION, &el);
        ballum_change_t c = { BALLUM_CONTACT_COLOR_FG, 0, RED };
        assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_APPLIED);
        assert(ballum_interp_end_frame(it, g_dst) == BALLUM_STATUS_OK);
        assert(same(g_dst[(size_t)15 * W + 15], RED));                    /* full ink -> red   */
        assert(same(g_dst[(size_t)9 * W + 9], WHITE));                    /* paper untouched   */
        ballum_color_t aa = g_dst[(size_t)12 * W + 12];                   /* 50% ring -> pink  */
        assert(aa.r == 255 && aa.g > 100 && aa.g < 160 && aa.g == aa.b);
        ballum_interp_destroy(it);
    }
    /* COLOR_BG recolours the paper and keeps the ink */
    {
        ballum_interp_t *it = begin(BALLUM_INTERP_ALTERATION, &el);
        ballum_change_t c = { BALLUM_CONTACT_COLOR_BG, 0, RED };
        assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_APPLIED);
        assert(ballum_interp_end_frame(it, g_dst) == BALLUM_STATUS_OK);
        assert(same(g_dst[(size_t)9 * W + 9], RED));
        assert(same(g_dst[(size_t)15 * W + 15], BLACK));
        ballum_interp_destroy(it);
    }
    /* WEIGHT grows and shrinks the ink; an unchanged pixel stays exactly as drawn */
    {
        size_t base = ink_pixels(g_src);
        ballum_interp_t *it = begin(BALLUM_INTERP_ALTERATION, &el);
        ballum_change_t c = { BALLUM_CONTACT_WEIGHT, 1, BLACK };
        assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_APPLIED);
        assert(ballum_interp_end_frame(it, g_dst) == BALLUM_STATUS_OK);
        assert(ink_pixels(g_dst) > base);
        assert(same(g_dst[(size_t)15 * W + 15], BLACK));
        ballum_interp_destroy(it);
        it = begin(BALLUM_INTERP_ALTERATION, &el);
        c.value = -1;
        assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_APPLIED);
        assert(ballum_interp_end_frame(it, g_dst) == BALLUM_STATUS_OK);
        assert(ink_pixels(g_dst) < base);
        ballum_interp_destroy(it);
    }
    /* OPACITY, VISIBILITY, BRIGHTNESS, EFFECT, UNDERLINE, STRIKETHROUGH, BLUR */
    {
        ballum_interp_t *it = begin(BALLUM_INTERP_ALTERATION, &el);
        ballum_change_t c = { BALLUM_CONTACT_OPACITY, 0, RED };
        assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_APPLIED);
        assert(ballum_interp_end_frame(it, g_dst) == BALLUM_STATUS_OK);
        assert(ink_pixels(g_dst) == 0);                                   /* 0% opacity: all paper */
        ballum_interp_destroy(it);

        it = begin(BALLUM_INTERP_ALTERATION, &el);
        c.point = BALLUM_CONTACT_VISIBILITY; c.value = 0;
        assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_APPLIED);
        c.point = BALLUM_CONTACT_BRIGHTNESS; c.value = -100;
        assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_APPLIED);   /* hidden then darkened */
        c.point = BALLUM_CONTACT_VISIBILITY; c.value = 1;                       /* restore the original */
        assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_APPLIED);
        assert(ballum_interp_end_frame(it, g_dst) == BALLUM_STATUS_OK);
        assert(memcmp(g_dst, g_src, sizeof(g_src)) == 0);
        ballum_interp_destroy(it);

        it = begin(BALLUM_INTERP_ALTERATION, &el);
        c.point = BALLUM_CONTACT_BRIGHTNESS; c.value = 100;
        assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_APPLIED);
        assert(ballum_interp_end_frame(it, g_dst) == BALLUM_STATUS_OK);
        assert(ink_pixels(g_dst) == 0);                                   /* +100 brightness: white */
        ballum_interp_destroy(it);

        it = begin(BALLUM_INTERP_ALTERATION, &el);
        c.point = BALLUM_CONTACT_EFFECT; c.value = 1;
        assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_APPLIED);
        assert(ballum_interp_end_frame(it, g_dst) == BALLUM_STATUS_OK);
        assert(same(g_dst[(size_t)15 * W + 15], WHITE) && same(g_dst[(size_t)9 * W + 9], BLACK));  /* invert */
        ballum_interp_destroy(it);

        it = begin(BALLUM_INTERP_ALTERATION, &el);
        c.point = BALLUM_CONTACT_EFFECT; c.value = 3;                           /* sepia of white stays light */
        assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_APPLIED);
        c.point = BALLUM_CONTACT_UNDERLINE; c.value = 2; c.color = RED;
        assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_APPLIED);
        c.point = BALLUM_CONTACT_STRIKETHROUGH; c.value = 1;
        assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_APPLIED);
        assert(ballum_interp_end_frame(it, g_dst) == BALLUM_STATUS_OK);
        assert(same(g_dst[(size_t)23 * W + 10], RED) && same(g_dst[(size_t)22 * W + 10], RED));   /* last two rows */
        assert(same(g_dst[(size_t)16 * W + 20], RED));                                              /* middle row   */
        ballum_interp_destroy(it);

        it = begin(BALLUM_INTERP_ALTERATION, &el);
        c.point = BALLUM_CONTACT_BLUR; c.value = 2;
        assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_APPLIED);
        assert(ballum_interp_end_frame(it, g_dst) == BALLUM_STATUS_OK);
        ballum_color_t m = g_dst[(size_t)14 * W + 11];                          /* just outside the ring: now soft */
        assert(m.r > 0 && m.r < 255);
        ballum_interp_destroy(it);
    }
    /* out-of-range values are refused and draw nothing */
    {
        struct { ballum_contact_t p; int v; } bad[] = {
            { BALLUM_CONTACT_OPACITY, 101 }, { BALLUM_CONTACT_OPACITY, -1 }, { BALLUM_CONTACT_BRIGHTNESS, 101 },
            { BALLUM_CONTACT_CONTRAST, -101 }, { BALLUM_CONTACT_SATURATION, 201 }, { BALLUM_CONTACT_VISIBILITY, 2 },
            { BALLUM_CONTACT_EFFECT, 4 }, { BALLUM_CONTACT_BLUR, 0 }, { BALLUM_CONTACT_BLUR, 9 },
            { BALLUM_CONTACT_UNDERLINE, 0 }, { BALLUM_CONTACT_STRIKETHROUGH, 5 }, { BALLUM_CONTACT_WEIGHT, 4 },
            { BALLUM_CONTACT_OUTLINE, 0 }, { BALLUM_CONTACT_OUTLINE, 4 }, { BALLUM_CONTACT_POSITION_X, 17 },
            { BALLUM_CONTACT_POSITION_Y, -17 }, { BALLUM_CONTACT_SLANT, 101 }, { BALLUM_CONTACT_SIZE, 24 },
            { BALLUM_CONTACT_SIZE, 401 }, { BALLUM_CONTACT_SPACING, -5 }, { BALLUM_CONTACT_SPACING, 33 },
            { BALLUM_CONTACT_LINE_HEIGHT, -9 }, { BALLUM_CONTACT_LINE_HEIGHT, 65 }, { BALLUM_CONTACT_Z_ORDER, 256 },
            { BALLUM_CONTACT_Z_ORDER, -1 }, { BALLUM_CONTACT_FONT_FAMILY, 16 },
        };
        for (size_t k = 0; k < sizeof(bad) / sizeof(bad[0]); ++k) {
            ballum_interp_t *it = begin(BALLUM_INTERP_ALTERATION, &el);
            ballum_change_t c = { bad[k].p, bad[k].v, RED };
            assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_INVALID);
            assert(ballum_interp_end_frame(it, g_dst) == BALLUM_STATUS_OK);
            assert(memcmp(g_dst, g_src, sizeof(g_src)) == 0);
            ballum_interp_destroy(it);
        }
        ballum_interp_t *it = begin(BALLUM_INTERP_ALTERATION, &el);
        ballum_change_t c = { (ballum_contact_t)40, 0, RED };
        assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_INVALID);
        c.point = BALLUM_CONTACT_COLOR_FG;
        assert(ballum_interp_apply(it, 9, &c, &ok) == BALLUM_ALTER_INVALID);       /* no such element */
        assert(ballum_interp_apply(it, 0, &c, NULL) == BALLUM_ALTER_INVALID);      /* scores are required */
        assert(ballum_interp_apply(it, 0, NULL, &ok) == BALLUM_ALTER_INVALID);
        assert(ballum_interp_apply(NULL, 0, &c, &ok) == BALLUM_ALTER_INVALID);
        /* ink-dependent points on an element with no known paper are unsupported, not guessed */
        ballum_element_t img = el; img.kind = BALLUM_ELEM_IMAGE; img.text = NULL; img.text_len = 0;
        assert(ballum_interp_submit(it, &img) == BALLUM_STATUS_OK);
        assert(ballum_interp_apply(it, 1, &c, &ok) == BALLUM_ALTER_UNSUPPORTED);
        c.point = BALLUM_CONTACT_WEIGHT; c.value = 1;
        assert(ballum_interp_apply(it, 1, &c, &ok) == BALLUM_ALTER_UNSUPPORTED);
        ballum_element_t clear = el; clear.bg.a = 0;
        assert(ballum_interp_submit(it, &clear) == BALLUM_STATUS_OK);
        c.point = BALLUM_CONTACT_OPACITY; c.value = 50;
        assert(ballum_interp_apply(it, 2, &c, &ok) == BALLUM_ALTER_UNSUPPORTED);
        assert(ballum_interp_end_frame(it, g_dst) == BALLUM_STATUS_OK);
        assert(memcmp(g_dst, g_src, sizeof(g_src)) == 0);
        ballum_interp_destroy(it);
    }
    /* a FILL element takes COLOR_FG as a fill */
    {
        ballum_element_t fill; memset(&fill, 0, sizeof(fill));
        fill.kind = BALLUM_ELEM_FILL; fill.rect = R(30, 4, 8, 8); fill.fg = WHITE;
        ballum_interp_t *it = begin(BALLUM_INTERP_ALTERATION, &fill);
        ballum_change_t c = { BALLUM_CONTACT_COLOR_FG, 0, RED };
        assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_APPLIED);
        assert(ballum_interp_end_frame(it, g_dst) == BALLUM_STATUS_OK);
        assert(same(g_dst[(size_t)6 * W + 32], RED) && outside_unchanged(fill.rect));
        ballum_interp_destroy(it);
    }

    /* ---- the filter band ------------------------------------------------------ */
    {
        ballum_filter_band_t b; ballum_filter_band_default(&b);
        assert(b.axis[BALLUM_FILTER_MORALS].low == 0 && b.axis[BALLUM_FILTER_MORALS].high == 1000);
        assert(b.axis[BALLUM_FILTER_CONFIDENCE].low == 500);
        assert(strcmp(ballum_filter_axis_name(BALLUM_FILTER_MORALS), "morals") == 0);
        assert(strcmp(ballum_filter_axis_name(BALLUM_FILTER_CLEANLINESS), "cleanliness") == 0);
        assert(strcmp(ballum_filter_axis_name(BALLUM_FILTER_THROUGHPUT), "throughput") == 0);
        assert(strcmp(ballum_filter_axis_name(BALLUM_FILTER_CONFIDENCE), "confidence") == 0);

        ballum_interp_t *it = begin(BALLUM_INTERP_ALTERATION, &el);
        ballum_change_t c = { BALLUM_CONTACT_COLOR_FG, 0, RED };
        ballum_filter_scores_t lowc = scores(900, 900, 900, 400);              /* below the default confidence floor */
        assert(ballum_interp_apply(it, 0, &c, &lowc) == BALLUM_ALTER_BLOCKED);
        ballum_filter_scores_t bad = scores(900, 1001, 900, 900);
        assert(ballum_interp_apply(it, 0, &c, &bad) == BALLUM_ALTER_INVALID);   /* not a pass, an error */
        bad = scores(-1, 900, 900, 900);
        assert(ballum_interp_apply(it, 0, &c, &bad) == BALLUM_ALTER_INVALID);

        for (int axis = 0; axis < 4; ++axis) {                                  /* each axis can block on its own */
            ballum_filter_band_t nb; ballum_filter_band_default(&nb);
            nb.axis[axis].low = 600; nb.axis[axis].high = 700;
            ballum_interp_set_band(it, &nb);
            ballum_filter_scores_t s = good();                                  /* 900: above the window */
            assert(ballum_interp_apply(it, 0, &c, &s) == BALLUM_ALTER_BLOCKED);
            s.score[axis] = 650;                                                /* inside */
            assert(ballum_interp_apply(it, 0, &c, &s) == BALLUM_ALTER_APPLIED);
            s.score[axis] = 599;                                                /* just below */
            assert(ballum_interp_apply(it, 0, &c, &s) == BALLUM_ALTER_BLOCKED);
            s.score[axis] = 700;                                                /* window edge is inclusive */
            assert(ballum_interp_apply(it, 0, &c, &s) == BALLUM_ALTER_APPLIED);
        }
        ballum_filter_band_t contradict; ballum_filter_band_default(&contradict);
        contradict.axis[BALLUM_FILTER_MORALS].low = 800; contradict.axis[BALLUM_FILTER_MORALS].high = 100;
        ballum_interp_set_band(it, &contradict);
        for (int v = 0; v <= 1000; v += 100) {                                  /* a contradictory window passes nothing */
            ballum_filter_scores_t s = scores(v, 900, 900, 900);
            assert(ballum_interp_apply(it, 0, &c, &s) == BALLUM_ALTER_BLOCKED);
        }
        ballum_filter_band_t wild; ballum_filter_band_default(&wild);
        wild.axis[0].low = -50; wild.axis[0].high = 5000;                       /* clamped into range */
        ballum_interp_set_band(it, &wild);
        ballum_interp_get_band(it, &b);
        assert(b.axis[0].low == 0 && b.axis[0].high == 1000);
        ballum_interp_destroy(it);

        /* a blocked alteration leaves the frame exactly as the program drew it */
        it = begin(BALLUM_INTERP_ALTERATION, &el);
        assert(ballum_interp_apply(it, 0, &c, &lowc) == BALLUM_ALTER_BLOCKED);
        assert(ballum_interp_end_frame(it, g_dst) == BALLUM_STATUS_OK);
        assert(memcmp(g_dst, g_src, sizeof(g_src)) == 0);
        ballum_interp_stats_t st; ballum_interp_stats(it, &st);
        assert(st.alterations_blocked == 1 && st.alterations_applied == 0 && st.pixels_altered == 0);
        ballum_interp_destroy(it);
    }

    /* ---- recolor_text and raw alter -------------------------------------------- */
    {
        ballum_interp_t *it = begin(BALLUM_INTERP_ALTERATION, &el);
        ballum_element_t other = text_elem("B", 1); other.rect = R(40, 8, 8, 8);
        ballum_element_t guess = text_elem("A", 0); guess.rect = R(40, 20, 8, 8);     /* same text, not certain */
        assert(ballum_interp_submit(it, &other) == BALLUM_STATUS_OK);
        assert(ballum_interp_submit(it, &guess) == BALLUM_STATUS_OK);
        assert(ballum_interp_recolor_text(it, "A", RED, &ok) == 1);                     /* only the certain match */
        assert(ballum_interp_recolor_text(it, "Z", RED, &ok) == 0);
        assert(ballum_interp_recolor_text(it, NULL, RED, &ok) == 0);
        ballum_filter_scores_t low = scores(900, 900, 900, 100);
        assert(ballum_interp_recolor_text(it, "A", RED, &low) == 0);                    /* the band gates it */
        ballum_color_t rep[16 * 16]; for (int i = 0; i < 256; ++i) rep[i] = RED;
        assert(ballum_interp_alter(it, 0, rep, 255, &ok) == BALLUM_ALTER_INVALID);      /* wrong pixel count */
        assert(ballum_interp_alter(it, 0, NULL, 256, &ok) == BALLUM_ALTER_INVALID);
        assert(ballum_interp_alter(it, 0, rep, 256, &ok) == BALLUM_ALTER_APPLIED);
        assert(ballum_interp_end_frame(it, g_dst) == BALLUM_STATUS_OK);
        assert(same(g_dst[(size_t)10 * W + 10], RED));
        ballum_interp_destroy(it);
    }

    /* ---- crypto grade: digest and keyed chain ---------------------------------- */
    {
        static const uint8_t key[] = "ballum-audit-key";
        ballum_interp_t *it = begin(BALLUM_INTERP_ALTERATION, &el);
        ballum_change_t g = { BALLUM_CONTACT_CRYPTO_GRADE, BALLUM_CRYPTO_CHAIN, BLACK };
        assert(ballum_interp_apply(it, 0, &g, &ok) == BALLUM_ALTER_INVALID);            /* chain needs a key first */
        assert(ballum_interp_set_audit_key(it, key, sizeof(key) - 1) == BALLUM_STATUS_OK);
        assert(ballum_interp_set_audit_key(it, key, 0) == BALLUM_STATUS_INVALID_ARGUMENT);
        assert(ballum_interp_set_audit_key(it, key, 65) == BALLUM_STATUS_INVALID_ARGUMENT);
        g.value = 9; assert(ballum_interp_apply(it, 0, &g, &ok) == BALLUM_ALTER_INVALID);
        g.value = BALLUM_CRYPTO_CHAIN;
        ballum_filter_scores_t low = scores(900, 900, 900, 100);
        assert(ballum_interp_apply(it, 0, &g, &low) == BALLUM_ALTER_BLOCKED);           /* the band gates policy too */
        assert(ballum_interp_crypto_grade(it) == 0);
        assert(ballum_interp_apply(it, 0, &g, &ok) == BALLUM_ALTER_APPLIED);
        assert(ballum_interp_crypto_grade(it) == BALLUM_CRYPTO_CHAIN);

        ballum_change_t a = { BALLUM_CONTACT_COLOR_FG, 0, RED };
        assert(ballum_interp_apply(it, 0, &a, &ok) == BALLUM_ALTER_APPLIED);
        a.point = BALLUM_CONTACT_UNDERLINE; a.value = 1; a.color = BLACK;
        assert(ballum_interp_apply(it, 0, &a, &ok) == BALLUM_ALTER_APPLIED);
        assert(ballum_interp_audit_count(it) == 2);

        ballum_audit_entry_t e1, e2;
        assert(ballum_interp_audit_at(it, 0, &e1) && ballum_interp_audit_at(it, 1, &e2));
        assert(!ballum_interp_audit_at(it, 2, &e1));
        assert(e1.seq == 1 && e2.seq == 2 && e1.point == (int)BALLUM_CONTACT_COLOR_FG && e2.point == (int)BALLUM_CONTACT_UNDERLINE);
        assert(e1.rect.x == 8 && e1.rect.width == 16 && e1.element == 0 && e1.frame == 1);

        /* the digest of the last entry is the SHA-256 of the element pixels as they now are */
        assert(ballum_interp_end_frame(it, g_dst) == BALLUM_STATUS_OK);
        uint8_t bytes[16 * 16 * 4], want[32];
        for (int y = 0; y < 16; ++y) memcpy(bytes + y * 16 * 4, &g_dst[(size_t)(8 + y) * W + 8], 16 * 4);
        slc_sha256(bytes, sizeof(bytes), want);
        assert(memcmp(want, e2.digest, 32) == 0);
        assert(memcmp(e1.digest, e2.digest, 32) != 0);

        uint8_t zero[32] = { 0 };
        assert(memcmp(e1.mac, zero, 32) != 0 && memcmp(e1.mac, e2.mac, 32) != 0);
        assert(ballum_interp_audit_verify(it, key, sizeof(key) - 1) == 1);               /* chain intact */
        static const uint8_t wrong[] = "not-the-key";
        assert(ballum_interp_audit_verify(it, wrong, sizeof(wrong) - 1) == 0);           /* wrong key fails */
        assert(ballum_interp_audit_verify(it, NULL, 0) == 0);
        {   uint8_t h1[32], h2[32];
            assert(ballum_interp_audit_head(it, h1) == 1 && ballum_interp_audit_head(it, h2) == 1);
            assert(memcmp(h1, h2, 32) == 0);                       /* stable */
            assert(ballum_interp_audit_head(NULL, h1) == 0 && ballum_interp_audit_head(it, NULL) == 0);
        }
        g.value = BALLUM_CRYPTO_DIGEST;
        assert(ballum_interp_apply(it, 0, &g, &ok) == BALLUM_ALTER_INVALID);             /* one grade per log */
        ballum_interp_destroy(it);

        /* grade 1: digests, no MAC, no chain to verify */
        it = begin(BALLUM_INTERP_ALTERATION, &el);
        g.value = BALLUM_CRYPTO_DIGEST;
        assert(ballum_interp_apply(it, 0, &g, &ok) == BALLUM_ALTER_APPLIED);
        a.point = BALLUM_CONTACT_BRIGHTNESS; a.value = -10;
        assert(ballum_interp_apply(it, 0, &a, &ok) == BALLUM_ALTER_APPLIED);
        assert(ballum_interp_audit_at(it, 0, &e1));
        assert(memcmp(e1.mac, zero, 32) == 0 && memcmp(e1.digest, zero, 32) != 0);
        assert(ballum_interp_set_audit_key(it, key, sizeof(key) - 1) == BALLUM_STATUS_OK);
        assert(ballum_interp_audit_verify(it, key, sizeof(key) - 1) == 0);
        ballum_interp_destroy(it);

        /* grade 0 records nothing */
        it = begin(BALLUM_INTERP_ALTERATION, &el);
        a.point = BALLUM_CONTACT_BRIGHTNESS;
        assert(ballum_interp_apply(it, 0, &a, &ok) == BALLUM_ALTER_APPLIED);
        assert(ballum_interp_audit_count(it) == 0);
        ballum_interp_destroy(it);

        /* a full log refuses further alterations instead of applying them unrecorded */
        it = begin(BALLUM_INTERP_ALTERATION, &el);
        g.value = BALLUM_CRYPTO_DIGEST;
        assert(ballum_interp_apply(it, 0, &g, &ok) == BALLUM_ALTER_APPLIED);
        a.point = BALLUM_CONTACT_EFFECT; a.value = 0;
        for (int i = 0; i < 4096; ++i) assert(ballum_interp_apply(it, 0, &a, &ok) == BALLUM_ALTER_APPLIED);
        assert(ballum_interp_audit_count(it) == 4096);
        a.point = BALLUM_CONTACT_UNDERLINE; a.value = 2; a.color = RED;
        assert(ballum_interp_apply(it, 0, &a, &ok) == BALLUM_ALTER_AUDIT_FULL);
        assert(ballum_interp_end_frame(it, g_dst) == BALLUM_STATUS_OK);
        assert(memcmp(g_dst, g_src, sizeof(g_src)) == 0);                                /* the refused one drew nothing */
        ballum_interp_destroy(it);
    }

    /* ---- the ten points that were declared: now built out ---------------------- */
    {
        ballum_change_t c; ballum_interp_t *it;

        /* SIZE: 100% is the identity, 200% grows the ink about the centre, 50% shrinks it */
        it = begin(BALLUM_INTERP_ALTERATION, &el);
        c = (ballum_change_t){ BALLUM_CONTACT_SIZE, 100, BLACK };
        assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_APPLIED);
        assert(ballum_interp_end_frame(it, g_dst) == BALLUM_STATUS_OK && memcmp(g_dst, g_src, sizeof(g_src)) == 0);
        ballum_interp_destroy(it);
        it = begin(BALLUM_INTERP_ALTERATION, &el);
        c.value = 200;
        assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_APPLIED);
        assert(ballum_interp_end_frame(it, g_dst) == BALLUM_STATUS_OK);
        assert(ink_pixels(g_dst) > ink_pixels(g_src) && outside_unchanged(el.rect));
        assert(same(g_dst[(size_t)16 * W + 16], BLACK));
        ballum_interp_destroy(it);
        it = begin(BALLUM_INTERP_ALTERATION, &el);
        c.value = 50;
        assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_APPLIED);
        assert(ballum_interp_end_frame(it, g_dst) == BALLUM_STATUS_OK);
        assert(ink_pixels(g_dst) < ink_pixels(g_src) && outside_unchanged(el.rect));
        ballum_interp_destroy(it);

        /* SLANT: 0 is the identity; a shear moves the upper rows right and keeps the baseline */
        it = begin(BALLUM_INTERP_ALTERATION, &el);
        c = (ballum_change_t){ BALLUM_CONTACT_SLANT, 0, BLACK };
        assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_APPLIED);
        assert(ballum_interp_end_frame(it, g_dst) == BALLUM_STATUS_OK && memcmp(g_dst, g_src, sizeof(g_src)) == 0);
        ballum_interp_destroy(it);
        it = begin(BALLUM_INTERP_ALTERATION, &el);
        c.value = 100;
        assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_APPLIED);
        assert(ballum_interp_end_frame(it, g_dst) == BALLUM_STATUS_OK);
        assert(same(g_dst[(size_t)18 * W + 20], BLACK));            /* local (12,10): shifted by 5 */
        assert(same(g_dst[(size_t)18 * W + 15], WHITE));            /* local (7,10): the ink left it */
        assert(outside_unchanged(el.rect));
        ballum_interp_destroy(it);

        /* POSITION_X / POSITION_Y move the content inside the rectangle */
        it = begin(BALLUM_INTERP_ALTERATION, &el);
        c = (ballum_change_t){ BALLUM_CONTACT_POSITION_X, 3, BLACK };
        assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_APPLIED);
        c = (ballum_change_t){ BALLUM_CONTACT_POSITION_Y, 2, BLACK };
        assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_APPLIED);
        assert(ballum_interp_end_frame(it, g_dst) == BALLUM_STATUS_OK);
        assert(same(g_dst[(size_t)(13 + 2) * W + 13 + 3], BLACK));  /* ink moved +3,+2 */
        assert(same(g_dst[(size_t)13 * W + 13], WHITE));            /* where it was */
        assert(ink_pixels(g_dst) == ink_pixels(g_src) && outside_unchanged(el.rect));
        ballum_interp_destroy(it);
        it = begin(BALLUM_INTERP_ALTERATION, &el);
        c = (ballum_change_t){ BALLUM_CONTACT_POSITION_X, 16, BLACK };            /* all of it moves out and is clipped */
        assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_APPLIED);
        assert(ballum_interp_end_frame(it, g_dst) == BALLUM_STATUS_OK);
        assert(ink_pixels(g_dst) == 0 && outside_unchanged(el.rect));
        ballum_interp_destroy(it);

        /* OUTLINE: a ring around the ink; the ink is untouched */
        it = begin(BALLUM_INTERP_ALTERATION, &el);
        c = (ballum_change_t){ BALLUM_CONTACT_OUTLINE, 1, RED };
        assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_APPLIED);
        assert(ballum_interp_end_frame(it, g_dst) == BALLUM_STATUS_OK);
        assert(same(g_dst[(size_t)16 * W + 11], RED));              /* local (3,8): just outside the ring */
        assert(same(g_dst[(size_t)16 * W + 10], WHITE));            /* local (2,8): too far for thickness 1 */
        assert(same(g_dst[(size_t)15 * W + 15], BLACK));
        ballum_interp_destroy(it);
        it = begin(BALLUM_INTERP_ALTERATION, &el);
        c.value = 2;
        assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_APPLIED);
        assert(ballum_interp_end_frame(it, g_dst) == BALLUM_STATUS_OK && same(g_dst[(size_t)16 * W + 10], RED));
        ballum_interp_destroy(it);

        /* SPACING and LINE_HEIGHT cut at blank columns/rows and move the pieces */
        for (size_t i = 0; i < N; ++i) g_src[i] = WHITE;
        for (uint32_t y = 6; y < 12; ++y) { for (uint32_t x = 6; x < 10; ++x) g_src[(size_t)y * W + x] = BLACK;
                                            for (uint32_t x = 14; x < 18; ++x) g_src[(size_t)y * W + x] = BLACK; }
        ballum_element_t row = block_elem(R(4, 4, 40, 10), BALLUM_ELEM_TEXT);       /* A at local 2..5, B at 10..13 */
        it = begin(BALLUM_INTERP_ALTERATION, &row);
        c = (ballum_change_t){ BALLUM_CONTACT_SPACING, 5, BLACK };
        assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_APPLIED);
        assert(ballum_interp_end_frame(it, g_dst) == BALLUM_STATUS_OK);
        assert(same(g_dst[(size_t)8 * W + 6], BLACK) && same(g_dst[(size_t)8 * W + 9], BLACK));   /* A stays */
        assert(same(g_dst[(size_t)8 * W + 14], WHITE) && same(g_dst[(size_t)8 * W + 19], BLACK)); /* B: gap 4 + 5 */
        assert(outside_unchanged(row.rect));
        ballum_interp_destroy(it);
        it = begin(BALLUM_INTERP_ALTERATION, &row);
        c.value = -4;
        assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_APPLIED);
        assert(ballum_interp_end_frame(it, g_dst) == BALLUM_STATUS_OK);
        assert(same(g_dst[(size_t)8 * W + 10], BLACK) && same(g_dst[(size_t)8 * W + 13], BLACK));  /* touching, never overlapping */
        ballum_interp_destroy(it);
        for (size_t i = 0; i < N; ++i) g_src[i] = WHITE;
        for (uint32_t x = 6; x < 12; ++x) { for (uint32_t y = 6; y < 10; ++y) g_src[(size_t)y * W + x] = BLACK;
                                            for (uint32_t y = 14; y < 18; ++y) g_src[(size_t)y * W + x] = BLACK; }
        ballum_element_t col = block_elem(R(4, 4, 10, 26), BALLUM_ELEM_TEXT);
        it = begin(BALLUM_INTERP_ALTERATION, &col);
        c = (ballum_change_t){ BALLUM_CONTACT_LINE_HEIGHT, 6, BLACK };
        assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_APPLIED);
        assert(ballum_interp_end_frame(it, g_dst) == BALLUM_STATUS_OK);
        assert(same(g_dst[(size_t)8 * W + 8], BLACK));              /* first line stays */
        assert(same(g_dst[(size_t)14 * W + 8], WHITE) && same(g_dst[(size_t)20 * W + 8], BLACK));   /* second: gap 4 + 6 */
        ballum_interp_destroy(it);
        make_scene();

        /* TEXT and FONT_FAMILY through a glyph source, and they survive a z-order rebuild */
        int calls = 0;
        ballum_glyph_source_t gs = { box_glyph, &calls };
        ballum_element_t line = block_elem(R(4, 20, 40, 10), BALLUM_ELEM_TEXT);
        line.text = "AB"; line.text_len = 2; line.glyph_known = 1;
        ballum_element_t other = block_elem(R(50, 2, 8, 8), BALLUM_ELEM_FILL);
        it = begin(BALLUM_INTERP_ALTERATION, &line);
        assert(ballum_interp_submit(it, &other) == BALLUM_STATUS_OK);
        assert(ballum_interp_set_glyph_source(it, &gs) == BALLUM_STATUS_OK);
        assert(ballum_contact_available(it, BALLUM_CONTACT_TEXT) == 1 && ballum_contact_available(it, BALLUM_CONTACT_FONT_FAMILY) == 1);
        assert(ballum_interp_apply_text(it, 0, "AB C", 4, &ok) == BALLUM_ALTER_APPLIED);
        assert(calls >= 3);
        assert(same(g_src[(size_t)20 * W + 4], WHITE));
        c = (ballum_change_t){ BALLUM_CONTACT_FONT_FAMILY, 2, BLACK };
        assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_APPLIED);
        c = (ballum_change_t){ BALLUM_CONTACT_Z_ORDER, 9, BLACK };                   /* forces a rebuild */
        assert(ballum_interp_apply(it, 1, &c, &ok) == BALLUM_ALTER_APPLIED);
        assert(ballum_interp_end_frame(it, g_dst) == BALLUM_STATUS_OK);
        assert(same(g_dst[(size_t)20 * W + 4 + 7], BLACK));         /* family 2: the first glyph is 8 wide */
        assert(same(g_dst[(size_t)20 * W + 4 + 8], WHITE));         /* then the 1 px gap */
        assert(same(g_dst[(size_t)20 * W + 4 + 9], BLACK));         /* second glyph starts at 9 */
        assert(same(g_dst[(size_t)26 * W + 4], WHITE));             /* glyphs are half the line tall */
        ballum_interp_destroy(it);

        /* an element whose characters are unknown cannot change family; bad UTF-8 is survivable */
        ballum_element_t mute = block_elem(R(4, 20, 40, 10), BALLUM_ELEM_TEXT);
        it = begin(BALLUM_INTERP_ALTERATION, &mute);
        ballum_interp_set_glyph_source(it, &gs);
        c = (ballum_change_t){ BALLUM_CONTACT_FONT_FAMILY, 1, BLACK };
        assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_UNSUPPORTED);
        assert(ballum_interp_apply_text(it, 0, "\xff\xc3\x28\xed\xa0\x80ok", 9, &ok) == BALLUM_ALTER_APPLIED);
        assert(outside_unchanged(mute.rect));
        ballum_interp_set_glyph_source(it, NULL);                                    /* removing it makes TEXT unavailable again */
        assert(ballum_contact_available(it, BALLUM_CONTACT_TEXT) == 0);
        ballum_interp_destroy(it);

        /* Z_ORDER decides who is on top where two altered elements overlap */
        ballum_element_t za = block_elem(R(4, 4, 10, 10), BALLUM_ELEM_FILL);
        ballum_element_t zb = block_elem(R(9, 9, 10, 10), BALLUM_ELEM_FILL);
        ballum_color_t BLUE = { 0, 0, 255, 255 };
        it = begin(BALLUM_INTERP_ALTERATION, &za);
        assert(ballum_interp_submit(it, &zb) == BALLUM_STATUS_OK);
        c = (ballum_change_t){ BALLUM_CONTACT_COLOR_FG, 0, RED };
        assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_APPLIED);
        c.color = BLUE;
        assert(ballum_interp_apply(it, 1, &c, &ok) == BALLUM_ALTER_APPLIED);        /* made later: on top */
        assert(ballum_interp_end_frame(it, g_dst) == BALLUM_STATUS_OK);
        assert(same(g_dst[(size_t)10 * W + 10], BLUE) && same(g_dst[(size_t)5 * W + 5], RED) && same(g_dst[(size_t)15 * W + 15], BLUE));
        ballum_interp_destroy(it);
        it = begin(BALLUM_INTERP_ALTERATION, &za);
        assert(ballum_interp_submit(it, &zb) == BALLUM_STATUS_OK);
        c = (ballum_change_t){ BALLUM_CONTACT_COLOR_FG, 0, RED };
        assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_APPLIED);
        c.color = BLUE;
        assert(ballum_interp_apply(it, 1, &c, &ok) == BALLUM_ALTER_APPLIED);
        c = (ballum_change_t){ BALLUM_CONTACT_Z_ORDER, 5, BLACK };
        assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_APPLIED);        /* A raised above B */
        assert(ballum_interp_end_frame(it, g_dst) == BALLUM_STATUS_OK);
        assert(same(g_dst[(size_t)10 * W + 10], RED) && same(g_dst[(size_t)5 * W + 5], RED) && same(g_dst[(size_t)15 * W + 15], BLUE));
        ballum_interp_destroy(it);
        /* a z that changes nothing about the order rebuilds to exactly the same frame */
        {
            static ballum_color_t plain[N];
            it = begin(BALLUM_INTERP_ALTERATION, &el);
            ballum_change_t a1 = { BALLUM_CONTACT_BRIGHTNESS, -30, BLACK }, a2 = { BALLUM_CONTACT_BLUR, 2, BLACK },
                            a3 = { BALLUM_CONTACT_COLOR_FG, 0, RED }, a4 = { BALLUM_CONTACT_WEIGHT, 1, BLACK },
                            zz = { BALLUM_CONTACT_Z_ORDER, 0, BLACK };
            assert(ballum_interp_apply(it, 0, &a1, &ok) == BALLUM_ALTER_APPLIED);
            assert(ballum_interp_apply(it, 0, &a2, &ok) == BALLUM_ALTER_APPLIED);
            assert(ballum_interp_apply(it, 0, &a3, &ok) == BALLUM_ALTER_APPLIED);
            assert(ballum_interp_apply(it, 0, &a4, &ok) == BALLUM_ALTER_APPLIED);
            assert(ballum_interp_end_frame(it, plain) == BALLUM_STATUS_OK);
            ballum_interp_destroy(it);
            it = begin(BALLUM_INTERP_ALTERATION, &el);
            assert(ballum_interp_apply(it, 0, &a1, &ok) == BALLUM_ALTER_APPLIED);
            assert(ballum_interp_apply(it, 0, &a2, &ok) == BALLUM_ALTER_APPLIED);
            assert(ballum_interp_apply(it, 0, &zz, &ok) == BALLUM_ALTER_APPLIED);   /* rebuild in the middle */
            assert(ballum_interp_apply(it, 0, &a3, &ok) == BALLUM_ALTER_APPLIED);
            assert(ballum_interp_apply(it, 0, &a4, &ok) == BALLUM_ALTER_APPLIED);
            assert(ballum_interp_apply(it, 0, &zz, &ok) == BALLUM_ALTER_APPLIED);
            assert(ballum_interp_end_frame(it, g_dst) == BALLUM_STATUS_OK);
            assert(memcmp(plain, g_dst, sizeof(plain)) == 0);
            ballum_interp_destroy(it);
        }
    }

    /* ---- confidence of the interface ------------------------------------------ */
    {
        int lo = 1000, hi = 0;
        for (int i = 0; i < 23; ++i) {
            int cf = ballum_contact_confidence((ballum_contact_t)i);
            assert(cf >= 0 && cf <= 1000);
            if (cf < lo) lo = cf;
            if (cf > hi) hi = cf;
        }
        assert(lo == 600 && hi == 1000);
        assert(ballum_contact_confidence(BALLUM_CONTACT_COLOR_FG) == 1000);
        assert(ballum_contact_confidence(BALLUM_CONTACT_SIZE) == 800);
        assert(ballum_contact_confidence(BALLUM_CONTACT_SPACING) == 700);
        assert(ballum_contact_confidence(BALLUM_CONTACT_TEXT) == 600);
        assert(ballum_contact_confidence((ballum_contact_t)99) == -1);

        ballum_filter_scores_t base = scores(900, 900, 900, 900);
        assert(ballum_filter_scores_capped(&base, BALLUM_CONTACT_TEXT).score[BALLUM_FILTER_CONFIDENCE] == 600);
        assert(ballum_filter_scores_capped(&base, BALLUM_CONTACT_COLOR_FG).score[BALLUM_FILTER_CONFIDENCE] == 900);   /* never raised */
        assert(ballum_filter_scores_capped(&base, BALLUM_CONTACT_TEXT).score[BALLUM_FILTER_MORALS] == 900);            /* other axes untouched */
        ballum_filter_scores_t zero = ballum_filter_scores_capped(NULL, BALLUM_CONTACT_TEXT);
        assert(zero.score[0] == 0 && zero.score[3] == 0);

        /* a band with a Confidence floor of 700 refuses the lower-rated points on its own */
        ballum_filter_band_t b; ballum_filter_band_default(&b);
        b.axis[BALLUM_FILTER_CONFIDENCE].low = 700;
        ballum_interp_t *it = begin(BALLUM_INTERP_ALTERATION, &el);
        ballum_interp_set_band(it, &b);
        ballum_filter_scores_t for_text = ballum_filter_scores_capped(&base, BALLUM_CONTACT_TEXT);
        ballum_filter_scores_t for_color = ballum_filter_scores_capped(&base, BALLUM_CONTACT_COLOR_FG);
        ballum_filter_scores_t for_spacing = ballum_filter_scores_capped(&base, BALLUM_CONTACT_SPACING);
        ballum_glyph_source_t gs = { box_glyph, NULL };
        ballum_interp_set_glyph_source(it, &gs);
        assert(ballum_interp_apply_text(it, 0, "A", 1, &for_text) == BALLUM_ALTER_BLOCKED);       /* 600 < 700 */
        ballum_change_t c = { BALLUM_CONTACT_COLOR_FG, 0, RED };
        assert(ballum_interp_apply(it, 0, &c, &for_color) == BALLUM_ALTER_APPLIED);                /* 900 */
        c = (ballum_change_t){ BALLUM_CONTACT_SPACING, 3, BLACK };
        assert(ballum_interp_apply(it, 0, &c, &for_spacing) == BALLUM_ALTER_APPLIED);              /* 700 is inside the floor */
        ballum_interp_destroy(it);
    }

    /* ---- a frame can hold only so many alterations --------------------------- */
    {
        ballum_interp_t *it = begin(BALLUM_INTERP_ALTERATION, &el);
        ballum_change_t c = { BALLUM_CONTACT_EFFECT, 0, BLACK };
        for (int i = 0; i < 4096; ++i) assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_APPLIED);
        c = (ballum_change_t){ BALLUM_CONTACT_UNDERLINE, 2, RED };
        assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_FRAME_FULL);
        assert(ballum_interp_end_frame(it, g_dst) == BALLUM_STATUS_OK);
        assert(memcmp(g_dst, g_src, sizeof(g_src)) == 0);                       /* the refused one drew nothing */
        assert(ballum_interp_begin_frame(it, g_src) == BALLUM_STATUS_OK);       /* a new frame starts empty */
        assert(ballum_interp_submit(it, &el) == BALLUM_STATUS_OK);
        assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_APPLIED);
        ballum_interp_destroy(it);
    }

    /* ---- reading colours off the frame ------------------------------------------------ */
    {
        make_scene();                                          /* white page, grey ring, black square at local (5..10) */
        ballum_element_t u = el; u.bg.a = 0; u.fg.a = 0; u.text = NULL; u.text_len = 0;
        assert(ballum_element_infer_colors(g_src, W, H, &u) == 2);
        assert(same(u.bg, WHITE) && u.bg.a == 255 && same(u.fg, BLACK) && u.fg.a == 255);
        /* known colours are never overwritten */
        ballum_element_t k = el; k.bg = RED; k.fg = RED;
        assert(ballum_element_infer_colors(g_src, W, H, &k) == 0 && same(k.bg, RED) && same(k.fg, RED));
        ballum_element_t half = el; half.fg.a = 0;                                           /* paper known, ink not */
        assert(ballum_element_infer_colors(g_src, W, H, &half) == 1 && same(half.fg, BLACK));
        /* a run of nothing but paper has no ink to find */
        ballum_element_t blank = el; blank.bg.a = 0; blank.fg.a = 0; blank.rect = R(40, 4, 10, 10);
        assert(ballum_element_infer_colors(g_src, W, H, &blank) == 1 && blank.fg.a == 0);
        /* at the edge of the frame the ring is read from inside */
        ballum_element_t edge = el; edge.bg.a = 0; edge.fg.a = 0; edge.rect = R(0, 0, 64, 32);
        assert(ballum_element_infer_colors(g_src, W, H, &edge) == 2 && same(edge.bg, WHITE));
        /* a paper that is not the clear majority of the ring stays unknown */
        for (size_t i = 0; i < N; ++i) g_src[i] = (i & 1) ? WHITE : RED;
        ballum_element_t mixed = el; mixed.bg.a = 0; mixed.fg.a = 0;
        assert(ballum_element_infer_colors(g_src, W, H, &mixed) == 0 && mixed.bg.a == 0);
        /* bad arguments */
        assert(ballum_element_infer_colors(NULL, W, H, &u) == 0 && ballum_element_infer_colors(g_src, W, H, NULL) == 0);
        ballum_element_t out = el; out.bg.a = 0; out.rect = R(60, 0, 16, 16);
        assert(ballum_element_infer_colors(g_src, W, H, &out) == 0);
        /* ink-dependent points refuse an element whose ink is unknown */
        make_scene();
        ballum_interp_t *it = ballum_interp_create(BALLUM_INTERP_ALTERATION, W, H, NULL);
        assert(ballum_interp_begin_frame(it, g_src) == BALLUM_STATUS_OK);
        ballum_element_t noink = el; noink.fg.a = 0;
        assert(ballum_interp_submit(it, &noink) == BALLUM_STATUS_OK);
        ballum_change_t c = { BALLUM_CONTACT_COLOR_FG, 0, RED };
        assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_UNSUPPORTED);
        ballum_interp_destroy(it);
        make_scene();
    }

    /* ---- input follows the picture ------------------------------------------------------- */
    {
        make_scene();
        ballum_interp_t *it = begin(BALLUM_INTERP_ALTERATION, &el);
        int32_t mx, my;
        assert(ballum_interp_map_point(it, 18, 17, &mx, &my) == 0 && mx == 18 && my == 17);      /* nothing moved yet */
        ballum_change_t c = { BALLUM_CONTACT_POSITION_X, 3, BLACK };
        assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_APPLIED);
        c = (ballum_change_t){ BALLUM_CONTACT_POSITION_Y, 2, BLACK };
        assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_APPLIED);
        assert(ballum_interp_map_point(it, 18, 17, &mx, &my) == 1 && mx == 15 && my == 15);      /* shown +3,+2 -> program */
        assert(ballum_interp_map_point(it, 40, 20, &mx, &my) == 0 && mx == 40 && my == 20);      /* outside every moved element */
        assert(ballum_interp_map_point(it, 8, 8, &mx, &my) == 1 && mx == 8 && my == 8);          /* exposed paper: clamped to the edge */
        assert(ballum_interp_map_point(NULL, 1, 1, &mx, &my) == 0 && ballum_interp_map_point(it, 1, 1, NULL, &my) == 0);
        ballum_interp_destroy(it);

        /* size: about the centre, so the centre stays and distances halve at 200% */
        it = begin(BALLUM_INTERP_ALTERATION, &el);
        c = (ballum_change_t){ BALLUM_CONTACT_SIZE, 200, BLACK };
        assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_APPLIED);
        assert(ballum_interp_map_point(it, 16, 16, &mx, &my) == 1 && mx >= 15 && mx <= 16 && my >= 15 && my <= 16);
        assert(ballum_interp_map_point(it, 22, 16, &mx, &my) == 1 && mx >= 18 && mx <= 20 && my >= 15 && my <= 16);   /* 6 px right -> 3 */
        ballum_interp_destroy(it);

        /* slant, then a move: undone in reverse order */
        it = begin(BALLUM_INTERP_ALTERATION, &el);
        c = (ballum_change_t){ BALLUM_CONTACT_SLANT, 100, BLACK };
        assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_APPLIED);
        assert(ballum_interp_map_point(it, 20, 18, &mx, &my) == 1 && mx == 15 && my == 18);      /* local (12,10): shear 5 */
        c = (ballum_change_t){ BALLUM_CONTACT_POSITION_X, 2, BLACK };
        assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_APPLIED);
        assert(ballum_interp_map_point(it, 22, 18, &mx, &my) == 1 && mx == 15 && my == 18);      /* the move first, then the shear */
        ballum_interp_destroy(it);

        /* points that move nothing are left alone: a colour point, spacing, blur */
        it = begin(BALLUM_INTERP_ALTERATION, &el);
        c = (ballum_change_t){ BALLUM_CONTACT_COLOR_FG, 0, RED };      assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_APPLIED);
        c = (ballum_change_t){ BALLUM_CONTACT_BLUR, 2, BLACK };        assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_APPLIED);
        assert(ballum_interp_map_point(it, 18, 17, &mx, &my) == 0 && mx == 18 && my == 17);
        ballum_interp_destroy(it);

        /* where two moved elements overlap, the one on top owns the point */
        ballum_element_t top = el; top.rect = R(12, 12, 16, 16);
        it = begin(BALLUM_INTERP_ALTERATION, &el);
        assert(ballum_interp_submit(it, &top) == BALLUM_STATUS_OK);
        c = (ballum_change_t){ BALLUM_CONTACT_POSITION_X, 1, BLACK };
        assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_APPLIED);
        c = (ballum_change_t){ BALLUM_CONTACT_POSITION_X, 4, BLACK };
        assert(ballum_interp_apply(it, 1, &c, &ok) == BALLUM_ALTER_APPLIED);
        assert(ballum_interp_map_point(it, 20, 20, &mx, &my) == 1 && mx == 16);                  /* the later element: 20 - 4 */
        c = (ballum_change_t){ BALLUM_CONTACT_Z_ORDER, 9, BLACK };
        assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_APPLIED);                      /* raise the first */
        assert(ballum_interp_map_point(it, 20, 20, &mx, &my) == 1 && mx == 19);                  /* now 20 - 1 */
        ballum_interp_destroy(it);

        /* NOTATION never moves anything, so it never maps */
        it = begin(BALLUM_INTERP_NOTATION, &el);
        c = (ballum_change_t){ BALLUM_CONTACT_POSITION_X, 3, BLACK };
        assert(ballum_interp_apply(it, 0, &c, &ok) == BALLUM_ALTER_MODE_NOTATION);
        assert(ballum_interp_map_point(it, 18, 17, &mx, &my) == 0 && mx == 18);
        ballum_interp_destroy(it);
        make_scene();
    }

    /* ---- result names ---------------------------------------------------------- */
    for (int r = 0; r <= (int)BALLUM_ALTER_FRAME_FULL; ++r)
        assert(strcmp(ballum_alter_result_name((ballum_alter_result_t)r), "unknown") != 0);

    puts("test_ballum_interp: ok");
    return 0;
}
