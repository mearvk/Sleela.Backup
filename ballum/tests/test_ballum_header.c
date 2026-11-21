#include "sleela_ballum/ballum_header.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

static time_t fixed_now(void *u) { return *(time_t *)u; }

static int ink(const ballum_color_t *px, size_t n, ballum_color_t fg) {
    int k = 0;
    for (size_t i = 0; i < n; ++i) if (px[i].r == fg.r && px[i].g == fg.g && px[i].b == fg.b) ++k;
    return k;
}

int main(void) {
    /* every printable glyph has ink (space excepted) and no two glyphs are alike,
     * apart from the few that differ only by case on purpose */
    uint8_t a[7], b[7];
    for (int c = 0x21; c <= 0x7E; ++c) {
        CHECK(ballum_header_glyph((unsigned char)c, a) == 1);
        int any = 0; for (int r = 0; r < 7; ++r) { any |= a[r]; CHECK((a[r] & ~0x1F) == 0); }
        CHECK(any);
        for (int d = c + 1; d <= 0x7E; ++d) {
            CHECK(ballum_header_glyph((unsigned char)d, b) == 1);
            if (memcmp(a, b, 7) == 0) { printf("glyphs %c and %c are identical\n", c, d); return 1; }
        }
    }
    CHECK(ballum_header_glyph(' ', a) == 1);
    for (int r = 0; r < 7; ++r) CHECK(a[r] == 0);
    CHECK(ballum_header_glyph(0x80, a) == 0 && ballum_header_glyph(0x1F, a) == 0);

    CHECK(ballum_header_text_width("", 2) == 0);
    CHECK(ballum_header_text_width("AB", 2) == (2u * 6u - 1u) * 2u);
    CHECK(ballum_header_text_width(NULL, 2) == 0);

    ballum_color_t fg = { 10, 20, 30, 255 }, bg = { 255, 255, 255, 255 };
    enum { W = 400, H = 32 };
    static ballum_color_t px[W * H];
    ballum_surface_view_t v = { px, W, H, W };

    /* app name: ink appears on the left, none on the right */
    ballum_header_state_t name, clk;
    ballum_component_t cn, cc;
    CHECK(ballum_header_app_name(&name, "Terminal", 2, fg, &cn) == BALLUM_STATUS_OK);
    CHECK(cn.draw && cn.refresh_ms == 0);
    for (size_t i = 0; i < W * H; ++i) px[i] = bg;
    cn.draw(cn.user, &v);
    int left = 0, right = 0;
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x)
        if (px[y * W + x].r == fg.r) { if (x < W / 2) ++left; else ++right; }
    CHECK(left > 50 && right == 0);

    /* control characters are replaced, an over-long name is truncated, never overruns */
    char big[400]; memset(big, 'x', sizeof(big) - 1); big[sizeof(big) - 1] = '\0'; big[3] = '\n';
    CHECK(ballum_header_app_name(&name, big, 1, fg, &cn) == BALLUM_STATUS_OK);
    CHECK(strlen(name.text) == BALLUM_HEADER_TEXT_MAX - 1 && name.text[3] == '?');
    cn.draw(cn.user, &v);                                        /* clipped, no crash */
    CHECK(ballum_header_app_name(&name, "x", 0, fg, &cn) == BALLUM_STATUS_INVALID_ARGUMENT);
    CHECK(ballum_header_app_name(&name, "x", 9, fg, &cn) == BALLUM_STATUS_INVALID_ARGUMENT);
    CHECK(ballum_header_app_name(NULL, "x", 1, fg, &cn) == BALLUM_STATUS_INVALID_ARGUMENT);

    /* date time: fixed clock -> exact text, right aligned, redraws each second */
    time_t t = 1700000000;                                      /* 2023-11-14 22:13:20 UTC */
    CHECK(ballum_header_date_time(&clk, 1, 2, fg, fixed_now, &t, &cc) == BALLUM_STATUS_OK);
    CHECK(cc.refresh_ms == 1000);
    for (size_t i = 0; i < W * H; ++i) px[i] = bg;
    cc.draw(cc.user, &v);
    CHECK(strcmp(clk.text, "2023-11-14 22:13:20") == 0);
    left = right = 0;
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x)
        if (px[y * W + x].r == fg.r) { if (x < W / 4) ++left; else ++right; }
    CHECK(right > 100 && left == 0);
    t += 1; cc.draw(cc.user, &v);
    CHECK(strcmp(clk.text, "2023-11-14 22:13:21") == 0);

    /* drawing the same text twice paints exactly the same pixels */
    for (size_t i = 0; i < W * H; ++i) px[i] = bg;
    ballum_header_draw_text(&v, 4, 4, "Hi", 1, fg);
    int n1 = ink(px, W * H, fg);
    ballum_header_draw_text(&v, 4, 4, "Hi", 1, fg);
    CHECK(n1 > 0 && ink(px, W * H, fg) == n1);
    CHECK(ballum_header_draw_text(&v, -100, -100, "Hi", 3, fg) == ballum_header_text_width("Hi", 3));
    CHECK(ballum_header_draw_text(&v, 0, 0, "Hi", 9, fg) == 0);

    puts("test_ballum_header: ok");
    return 0;
}
