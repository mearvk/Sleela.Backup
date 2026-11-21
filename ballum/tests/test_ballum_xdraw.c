/* Tests for the drawing tracker: windows, core text and fonts, fills, images,
 * copies, pixmaps, RENDER glyphs recognised through a charsource, the settle
 * flag, limits, hostile input, and an end-to-end run through the wire framer. */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1
#include "sleela_ballum/ballum_xdraw.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

static ballum_xdraw_t *xd;
static int g_be;                                     /* byte order of the requests being built */

static void p16(uint8_t *p, uint32_t v) { if (g_be) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; } else { p[1] = (uint8_t)(v >> 8); p[0] = (uint8_t)v; } }
static void p32(uint8_t *p, uint32_t v) { if (g_be) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
                                           else { p[3] = (uint8_t)(v >> 24); p[2] = (uint8_t)(v >> 16); p[1] = (uint8_t)(v >> 8); p[0] = (uint8_t)v; } }

/* ---- feeding requests directly -------------------------------------------------------- */

static uint8_t g_body[1 << 16];

static int send_req(uint8_t op, uint8_t data, const char *ext, size_t body_len) {
    ballum_xreq_t r;
    memset(&r, 0, sizeof(r));
    r.big_endian = g_be; r.opcode = op; r.data = data; r.ext = ext;
    r.body = g_body; r.body_len = body_len; r.total_len = body_len + 4;
    uint32_t save = 0;
    return ballum_xdraw_on_request(xd, &r, &save);
}
#define CORE(op, data, len) send_req((op), (data), NULL, (len))
#define REND(minor, len)    send_req(0, (minor), "RENDER", (len))

static void clear_body(void) { memset(g_body, 0, sizeof(g_body)); }

static void create_window(uint32_t id, uint32_t parent, int x, int y, int w, int h) {
    clear_body(); p32(g_body, id); p32(g_body + 4, parent); p16(g_body + 8, (uint32_t)x); p16(g_body + 10, (uint32_t)y);
    p16(g_body + 12, (uint32_t)w); p16(g_body + 14, (uint32_t)h); CORE(1, 0, 28);
}
static void map_window(uint32_t id) { clear_body(); p32(g_body, id); CORE(8, 0, 4); }
static void unmap_window(uint32_t id) { clear_body(); p32(g_body, id); CORE(10, 0, 4); }
static void create_gc(uint32_t gc, uint32_t fg, uint32_t bg, uint32_t font) {
    clear_body(); p32(g_body, gc); p32(g_body + 4, 1); p32(g_body + 8, 0x4u | 0x8u | 0x4000u);
    p32(g_body + 12, fg); p32(g_body + 16, bg); p32(g_body + 20, font); CORE(55, 0, 24);
}
static void open_font(uint32_t id) { clear_body(); p32(g_body, id); p16(g_body + 4, 5); memcpy(g_body + 8, "fixed", 5); CORE(45, 0, 16); }

/* Hand the tracker a QueryFont reply: characters 32..126, every one `width` wide. */
static void font_reply(uint32_t id, int ascent, int descent, int width) {
    size_t nchars = 95, len = 60 + nchars * 12;
    uint8_t *m = (uint8_t *)calloc(1, len);
    m[0] = 1;
    p32(m + 4, (uint32_t)((len - 32) / 4));
    p16(m + 28, (uint32_t)width);                                     /* max-bounds character width */
    p16(m + 40, 32); p16(m + 42, 126); p16(m + 44, 32);
    m[49] = 0; m[50] = 0;
    p16(m + 52, (uint32_t)ascent); p16(m + 54, (uint32_t)descent);
    p32(m + 56, (uint32_t)nchars);
    for (size_t i = 0; i < nchars; ++i) p16(m + 60 + i * 12 + 4, (uint32_t)width);
    ballum_xreply_t r;
    memset(&r, 0, sizeof(r));
    r.big_endian = g_be; r.opcode = 47; r.saved = id; r.bytes = m; r.len = len;
    ballum_xdraw_on_reply(xd, &r);
    free(m);
}

static void image_text(uint32_t drawable, uint32_t gc, int x, int y, const char *s) {
    size_t n = strlen(s);
    clear_body(); p32(g_body, drawable); p32(g_body + 4, gc); p16(g_body + 8, (uint32_t)x); p16(g_body + 10, (uint32_t)y);
    memcpy(g_body + 12, s, n); CORE(76, (uint8_t)n, 12 + ((n + 3) & ~(size_t)3));
}

typedef struct { ballum_element_t e[64]; char text[4096]; size_t n; int settled; } snap_t;
static void snap(snap_t *s, uint32_t vw, uint32_t vh) {
    s->n = ballum_xdraw_snapshot(xd, vw, vh, 1u << 30, 8, s->e, 64, s->text, sizeof(s->text), &s->settled);
}
static int has_text(const snap_t *s, const char *t) {
    for (size_t i = 0; i < s->n; ++i)
        if (s->e[i].kind == BALLUM_ELEM_TEXT && s->e[i].text && strlen(t) == s->e[i].text_len && memcmp(s->e[i].text, t, s->e[i].text_len) == 0) return 1;
    return 0;
}
static const ballum_element_t *find_text(const snap_t *s, const char *t) {
    for (size_t i = 0; i < s->n; ++i)
        if (s->e[i].kind == BALLUM_ELEM_TEXT && s->e[i].text && strlen(t) == s->e[i].text_len && memcmp(s->e[i].text, t, s->e[i].text_len) == 0) return &s->e[i];
    return NULL;
}
static size_t count_kind(const snap_t *s, ballum_elem_kind_t k) { size_t c = 0; for (size_t i = 0; i < s->n; ++i) c += s->e[i].kind == k; return c; }

/* ---- a recognisable glyph set -------------------------------------------------------------- */

#define GW 5
#define GH 7
static uint8_t glyph_px(uint32_t cp, int x, int y) {
    uint32_t h = cp * 2654435761u ^ ((uint32_t)x * 40503u + (uint32_t)y * 9973u + 1u) * 2246822519u;
    h ^= h >> 13; h *= 3266489917u; h ^= h >> 16;
    return (h & 3u) == 0u ? 255 : 0;
}
static void glyph_cell(uint32_t cp, uint8_t *out) {
    for (int y = 0; y < GH; ++y) for (int x = 0; x < GW; ++x) out[y * GW + x] = glyph_px(cp, x, y);
    out[0] = 255; out[GW * GH - 1] = 255;
}

/* AddGlyphs for glyph ids 1..n, glyph k showing character chars[k-1] (0 = a blank, 4 px advance). */
static int g_glyph_x = 0;                                     /* the glyph's left bearing, for the origin test */
static void add_glyphs(uint32_t gs, const uint32_t *chars, int n, int truncated) {
    clear_body();
    size_t ids_at = 8, infos_at = ids_at + (size_t)n * 4, data_at = infos_at + (size_t)n * 12, off = data_at;
    p32(g_body, gs); p32(g_body + 4, (uint32_t)n);
    for (int i = 0; i < n; ++i) {
        p32(g_body + ids_at + (size_t)i * 4, (uint32_t)(i + 1));
        uint8_t *gi = g_body + infos_at + (size_t)i * 12;
        int blank = chars[i] == 0;
        p16(gi, blank ? 0 : GW); p16(gi + 2, blank ? 0 : GH);
        p16(gi + 4, blank ? 0 : (uint32_t)g_glyph_x); p16(gi + 6, blank ? 0 : GH);   /* origin: bearing, baseline at the bottom */
        p16(gi + 8, blank ? 4 : GW + 1); p16(gi + 10, 0);                    /* advance */
        if (!blank) {
            uint8_t cell[GW * GH]; glyph_cell(chars[i], cell);
            for (int y = 0; y < GH; ++y) memcpy(g_body + off + (size_t)y * 8, cell + y * GW, GW);   /* A8: rows padded to 4 */
            off += 8u * GH;
        }
    }
    ballum_xreq_t r;
    memset(&r, 0, sizeof(r));
    r.big_endian = g_be; r.opcode = 0; r.data = 20; r.ext = "RENDER";
    r.body = g_body; r.body_len = off; r.total_len = off + 4; r.truncated = truncated;
    uint32_t save;
    ballum_xdraw_on_request(xd, &r, &save);
}

/* CompositeGlyphs8 with ONE element: ids, starting at (dx, dy). */
static size_t elt8(uint8_t *p, int dx, int dy, const uint8_t *ids, int n) {
    p[0] = (uint8_t)n; p16(p + 4, (uint32_t)dx); p16(p + 6, (uint32_t)dy);
    memcpy(p + 8, ids, (size_t)n);
    return 8 + (((size_t)n + 3) & ~(size_t)3);
}
static void composite_glyphs(uint32_t src, uint32_t dst, uint32_t gs, size_t elts_len) {
    p32(g_body + 4, src); p32(g_body + 8, dst); p32(g_body + 16, gs); g_body[0] = 3;
    REND(23, 24 + elts_len);
}

int main(void) {
    for (g_be = 0; g_be < 2; ++g_be) {
        xd = ballum_xdraw_create();
        assert(xd);
        snap_t s;

        /* ---- windows and core text ------------------------------------------------------ */
        create_window(0x1000, 0x100, 0, 0, 200, 100);
        snap(&s, 200, 100); assert(s.n == 0);                              /* not mapped: nothing is on screen */
        map_window(0x1000);
        open_font(10);
        font_reply(10, 10, 3, 6);
        create_gc(20, 0xFF0000, 0xFFFFFF, 10);
        image_text(0x1000, 20, 10, 20, "Hello");
        snap(&s, 200, 100);
        assert(s.n == 1 && has_text(&s, "Hello"));
        const ballum_element_t *e = find_text(&s, "Hello");
        assert(e->rect.x == 10 && e->rect.y == 10 && e->rect.width == 30 && e->rect.height == 13);   /* x, baseline-ascent, 5*6, ascent+descent */
        assert(e->fg.r == 255 && e->fg.g == 0 && e->bg.r == 255 && e->bg.g == 255 && e->bg.a == 255 && e->glyph_known);
        assert(e->text != NULL && e->text[5] == '\0');

        image_text(0x1000, 20, 10, 20, "World");                            /* paints over it: the old text is gone */
        snap(&s, 200, 100);
        assert(s.n == 1 && has_text(&s, "World") && !has_text(&s, "Hello"));

        /* 16-bit text and non-ASCII Latin-1 */
        clear_body(); p32(g_body, 0x1000); p32(g_body + 4, 20); p16(g_body + 8, 60); p16(g_body + 10, 40);
        g_body[12] = 0; g_body[13] = 'A'; g_body[14] = 0; g_body[15] = 'B';
        CORE(77, 2, 16);
        snap(&s, 200, 100);
        assert(has_text(&s, "AB"));

        /* PolyText8: two items, the second moved 3 px right */
        clear_body(); p32(g_body, 0x1000); p32(g_body + 4, 20); p16(g_body + 8, 100); p16(g_body + 10, 60);
        g_body[12] = 2; g_body[13] = 0; memcpy(g_body + 14, "ab", 2);
        g_body[16] = 2; g_body[17] = 3; memcpy(g_body + 18, "cd", 2);
        CORE(74, 0, 20);
        snap(&s, 200, 100);
        e = find_text(&s, "ab"); assert(e && e->rect.x == 100 && !e->bg.a);               /* no paper: unknown */
        e = find_text(&s, "cd"); assert(e && e->rect.x == 100 + 12 + 3);

        /* a font that was never queried is not guessed at */
        create_gc(21, 0, 0xFFFFFF, 99);
        image_text(0x1000, 21, 10, 90, "zzz");
        snap(&s, 200, 100); assert(!has_text(&s, "zzz"));
        ballum_xdraw_stats_t st; ballum_xdraw_stats(xd, &st); assert(st.text_unsized >= 1);

        /* PolyFillRectangle, then ClearArea removes what it covers */
        clear_body(); p32(g_body, 0x1000); p32(g_body + 4, 20);
        p16(g_body + 8, 150); p16(g_body + 10, 5); p16(g_body + 12, 20); p16(g_body + 14, 10);
        CORE(70, 0, 16);
        snap(&s, 200, 100);
        assert(count_kind(&s, BALLUM_ELEM_FILL) == 1);
        clear_body(); p32(g_body, 0x1000); p16(g_body + 4, 140); p16(g_body + 6, 0); p16(g_body + 8, 60); p16(g_body + 10, 30);
        CORE(61, 0, 12);
        snap(&s, 200, 100); assert(count_kind(&s, BALLUM_ELEM_FILL) == 0);

        /* PutImage is an image element and paints over what it covers */
        clear_body(); p32(g_body, 0x1000); p32(g_body + 4, 20); p16(g_body + 8, 40); p16(g_body + 10, 30); p16(g_body + 12, 0); p16(g_body + 14, 0);
        CORE(72, 2, 16 + 4);
        snap(&s, 200, 100);
        assert(count_kind(&s, BALLUM_ELEM_IMAGE) == 1 && !has_text(&s, "World"));        /* World (10,10)-(40,23) is inside (0,0,40,30)? no: 40 wide */
        ballum_xdraw_destroy(xd); xd = ballum_xdraw_create();
        create_window(0x1000, 0x100, 0, 0, 200, 100); map_window(0x1000);
        open_font(10); font_reply(10, 10, 3, 6); create_gc(20, 0xFF0000, 0xFFFFFF, 10);

        /* child windows: origin offsets, map state, the server's ConfigureNotify */
        create_window(0x2000, 0x1000, 50, 40, 100, 40);
        image_text(0x2000, 20, 5, 15, "child");
        snap(&s, 200, 100); assert(!has_text(&s, "child"));                                 /* unmapped */
        map_window(0x2000);
        snap(&s, 200, 100);
        e = find_text(&s, "child");
        assert(e && e->rect.x == 55 && e->rect.y == 45 && e->glyph_known);
        uint8_t ev[32]; memset(ev, 0, sizeof(ev));
        ev[0] = 22; p32(ev + 4, 0x2000); p32(ev + 8, 0x2000); p16(ev + 16, 20); p16(ev + 18, 10); p16(ev + 20, 100); p16(ev + 22, 40);
        ballum_xdraw_on_event(xd, ev, 32, g_be);                                            /* the window manager moved it */
        snap(&s, 200, 100);
        e = find_text(&s, "child"); assert(e && e->rect.x == 25 && e->rect.y == 15);
        ev[16] = 0; ev[17] = 0; p16(ev + 16, 0); p16(ev + 20, 20);                          /* narrower: the text is clipped */
        ballum_xdraw_on_event(xd, ev, 32, g_be);
        snap(&s, 200, 100);
        e = find_text(&s, "child"); assert(e && e->rect.width < 30 && !e->glyph_known);
        unmap_window(0x1000);
        snap(&s, 200, 100); assert(s.n == 0);                                               /* a parent hides its children */
        map_window(0x1000);
        snap(&s, 100, 20);                                                                   /* a smaller viewport clips to it */
        for (size_t i = 0; i < s.n; ++i)
            assert(s.e[i].rect.x + s.e[i].rect.width <= 100 && s.e[i].rect.y + s.e[i].rect.height <= 20);

        /* pixmaps: drawn off screen, shown when copied, scrolling keeps the characters */
        create_window(0x3000, 0x100, 0, 0, 200, 100); map_window(0x3000);
        clear_body(); p32(g_body, 0x4000); p32(g_body + 4, 0x3000); p16(g_body + 8, 100); p16(g_body + 10, 50); CORE(53, 24, 12);
        image_text(0x4000, 20, 4, 14, "buffered");
        snap(&s, 200, 100); assert(!has_text(&s, "buffered"));                              /* off screen */
        clear_body(); p32(g_body, 0x4000); p32(g_body + 4, 0x3000); p32(g_body + 8, 20);
        p16(g_body + 12, 0); p16(g_body + 14, 0); p16(g_body + 16, 30); p16(g_body + 18, 60); p16(g_body + 20, 100); p16(g_body + 22, 50);
        CORE(62, 0, 24);
        snap(&s, 200, 100);
        e = find_text(&s, "buffered");
        assert(e && e->rect.x == 34 && e->rect.y == 64 && e->glyph_known);                 /* (4,4) + (30,60) */
        assert(count_kind(&s, BALLUM_ELEM_IMAGE) >= 1);
        /* scroll the window up by 20 px: the text moves with the pixels */
        clear_body(); p32(g_body, 0x3000); p32(g_body + 4, 0x3000); p32(g_body + 8, 20);
        p16(g_body + 12, 0); p16(g_body + 14, 20); p16(g_body + 16, 0); p16(g_body + 18, 0); p16(g_body + 20, 200); p16(g_body + 22, 80);
        CORE(62, 0, 24);
        snap(&s, 200, 100);
        e = find_text(&s, "buffered"); assert(e && e->rect.x == 34 && e->rect.y == 44);
        /* a copy that cuts a text run in half marks it unknown */
        clear_body(); p32(g_body, 0x3000); p32(g_body + 4, 0x3000); p32(g_body + 8, 20);
        p16(g_body + 12, 40); p16(g_body + 14, 0); p16(g_body + 16, 140); p16(g_body + 18, 0); p16(g_body + 20, 30); p16(g_body + 22, 100);
        CORE(62, 0, 24);
        snap(&s, 200, 100);
        e = find_text(&s, "buffered");
        for (size_t i = 0; i < s.n; ++i) if (s.e[i].kind == BALLUM_ELEM_TEXT && s.e[i].rect.x >= 140) assert(!s.e[i].glyph_known);

        /* ---- RENDER: glyph images recognised through a charsource ---------------------------- */
        uint32_t chars[4] = { '1', '2', 0, '3' };                                          /* glyph 3 is a space */
        ballum_charsource_t *cs = ballum_charsource_create();
        for (uint32_t c = '0'; c <= '9'; ++c) { uint8_t cell[GW * GH]; glyph_cell(c, cell); assert(ballum_charsource_add_glyph(cs, c, 0, cell, GW, GH, GW + 1) == BALLUM_STATUS_OK); }
        ballum_xdraw_set_charsource(xd, cs);

        create_window(0x5000, 0x100, 0, 0, 200, 100); map_window(0x5000);
        clear_body(); p32(g_body, 30); p32(g_body + 4, 0x5000); p32(g_body + 8, 1); REND(4, 16);       /* picture 30 on the window */
        clear_body(); p32(g_body, 31); p16(g_body + 4, 0x8000); p16(g_body + 6, 0x4000); p16(g_body + 8, 0); p16(g_body + 10, 0xFFFF); REND(33, 12);   /* solid fill */
        clear_body(); p32(g_body, 40); p32(g_body + 4, 7); REND(17, 8);                                  /* glyph set 40 */
        add_glyphs(40, chars, 4, 0);
        ballum_xdraw_stats(xd, &st); assert(st.glyphs == 4);

        clear_body();
        uint8_t ids[4] = { 1, 2, 3, 4 };
        size_t el = elt8(g_body + 24, 20, 30, ids, 4);
        composite_glyphs(31, 30, 40, el);
        snap(&s, 200, 100);
        e = find_text(&s, "12 3");
        assert(e && e->glyph_known);                                                       /* a blank glyph is a space */
        assert(e->rect.x == 20 && e->rect.y == 30 - GH && e->rect.height == GH);
        assert(e->fg.r == 0x80 && e->fg.g == 0x40 && e->fg.b == 0 && e->fg.a == 255);     /* from the solid-fill picture */
        assert(e->bg.a == 0);                                                              /* paper unknown until the frame is read */
        ballum_xdraw_stats(xd, &st); assert(st.glyphs_known >= 3);

        /* two elements far apart are two runs; an unknown glyph id spoils certainty */
        clear_body();
        uint8_t a[2] = { 1, 1 }, b[2] = { 2, 99 };
        el = elt8(g_body + 24, 10, 60, a, 2);
        el += elt8(g_body + 24 + el, 0, 25, b, 2);                                          /* 25 px lower */
        composite_glyphs(31, 30, 40, el);
        snap(&s, 200, 100);
        assert(has_text(&s, "11"));
        const ballum_element_t *low = find_text(&s, "2");
        assert(low && !low->glyph_known && low->rect.y > 70);

        /* the same glyphs without a charsource: positions and ink, but no characters claimed */
        ballum_xdraw_set_charsource(xd, NULL);
        clear_body();
        uint8_t one[1] = { 1 };
        el = elt8(g_body + 24, 100, 90, one, 1);
        composite_glyphs(31, 30, 40, el);
        snap(&s, 200, 100);
        int found_unknown = 0;
        for (size_t i = 0; i < s.n; ++i)
            if (s.e[i].kind == BALLUM_ELEM_TEXT && s.e[i].rect.x == 100 && s.e[i].rect.y == 90 - GH) { found_unknown = !s.e[i].glyph_known; assert(s.e[i].text_len == 3); }
        assert(found_unknown);                                                              /* U+FFFD, not a guess */
        ballum_xdraw_set_charsource(xd, cs);

        /* the glyph origin: the box starts at pen minus the glyph's own x and y */
        g_glyph_x = 2;
        uint32_t one_char[1] = { '5' };
        add_glyphs(50, one_char, 1, 0);
        g_glyph_x = 0;
        clear_body();
        el = elt8(g_body + 24, 60, 70, one, 1);
        composite_glyphs(31, 30, 50, el);
        snap(&s, 200, 100);
        e = find_text(&s, "5");
        assert(e && e->rect.x == 60 - 2 && e->rect.y == 70 - GH && e->glyph_known);

        /* a glyph the character source does not hold is NOT claimed as any character */
        uint32_t stranger[1] = { 'Z' };
        add_glyphs(51, stranger, 1, 0);
        clear_body();
        el = elt8(g_body + 24, 90, 20, one, 1);
        composite_glyphs(31, 30, 51, el);
        snap(&s, 200, 100);
        {
            const ballum_element_t *z = NULL;
            for (size_t i = 0; i < s.n; ++i) if (s.e[i].kind == BALLUM_ELEM_TEXT && s.e[i].rect.x == 90) z = &s.e[i];
            assert(z && !z->glyph_known && z->text_len == 3 && memcmp(z->text, "\xEF\xBF\xBD", 3) == 0);
        }

        /* glyph set switch inside a request, and a reference to a set */
        clear_body(); p32(g_body, 41); p32(g_body + 4, 40); REND(18, 8);                    /* 41 refers to 40 */
        clear_body();
        el = 0;
        g_body[24] = 255; p32(g_body + 24 + 8, 41); el = 12;                                 /* switch to set 41 */
        uint8_t id3[1] = { 2 };
        el += elt8(g_body + 24 + el, 120, 40, id3, 1);
        composite_glyphs(31, 30, 40, el);
        snap(&s, 200, 100);
        assert(has_text(&s, "2"));

        /* a truncated upload is ignored; freeing glyphs makes them unknown again */
        ballum_xdraw_stats(xd, &st); uint64_t before = st.glyphs;
        add_glyphs(40, chars, 4, 1);
        ballum_xdraw_stats(xd, &st); assert(st.glyphs == before);
        clear_body(); p32(g_body, 40); p32(g_body + 4, 1); REND(22, 8);                      /* FreeGlyphs: glyph 1 */
        clear_body();
        el = elt8(g_body + 24, 20, 20, one, 1);
        composite_glyphs(31, 30, 40, el);
        snap(&s, 200, 100);
        assert(!has_text(&s, "1") || find_text(&s, "1")->rect.y != 20 - GH);

        /* FillRectangles and Composite */
        clear_body(); p32(g_body + 4, 30); p16(g_body + 8, 0xFFFF); p16(g_body + 10, 0); p16(g_body + 12, 0); p16(g_body + 14, 0xFFFF);
        p16(g_body + 16, 150); p16(g_body + 18, 80); p16(g_body + 20, 30); p16(g_body + 22, 10); g_body[0] = 1;
        REND(26, 24);
        snap(&s, 200, 100);
        int red_fill = 0;
        for (size_t i = 0; i < s.n; ++i) if (s.e[i].kind == BALLUM_ELEM_FILL && s.e[i].rect.x == 150 && s.e[i].fg.r == 255) red_fill = 1;
        assert(red_fill);
        clear_body(); p32(g_body + 4, 31); p32(g_body + 8, 0); p32(g_body + 12, 30);          /* solid source, no mask */
        p16(g_body + 24, 5); p16(g_body + 26, 5); p16(g_body + 28, 10); p16(g_body + 30, 10); g_body[0] = 3;
        REND(8, 32);
        snap(&s, 200, 100);
        int solid = 0;
        for (size_t i = 0; i < s.n; ++i) if (s.e[i].kind == BALLUM_ELEM_FILL && s.e[i].rect.x == 5 && s.e[i].rect.width == 10) solid = 1;
        assert(solid);
        clear_body(); p32(g_body, 30); REND(7, 4);                                            /* FreePicture */
        el = elt8(g_body + 24, 20, 20, one, 1);
        composite_glyphs(31, 30, 40, el);                                                      /* its destination is gone: ignored */
        clear_body(); p32(g_body, 40); REND(19, 4);                                            /* FreeGlyphSet */

        /* ---- the settle flag -------------------------------------------------------------------- */
        struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
        uint64_t real_now = (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
        image_text(0x3000, 20, 10, 10, "tick");
        int sett;
        ballum_xdraw_snapshot(xd, 200, 100, real_now + 2, 8, s.e, 64, s.text, sizeof(s.text), &sett);
        assert(sett == 0);                                                                     /* it drew just now */
        ballum_xdraw_snapshot(xd, 200, 100, real_now + 60, 8, s.e, 64, s.text, sizeof(s.text), &sett);
        assert(sett == 1);

        ballum_xdraw_destroy(xd);
        ballum_charsource_destroy(cs);
    }

    /* ---- limits and hostile input ------------------------------------------------------------------ */
    g_be = 0;
    xd = ballum_xdraw_create();
    create_window(0x1000, 0x100, 0, 0, 640, 480); map_window(0x1000);
    create_gc(20, 0x112233, 0x445566, 0);
    for (int i = 0; i < 700; ++i) {
        clear_body(); p32(g_body, 0x1000); p32(g_body + 4, 20); p16(g_body + 8, (uint32_t)(i % 600)); p16(g_body + 10, (uint32_t)(i / 2));
        p16(g_body + 12, 3); p16(g_body + 14, 3); CORE(70, 0, 16);
    }
    {
        static ballum_element_t big[2048]; static char txt[64]; int sett;
        size_t n = ballum_xdraw_snapshot(xd, 640, 480, 1u << 30, 8, big, 2048, txt, sizeof(txt), &sett);
        ballum_xdraw_stats_t st; ballum_xdraw_stats(xd, &st);
        assert(n <= 512 && st.dropped >= 188);                                                 /* bounded, and counted */
        n = ballum_xdraw_snapshot(xd, 640, 480, 1u << 30, 8, big, 10, txt, sizeof(txt), &sett);
        assert(n == 10);                                                                       /* the caller's capacity is respected */
        assert(ballum_xdraw_snapshot(xd, 0, 480, 0, 0, big, 10, txt, 64, &sett) == 0);
        assert(ballum_xdraw_snapshot(NULL, 640, 480, 0, 0, big, 10, txt, 64, &sett) == 0);
        assert(ballum_xdraw_snapshot(xd, 640, 480, 0, 0, NULL, 10, txt, 64, &sett) == 0);
        /* text that does not fit the arena is reported without its characters, never overrun */
        open_font(10); font_reply(10, 10, 3, 6); create_gc(22, 0, 0xFFFFFF, 10);
        image_text(0x1000, 22, 5, 300, "a long enough string");
        n = ballum_xdraw_snapshot(xd, 640, 480, 1u << 30, 8, big, 2048, txt, 8, &sett);
        for (size_t i = 0; i < n; ++i) if (big[i].kind == BALLUM_ELEM_TEXT) assert(big[i].text == NULL && !big[i].glyph_known);
    }

    /* random requests at a page boundary: an over-read would fault */
    {
        long pg = sysconf(_SC_PAGESIZE);
        uint8_t *map = (uint8_t *)mmap(NULL, (size_t)pg * 3, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        assert(map != MAP_FAILED);
        assert(mprotect(map + pg * 2, (size_t)pg, PROT_NONE) == 0);
        uint32_t rng = 12345u;
        static const char *exts[3] = { NULL, "RENDER", "OTHER" };
        for (int iter = 0; iter < 40000; ++iter) {
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            size_t len = rng % 200;
            uint8_t *body = map + pg * 2 - len;                                                /* ends exactly at the guard page */
            for (size_t k = 0; k < len; ++k) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; body[k] = (uint8_t)rng; }
            if (len >= 4 && (rng & 3u) == 0) { body[0] = 0; body[1] = 0; body[2] = 0; body[3] = 0; }
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            ballum_xreq_t r;
            memset(&r, 0, sizeof(r));
            r.big_endian = (int)(rng & 1u); r.ext = exts[(rng >> 1) % 3];
            r.opcode = r.ext ? (uint8_t)(128 + (rng >> 4) % 100) : (uint8_t)((rng >> 8) % 100);
            r.data = (uint8_t)(rng >> 16);
            if (r.ext) r.data = (uint8_t)(r.data % 40);
            r.body = body; r.body_len = len; r.total_len = len + 4; r.truncated = (int)((rng >> 24) & 1u);
            uint32_t save = 0;
            ballum_xdraw_on_request(xd, &r, &save);
            if ((iter % 50) == 0) {                                                             /* replies and events too */
                ballum_xreply_t rp; memset(&rp, 0, sizeof(rp));
                rp.big_endian = r.big_endian; rp.opcode = 47; rp.saved = rng % 20; rp.bytes = body; rp.len = len; rp.truncated = r.truncated;
                ballum_xdraw_on_reply(xd, &rp);
                ballum_xdraw_on_event(xd, body, len, r.big_endian);
            }
        }
        static ballum_element_t out[600]; static char tx[2048]; int sett;
        ballum_xdraw_snapshot(xd, 640, 480, 1u << 30, 8, out, 600, tx, sizeof(tx), &sett);       /* the state it left is still sound */
        munmap(map, (size_t)pg * 3);
    }
    ballum_xdraw_destroy(xd);
    ballum_xdraw_destroy(NULL);

    puts("test_ballum_xdraw: ok");
    return 0;
}
