/*
 * Integration test for the private-display program source.
 *
 * Needs Xvfb, libXtst.so.6 and an X client (xterm by default, or the program in
 * BALLUM_TEST_X11_PROGRAM). When any of them is missing the source must fail
 * closed WITHOUT leaving files behind, and this test reports SKIPPED and exits
 * 0. When they are present it runs the real thing: launch, capture, 1:1 input.
 */
#define _GNU_SOURCE 1
#include "sleela_ballum/ballum_host.h"
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define W 640u
#define H 400u

static unsigned count_private_dirs(void) {
    unsigned n = 0;
    const char *bases[2] = { getenv("XDG_RUNTIME_DIR"), "/tmp" };
    for (int b = 0; b < 2; ++b) {
        if (!bases[b] || !*bases[b]) continue;
        DIR *d = opendir(bases[b]);
        if (!d) continue;
        struct dirent *e;
        while ((e = readdir(d))) if (strncmp(e->d_name, "ballum-", 7) == 0) ++n;
        closedir(d);
    }
    return n;
}

static void nap(unsigned ms) {
    struct timespec ts = { (time_t)(ms / 1000u), (long)(ms % 1000u) * 1000000L };
    nanosleep(&ts, NULL);
}

static size_t distinct_colors(const ballum_color_t *p, size_t n) {
    size_t seen = 0; uint32_t first[8];
    for (size_t i = 0; i < n && seen < 8; ++i) {
        uint32_t v = ((uint32_t)p[i].r << 16) | ((uint32_t)p[i].g << 8) | p[i].b;
        size_t j = 0;
        while (j < seen && first[j] != v) ++j;
        if (j == seen) first[seen++] = v;
    }
    return seen;
}


/* The hook: run a program behind the relay and check what it saw. Essential facts
 * are asserted (the relay ran, requests were framed, the program drew, elements
 * appeared); the facts that rest on unconfirmed assumptions are only PRINTED, so
 * the first live run answers them. */
static void observe_phase(const char *label, const char *const *argv, int expect_text) {
    ballum_program_t p;
    memset(&p, 0, sizeof(p));
    p.argv = argv; p.sandbox = 1; p.log_fd = -1; p.observe = 1;
    ballum_host_source_t src; void *ctx = NULL;
    assert(ballum_host_source_x11_create(&p, &src, &ctx) == BALLUM_STATUS_OK && src.elements);
    ballum_status_t st = src.start(ctx, W, H);
    if (st != BALLUM_STATUS_OK) { printf("  [%s] SKIPPED (start: %s)\n", label, ballum_status_name(st)); ballum_host_source_x11_destroy(ctx); return; }
    if (!ballum_host_source_x11_observing(ctx)) {
        printf("  [%s] SKIPPED (the relay could not start: needs a writable /tmp/.X11-unix)\n", label);
        src.stop(ctx); ballum_host_source_x11_destroy(ctx); return;
    }
    const ballum_color_t *px = NULL; int changed = 0;
    size_t best = 0, texts = 0, known = 0;
    char sample[80] = "";
    int typed = 0;
    for (int i = 0; i < 400; ++i) {                                       /* up to ten seconds */
        assert(src.poll_frame(ctx, &px, &changed) == BALLUM_STATUS_OK);
        if (px && i > 60 && !typed) {                                     /* once it has drawn, type something */
            const uint32_t keys[] = { 'e', 'c', 'h', 'o', ' ', 'h', 'i' };
            for (size_t k = 0; k < sizeof(keys) / sizeof(keys[0]); ++k) { src.key(ctx, keys[k], 0, 1); src.key(ctx, keys[k], 0, 0); }
            typed = 1;
        }
        const ballum_element_t *el = NULL; size_t n = 0;
        assert(src.elements(ctx, &el, &n) == BALLUM_STATUS_OK);
        if (n > best) {
            best = n; texts = known = 0;
            for (size_t k = 0; k < n; ++k)
                if (el[k].kind == BALLUM_ELEM_TEXT) {
                    ++texts; known += el[k].glyph_known;
                    if (!sample[0] && el[k].text_len) snprintf(sample, sizeof(sample), "%.*s", (int)(el[k].text_len > 60 ? 60 : el[k].text_len), el[k].text);
                }
        }
        if (texts > 0 && typed && i > 120) break;
        nap(25);
    }
    ballum_xdraw_stats_t ds; ballum_x11proxy_stats_t rs;
    assert(ballum_host_source_x11_stats(ctx, &ds, &rs) == BALLUM_STATUS_OK);
    printf("  [%s] relay: connections=%llu to-server=%llu to-client=%llu unobserved=%llu\n"
           "  [%s] seen:  requests=%llu drawing=%llu elements=%llu glyphs=%llu glyphs-known=%llu text-unsized=%llu\n"
           "  [%s] frame: best element list=%zu (text runs=%zu, known=%zu) sample=\"%s\"\n",
           label, (unsigned long long)rs.connections, (unsigned long long)rs.bytes_to_server, (unsigned long long)rs.bytes_to_client,
           (unsigned long long)rs.unobserved, label, (unsigned long long)ds.requests, (unsigned long long)ds.drawing,
           (unsigned long long)ds.elements, (unsigned long long)ds.glyphs, (unsigned long long)ds.glyphs_known,
           (unsigned long long)ds.text_unsized, label, best, texts, known, sample);
    assert(rs.connections >= 1 && rs.bytes_to_server > 0 && rs.bytes_to_client > 0);   /* bytes crossed both ways */
    assert(ds.requests > 0 && ds.drawing > 0);                                        /* the framer followed the program */
    assert(best > 0 && "the program drew, but no element was reported");
    if (expect_text) assert(texts > 0 && "no text run was reported");
    src.stop(ctx);
    ballum_host_source_x11_destroy(ctx);
}

int main(void) {
    const char *prog = getenv("BALLUM_TEST_X11_PROGRAM");
    if (!prog || !*prog) prog = "xterm";
    const char *argv_xterm[] = { prog, "-fa", "Monospace", "-bg", "white", "-fg", "black", NULL };
    const char *argv_other[] = { prog, NULL };
    ballum_program_t p;
    memset(&p, 0, sizeof(p));
    p.argv = getenv("BALLUM_TEST_X11_PROGRAM") ? argv_other : argv_xterm;
    p.sandbox = 1; p.log_fd = -1;

    unsigned dirs_before = count_private_dirs();
    ballum_host_source_t src; void *ctx = NULL;
    ballum_status_t st = ballum_host_source_x11_create(&p, &src, &ctx);
    if (st == BALLUM_STATUS_UNSUPPORTED && !ctx) { puts("test_ballum_host_x11: SKIPPED (built without X11)"); return 0; }
    assert(st == BALLUM_STATUS_OK && ctx && src.start && src.poll_frame && src.key && src.stop);

    st = src.start(ctx, W, H);
    if (st != BALLUM_STATUS_OK) {
        /* missing Xvfb / libXtst / program: fail closed, leave nothing behind */
        assert(st == BALLUM_STATUS_UNSUPPORTED || st == BALLUM_STATUS_BACKEND_UNAVAILABLE ||
               st == BALLUM_STATUS_PERMISSION_DENIED);
        assert(!src.alive(ctx));
        src.stop(ctx);
        ballum_host_source_x11_destroy(ctx);
        assert(count_private_dirs() == dirs_before);
        printf("test_ballum_host_x11: SKIPPED (source failed closed with status %s)\n", ballum_status_name(st));
        return 0;
    }

    /* the program draws something */
    const ballum_color_t *px = NULL; int changed = 0; int got = 0;
    for (int i = 0; i < 400 && !got; ++i) {
        assert(src.poll_frame(ctx, &px, &changed) == BALLUM_STATUS_OK);
        if (px && changed) got = 1; else nap(25);
    }
    assert(got && "the program never produced a frame");
    assert(src.alive(ctx));
    assert(distinct_colors(px, (size_t)W * H) >= 2);

    /* 1:1 input: type into the program and see the frame change */
    ballum_color_t *before = malloc((size_t)W * H * sizeof(ballum_color_t));
    memcpy(before, px, (size_t)W * H * sizeof(ballum_color_t));
    assert(src.pointer(ctx, 100, 100) == BALLUM_STATUS_OK);
    const uint32_t keys[] = { 'e', 'c', 'h', 'o', ' ', 'h', 'i' };
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        assert(src.key(ctx, keys[i], 0, 1) == BALLUM_STATUS_OK);
        assert(src.key(ctx, keys[i], 0, 0) == BALLUM_STATUS_OK);
    }
    int moved = 0;
    for (int i = 0; i < 200 && !moved; ++i) {
        nap(25);
        assert(src.poll_frame(ctx, &px, &changed) == BALLUM_STATUS_OK);
        if (changed && memcmp(before, px, (size_t)W * H * sizeof(ballum_color_t)) != 0) moved = 1;
    }
    assert(moved && "typed input did not reach the program");
    free(before);

    src.stop(ctx);
    assert(!src.alive(ctx));
    src.stop(ctx);                                   /* idempotent */
    ballum_host_source_x11_destroy(ctx);
    assert(count_private_dirs() == dirs_before);     /* auth file and directory removed */
    /* behind the relay: a core-font xterm (ImageText) and an Xft/RENDER xterm */
    {
        const char *core[] = { prog, "-fn", "fixed", "-xrm", "XTerm*renderFont: false", "-bg", "white", "-fg", "black", NULL };
        const char *xft[]  = { prog, "-fa", "Monospace", "-bg", "white", "-fg", "black", NULL };
        puts("test_ballum_host_x11: observing a program through the relay");
        observe_phase("core fonts", core, 1);
        observe_phase("xft/render", xft, 1);
    }
    puts("test_ballum_host_x11: ok (real program captured, driven and observed)");
    return 0;
}
