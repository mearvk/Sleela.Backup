/*
 * ballum/include/sleela_ballum/ballum_charsource.h
 * Ballum's end of SOURCING CHARACTERS: one interface that takes in a pixel map
 * or a known charset and turns it into glyphs Ballum can both DRAW (as the
 * interpreter's glyph source) and RECOGNISE (match a glyph image the program
 * drew back to the character it is).
 *
 *   pixel map (atlas image) + charset  --\
 *   explicit glyph bitmaps             ---+--> CHARSOURCE --> render(char) -> pixels
 *   code point list                    --/               \--> identify(pixels) -> char + score
 *
 * KNOWN CHARSETS name which characters a pixel map holds and in what order:
 *   ASCII       U+0020..U+007E                       95 characters
 *   LATIN1      ASCII, then U+00A0..U+00FF           191 characters
 *   DIGITS      U+0030..U+0039                       10 characters
 *   ALNUM       0-9, A-Z, a-z                        62 characters
 * or an explicit list of code points for any other set.
 *
 * A PIXEL MAP is an image divided into a grid of equal cells, read left to right
 * and top to bottom; cell i holds the i-th character of the charset. The
 * coverage of a pixel (how much ink, 0..255) is taken from its alpha, or from its
 * brightness for an image that has none (dark ink on a light page, or light ink
 * on a dark page). A bitmap font made this way has one fixed size, so it renders
 * only when the line is at least a cell tall; it is never scaled silently.
 *
 * Ballum ships no font: the charset names only say WHICH characters an image
 * holds, so no font licence is bundled. The caller brings the glyphs.
 *
 * IDENTIFY compares a glyph image against every glyph held, after trimming both
 * to the ink they contain, and scores the match 0..1000 (1000 = identical
 * pixels). It also reports the MARGIN to the best different character, so a
 * caller can tell a clean match from an ambiguous one. The score and margin are
 * the natural inputs for the interpreter's Confidence axis.
 *
 * Everything here treats its input as untrusted: sizes, counts and indices are
 * bounds-checked and a bad request is refused with a status, never half-applied.
 */
#ifndef SLEELA_BALLUM_CHARSOURCE_H
#define SLEELA_BALLUM_CHARSOURCE_H

#include "ballum_interp.h"     /* ballum_glyph_source_t, ballum_color_t */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BALLUM_CHARSET_ASCII = 0,
    BALLUM_CHARSET_LATIN1,
    BALLUM_CHARSET_DIGITS,
    BALLUM_CHARSET_ALNUM,
    BALLUM_CHARSET__COUNT
} ballum_charset_t;

const char *ballum_charset_name(ballum_charset_t cs);               /* "ascii", ... */
int         ballum_charset_by_name(const char *name, ballum_charset_t *out);   /* 1 found */
size_t      ballum_charset_size(ballum_charset_t cs);               /* 0 if invalid */
/* The i-th code point of the set, or 0 when out of range. */
uint32_t    ballum_charset_at(ballum_charset_t cs, size_t index);
int         ballum_charset_contains(ballum_charset_t cs, uint32_t codepoint);

/* How a pixel's ink coverage is read from an image. */
typedef enum {
    BALLUM_COVER_ALPHA = 0,      /* the alpha channel                      */
    BALLUM_COVER_DARK_INK,       /* 255 - brightness: dark glyphs on light */
    BALLUM_COVER_LIGHT_INK       /* brightness: light glyphs on dark       */
} ballum_cover_t;

typedef struct ballum_charsource ballum_charsource_t;

ballum_charsource_t *ballum_charsource_create(void);
void                 ballum_charsource_destroy(ballum_charsource_t *cs);
void                 ballum_charsource_clear(ballum_charsource_t *cs);
size_t               ballum_charsource_glyph_count(const ballum_charsource_t *cs);

/* Add one glyph from a coverage bitmap of the whole character cell (w*h bytes,
 * row-major, 0..255). A cell with no ink is a blank glyph (a space). `advance`
 * is the pixels to the next glyph, 0..4*h. A glyph for the same code point and
 * family replaces the earlier one. Limits: cell up to 256x256, 65536 glyphs. */
ballum_status_t ballum_charsource_add_glyph(ballum_charsource_t *cs, uint32_t codepoint, int family,
                                            const uint8_t *coverage, uint32_t w, uint32_t h,
                                            int32_t advance);

/* A pixel map: `pixels` is width*height RGBA, divided into cell_w x cell_h cells.
 * Cell i is the i-th entry of `codepoints` (n_codepoints of them), or of `charset`
 * when codepoints is NULL. Cells beyond the entries are ignored; entries beyond
 * the cells are an error. The advance of each glyph is cell_w. Returns the number
 * of glyphs added through *added (may be NULL). */
typedef struct {
    const ballum_color_t *pixels;
    uint32_t              width, height;
    uint32_t              cell_w, cell_h;
    ballum_charset_t      charset;           /* used when codepoints == NULL */
    const uint32_t       *codepoints;
    size_t                n_codepoints;
    int                   family;            /* 0..15 */
    ballum_cover_t        cover;
} ballum_pixmap_font_t;

ballum_status_t ballum_charsource_add_pixmap(ballum_charsource_t *cs, const ballum_pixmap_font_t *pm,
                                             size_t *added);

/* Read a binary PGM ("P5", 8-bit) image from memory and add it as a pixel map.
 * PGM is the plainest lossless image format, so an atlas can be made by any tool.
 * The same cell/charset rules as ballum_pixmap_font_t apply; `cover` must be
 * DARK_INK or LIGHT_INK (a PGM has no alpha). */
ballum_status_t ballum_charsource_add_pgm(ballum_charsource_t *cs, const uint8_t *data, size_t len,
                                          uint32_t cell_w, uint32_t cell_h, ballum_charset_t charset,
                                          int family, ballum_cover_t cover, size_t *added);

/* The glyph source the interpreter's TEXT and FONT_FAMILY points use. The glyph
 * source borrows `cs`: keep it alive while the interpreter uses it. */
ballum_glyph_source_t ballum_charsource_glyph_source(ballum_charsource_t *cs);

/* ---- recognising glyphs ------------------------------------------------------ */

typedef struct {
    uint32_t codepoint;
    int      family;
    int      score;     /* 0..1000, 1000 = identical pixels                      */
    int      margin;    /* score minus the best score of a DIFFERENT character   */
} ballum_glyph_match_t;

/* Match a coverage bitmap (w*h bytes) against the glyphs held, restricted to
 * `family` (or any family when negative). An image with no ink matches a blank
 * glyph. Returns 1 and fills *out when something was comparable, else 0. */
int ballum_charsource_identify(const ballum_charsource_t *cs, const uint8_t *coverage,
                               uint32_t w, uint32_t h, int family, ballum_glyph_match_t *out);

#ifdef __cplusplus
}
#endif

#endif /* SLEELA_BALLUM_CHARSOURCE_H */
