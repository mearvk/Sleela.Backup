/*
 * ballum/include/sleela_ballum/ballum_xdraw.h
 * What the program DREW: the semantic half of Ballum's hook into the standard X11
 * stack. It consumes the framed requests, replies and events of ballum_xwire.h
 * and keeps just enough of the X server's drawing state to say, for the picture
 * the program has on screen, WHICH elements are there: where each run of text is
 * and what characters it holds, where each fill and image is, in what colours.
 * That list is what the interpreter (ballum_interp.h) acts on.
 *
 * What it follows
 *   windows        create, destroy, reparent, map, unmap, configure (and the
 *                  server's ConfigureNotify, so a window the window manager moved
 *                  is where the server put it)
 *   pixmaps, GCs   create, change, free; the GC's foreground, background and font
 *   core fonts     OpenFont, and the QueryFont reply: ascent, descent and the
 *                  width of every character, which is what sizes a core text run
 *   core drawing   ImageText8/16, PolyText8/16, PolyFillRectangle, PutImage,
 *                  CopyArea (scrolling), ClearArea
 *   RENDER         pictures, solid fills, glyph sets, AddGlyphs (the glyph images
 *                  and their metrics), CompositeGlyphs8/16/32 (the text of GTK,
 *                  Qt, Xft and cairo programs), FillRectangles, Composite
 *
 * Drawing into a pixmap is remembered on the pixmap and moves to a window when the
 * program copies it there, so double-buffered programs are followed.
 *
 * Characters. A core text request carries the characters themselves (Latin-1 for
 * 8-bit requests, the 16-bit code for the 16-bit ones: this assumes an ISO 8859-1
 * or ISO 10646 font encoding, as nearly every core font is). A RENDER text request
 * carries only glyph NUMBERS and the glyph images uploaded earlier; the characters
 * are recovered by matching each glyph image against a ballum_charsource_t. A
 * glyph that matches clearly is a known character; one that does not stays a
 * U+FFFD placeholder and its element is not marked `glyph_known`.
 *
 * Honest limits
 *   - The element list is the program's drawing REQUESTS, read as they pass. It
 *     is exact only when the program is idle (see `settled` in the snapshot); while
 *     it is drawing, a snapshot can be a frame behind the pixels.
 *   - Colours: a GC pixel value is read as 0xRRGGBB (24/32-bit TrueColor with the
 *     standard masks, which Ballum's own display uses). A glyph drawn through a
 *     non-solid source picture has an unknown ink colour (alpha 0).
 *   - RENDER's initial glyph pen is taken as (0,0) with the first delta absolute,
 *     as Xft and cairo send it. This has not been confirmed against a live
 *     server.
 *   - Element rectangles of core text come from font metrics only when the
 *     program queried the font (as Xlib's XLoadQueryFont does); text in a font
 *     that was never queried is not sized and is not reported.
 *   - Bounded: at most 512 elements per drawable and fixed table sizes. What does
 *     not fit is dropped and counted, never grown without limit.
 *
 * The functions are thread-safe: a relay thread feeds requests while the host
 * takes snapshots.
 */
#ifndef SLEELA_BALLUM_XDRAW_H
#define SLEELA_BALLUM_XDRAW_H

#include "ballum_charsource.h"
#include "ballum_interp.h"
#include "ballum_xwire.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ballum_xdraw ballum_xdraw_t;

typedef struct {
    uint64_t requests;          /* requests seen                                   */
    uint64_t drawing;           /* of which drew something                         */
    uint64_t elements;          /* elements created                                */
    uint64_t dropped;           /* elements or table entries dropped for room      */
    uint64_t glyphs;            /* glyph images stored                             */
    uint64_t glyphs_known;      /* of which matched a character                    */
    uint64_t text_unsized;      /* core text not reported: its font was never queried */
} ballum_xdraw_stats_t;

ballum_xdraw_t *ballum_xdraw_create(void);
void            ballum_xdraw_destroy(ballum_xdraw_t *xd);

/* The source of characters for RENDER glyphs. Borrowed: keep it alive while the
 * tracker runs, and do not change it from another thread while requests flow. */
void ballum_xdraw_set_charsource(ballum_xdraw_t *xd, const ballum_charsource_t *cs);

/* The callbacks to hand to ballum_xwire_create(): pass `xd` as the user pointer. */
int  ballum_xdraw_on_request(void *xd, const ballum_xreq_t *req, uint32_t *save);
void ballum_xdraw_on_reply(void *xd, const ballum_xreply_t *rep);
void ballum_xdraw_on_event(void *xd, const uint8_t *ev, size_t len, int big_endian);

/* The elements now on screen, in viewport coordinates (the root window's origin
 * is the viewport's), in drawing order (`z` rises). Text is copied into `text`
 * (capacity text_cap) and each element's text points into it. `*settled` is 1
 * when no drawing request arrived in the last `settle_ms` before `now_ms`.
 * Returns the number of elements written (at most cap). */
size_t ballum_xdraw_snapshot(ballum_xdraw_t *xd, uint32_t viewport_w, uint32_t viewport_h,
                             uint64_t now_ms, uint32_t settle_ms,
                             ballum_element_t *out, size_t cap,
                             char *text, size_t text_cap, int *settled);

void ballum_xdraw_stats(ballum_xdraw_t *xd, ballum_xdraw_stats_t *out);

/* Minimum glyph-match score (0..1000) and margin for a glyph to count as a known
 * character. */
#define BALLUM_XDRAW_MIN_SCORE   900
#define BALLUM_XDRAW_MIN_MARGIN   20

#ifdef __cplusplus
}
#endif

#endif /* SLEELA_BALLUM_XDRAW_H */
