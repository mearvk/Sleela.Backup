/*
 * Ballum header components: the APP NAME and DATE TIME slots above a hosted program.
 *
 * Text is drawn with a small built-in 5x7 bitmap font for printable ASCII
 * (0x20..0x7E). The glyphs were drawn for this project and carry no third-party
 * font licence. Characters outside that range are drawn as a hollow box so a
 * missing glyph is visible, never silently dropped. For other scripts or a
 * different look, register your own component or a ballum_charsource-backed one.
 *
 * Both constructors return a ballum_component_t for ballum_host_set_slot().
 * `state` is caller-owned storage that must outlive the component.
 */
#ifndef SLEELA_BALLUM_HEADER_H
#define SLEELA_BALLUM_HEADER_H

#include "ballum_host.h"
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BALLUM_HEADER_TEXT_MAX 128u

/* Source of the current time, so the clock can be tested. NULL = time(NULL). */
typedef time_t (*ballum_header_clock_fn)(void *user);

typedef struct {
    char                   text[BALLUM_HEADER_TEXT_MAX];
    ballum_color_t         fg;
    uint32_t               scale;     /* 1..8, pixels per font dot */
    int                    right;     /* 1 = right aligned, 0 = left aligned */
    int                    clock;     /* 1 = text is the local date and time */
    int                    utc;       /* clock only: 1 = UTC, 0 = local time */
    ballum_header_clock_fn now;
    void                  *now_user;
} ballum_header_state_t;

/* Width in pixels of `text` at `scale`, as drawn (5 dots + 1 gap per character). */
uint32_t ballum_header_text_width(const char *text, uint32_t scale);

/* Draw `text` with its top-left at (x, y) in the view. Returns the width drawn. */
uint32_t ballum_header_draw_text(ballum_surface_view_t *v, int32_t x, int32_t y,
                                 const char *text, uint32_t scale, ballum_color_t fg);

/* APP NAME: left-aligned name, vertically centred. The name is truncated to fit
 * BALLUM_HEADER_TEXT_MAX; control characters are replaced by '?'. */
ballum_status_t ballum_header_app_name(ballum_header_state_t *st, const char *name,
                                       uint32_t scale, ballum_color_t fg,
                                       ballum_component_t *out);

/* DATE TIME: right-aligned "YYYY-MM-DD HH:MM:SS", redrawn every second. */
ballum_status_t ballum_header_date_time(ballum_header_state_t *st, int utc,
                                        uint32_t scale, ballum_color_t fg,
                                        ballum_header_clock_fn now, void *now_user,
                                        ballum_component_t *out);

/* The 7 rows x 5 columns of a glyph for tests: bit 4 of each row is the left dot.
 * Returns 0 for characters without a glyph. */
int ballum_header_glyph(unsigned char ch, uint8_t rows[7]);

#ifdef __cplusplus
}
#endif

#endif /* SLEELA_BALLUM_HEADER_H */
