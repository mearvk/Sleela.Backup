/*
 * ballum/include/sleela_ballum/ballum_interp.h
 * The Ballum interpreter: the stage between a hosted program's pixels and the
 * Ballum surface.
 *
 *   program frame + decoded elements  ->  INTERPRETER  ->  frame shown by Ballum
 *                                            |
 *                                      filter band (Morals, Cleanliness,
 *                                      Throughput, Confidence)
 *
 * MODE (a binary choice, fixed for the life of the interpreter -- "basis
 * stable": decided once, never flips mid-session):
 *   NOTATION    observe and annotate only. The output frame is the input frame,
 *               byte for byte, always. The filter band may read and record;
 *               nothing may write a pixel.
 *   ALTERATION  the output frame is the input frame UNLESS an element is
 *               altered. Only the pixels inside an altered element's rectangle
 *               may differ; every other pixel is copied through unchanged. When
 *               nothing is altered the output equals the input exactly, so
 *               "listening for alteration" still means pixel-identical until a
 *               change is actually made.
 *
 * An ELEMENT is one decoded drawing act the program performed, in the program's
 * own coordinates: a run of text (with its recovered characters where known), a
 * filled rectangle, a copied region, or an image blit. The interpreter does not
 * decode the X protocol itself; a producer (the X11 source, or a test) submits
 * elements for the current frame. The interpreter owns the compositing, the
 * mode invariant, and the filter band.
 *
 * THE FILTER BAND is a clean, technological band-pass over four axes. Each axis
 * scores an element 0..1000; the band passes an alteration only when every axis
 * is within its [low, high] window. An alteration that falls outside any window
 * is rejected and that element is left exactly as the program drew it -- the
 * fail-closed direction is "do not alter". The four axes:
 *   MORALS       is the altered content allowed. Clean-room, lawful, no
 *                deception of the user. (A caller supplies the judgement; the
 *                band only gates on the score it is given.)
 *   CLEANLINESS  is the alteration well-formed: inside the element rectangle,
 *                inside the frame, no partial or malformed pixels.
 *   THROUGHPUT   is the alteration cheap enough for this frame's budget.
 *   CONFIDENCE   how sure the interpreter is of what the element IS (e.g. a
 *                glyph matched a known character with certainty = high).
 *
 * All integer, deterministic, no allocation in the hot path, and it treats the
 * producer's data as untrusted: every rectangle and index is bounds-checked
 * against the frame, and a bad element is dropped, never drawn.
 */
#ifndef SLEELA_BALLUM_INTERP_H
#define SLEELA_BALLUM_INTERP_H

#include "ballum_manager.h"   /* ballum_color_t, ballum_rect_t */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The basis-stable mode. Chosen at creation and never changed. */
typedef enum {
    BALLUM_INTERP_NOTATION = 0,  /* observe/annotate; output == input, always */
    BALLUM_INTERP_ALTERATION     /* output == input unless an element is altered */
} ballum_interp_mode_t;

/* The four filter axes, in band order. */
typedef enum {
    BALLUM_FILTER_MORALS = 0,
    BALLUM_FILTER_CLEANLINESS,
    BALLUM_FILTER_THROUGHPUT,
    BALLUM_FILTER_CONFIDENCE,
    BALLUM_FILTER__COUNT
} ballum_filter_axis_t;

#define BALLUM_SCORE_MIN 0
#define BALLUM_SCORE_MAX 1000

/* One axis window. An element passes the axis when low <= score <= high. */
typedef struct {
    int low;
    int high;
} ballum_filter_window_t;

/* The band: one window per axis. ballum_filter_band_default() opens every axis
 * fully (0..1000) except CONFIDENCE, which starts at 500..1000 so a low-
 * confidence identification does not drive an alteration. */
typedef struct {
    ballum_filter_window_t axis[BALLUM_FILTER__COUNT];
} ballum_filter_band_t;

void ballum_filter_band_default(ballum_filter_band_t *band);
const char *ballum_filter_axis_name(ballum_filter_axis_t axis);

/* The kind of drawing act an element represents. */
typedef enum {
    BALLUM_ELEM_TEXT = 0,   /* a run of characters (chars[] may be recovered)   */
    BALLUM_ELEM_FILL,       /* a solid rectangle                                */
    BALLUM_ELEM_COPY,       /* a region copied from elsewhere in the frame      */
    BALLUM_ELEM_IMAGE,      /* an opaque image blit (pixels only)               */
    BALLUM_ELEM__COUNT
} ballum_elem_kind_t;

/* One decoded element for the current frame, in program/frame coordinates.
 * `rect` is where it draws. For TEXT, `text`/`text_len` are the recovered
 * characters (UTF-8; may be empty when the characters could not be recovered),
 * and `glyph_known` is 1 when every glyph was identified with certainty. The
 * producer owns `text`; the interpreter copies what it needs during submit. */
typedef struct {
    ballum_elem_kind_t kind;
    ballum_rect_t      rect;
    ballum_color_t     fg;           /* text/fill colour                        */
    ballum_color_t     bg;           /* text background (a==0 = transparent)    */
    const char        *text;         /* UTF-8, TEXT only; not NUL-required      */
    size_t             text_len;
    int                glyph_known;  /* 1 = every glyph identified with certainty */
    uint32_t           z;            /* draw order; higher is later/on top      */
} ballum_element_t;

/* Scores a caller attaches to a proposed alteration, one per axis, each 0..1000. */
typedef struct {
    int score[BALLUM_FILTER__COUNT];
} ballum_filter_scores_t;

/* Why an alteration was or was not applied. */
typedef enum {
    BALLUM_ALTER_APPLIED = 0,     /* passed the band and was drawn                */
    BALLUM_ALTER_MODE_NOTATION,   /* interpreter is NOTATION: never alters        */
    BALLUM_ALTER_BLOCKED,         /* an axis fell outside its window              */
    BALLUM_ALTER_OUT_OF_BOUNDS,   /* the replacement rect left the element/frame  */
    BALLUM_ALTER_INVALID,         /* bad argument or value out of range           */
    BALLUM_ALTER_UNSUPPORTED,     /* the contact point (or element kind) cannot be
                                     done here; nothing was drawn                 */
    BALLUM_ALTER_AUDIT_FULL,      /* an audit grade is on and the log is full: the
                                     alteration is refused rather than unrecorded */
    BALLUM_ALTER_FRAME_FULL       /* this frame already holds the most alterations
                                     (or replacement bytes) it can keep         */
} ballum_alter_result_t;

typedef struct ballum_interp ballum_interp_t;

/* ---- lifecycle ------------------------------------------------------------ */

/* Create an interpreter for frames of w x h pixels in the given mode, with a
 * copy of `band` (NULL = the default band). The mode and size are fixed for the
 * interpreter's life. Returns NULL on bad arguments or allocation failure. */
ballum_interp_t *ballum_interp_create(ballum_interp_mode_t mode, uint32_t w, uint32_t h,
                                      const ballum_filter_band_t *band);
void ballum_interp_destroy(ballum_interp_t *it);

ballum_interp_mode_t ballum_interp_mode(const ballum_interp_t *it);
void ballum_interp_set_band(ballum_interp_t *it, const ballum_filter_band_t *band);
void ballum_interp_get_band(const ballum_interp_t *it, ballum_filter_band_t *out);

/* ---- a frame -------------------------------------------------------------- */

/* Begin a frame over `src` (w*h pixels, the exact program capture). The
 * interpreter borrows `src` until end_frame; it must stay valid. Clears the
 * element list. Returns a status (INVALID_ARGUMENT on a NULL/size mismatch). */
ballum_status_t ballum_interp_begin_frame(ballum_interp_t *it, const ballum_color_t *src);

/* Submit one decoded element for this frame. Bounds-checked against the frame;
 * an element whose rect leaves the frame is rejected (INVALID_ARGUMENT) and not
 * recorded. Returns OK when recorded. */
ballum_status_t ballum_interp_submit(ballum_interp_t *it, const ballum_element_t *elem);

size_t ballum_interp_element_count(const ballum_interp_t *it);
int    ballum_interp_element_at(const ballum_interp_t *it, size_t index,
                                ballum_element_t *out);   /* 1 ok, 0 out of range */

/* Propose altering element `index`: fill its rectangle with `replacement`
 * pixels (exactly rect.width*rect.height of them) guarded by `scores` against
 * the band. In NOTATION this always returns BALLUM_ALTER_MODE_NOTATION and
 * draws nothing. In ALTERATION it draws only inside the element's rectangle,
 * and only when every axis passes. The result says what happened. */
ballum_alter_result_t ballum_interp_alter(ballum_interp_t *it, size_t index,
                                          const ballum_color_t *replacement,
                                          size_t replacement_count,
                                          const ballum_filter_scores_t *scores);

/* A convenience over alter(): recolour every identified-character TEXT element
 * whose recovered text is exactly `match` to `fg` (keeps each glyph shape, only
 * swaps the ink colour). Returns how many elements were altered. NOTATION
 * returns 0. The band still gates each one with `scores`. */
size_t ballum_interp_recolor_text(ballum_interp_t *it, const char *match,
                                  ballum_color_t fg, const ballum_filter_scores_t *scores);

/* Finish the frame: compose into `dst` (w*h pixels, may equal nothing aliased
 * with src). In NOTATION dst is a byte-for-byte copy of src. In ALTERATION dst
 * is src with the applied alterations painted in. Returns a status. */
ballum_status_t ballum_interp_end_frame(ballum_interp_t *it, ballum_color_t *dst);


/* ======================================================================== *
 * THE 23 CONTACT POINTS
 *
 * The interface for altering characters, or parts of the GUI, is a fixed set
 * of 23 contact points. Each is one property of an element that a caller may
 * change through ballum_interp_apply() (TEXT, which carries a string, through
 * ballum_interp_apply_text()). They are Ballum's own vocabulary: no national or
 * industry standard is claimed for the set or its numbering.
 *
 * Every point is implemented. All of them work strictly inside the element
 * rectangle: nothing an alteration does can touch a pixel outside it.
 *
 * Each point has a CONFIDENCE rating (ballum_contact_confidence, 0..1000): a
 * design rating of how exactly its result is defined, NOT a measured accuracy.
 *   1000  exact: every output pixel is a defined function of the input pixels
 *    900  exact on the ink coverage, but the ink model is a model (weight, outline)
 *    800  resampling: bilinear scale or shear of the element's own pixels
 *    700  segmentation: assumes glyphs/lines are separated by blank pixels
 *    600  depends on the quality of a caller-supplied glyph source
 * ballum_filter_scores_capped() lowers the Confidence axis of a score set to the
 * point's rating, so a band with a Confidence floor can refuse the lower-rated
 * points automatically.
 *
 *   point          rating  value                              colour
 *   COLOR_FG        1000   -                                  ink colour
 *   COLOR_BG        1000   -                                  paper colour
 *   OPACITY         1000   0..100 % toward the paper          -
 *   BRIGHTNESS      1000   -100..100                          -
 *   CONTRAST        1000   -100..100                          -
 *   SATURATION      1000   0..200 (100 = unchanged)           -
 *   VISIBILITY      1000   0 hide, 1 restore the original     -
 *   EFFECT          1000   0 none 1 invert 2 gray 3 sepia     -
 *   BLUR            1000   radius 1..8 px                     -
 *   UNDERLINE       1000   thickness 1..4 px                  line colour
 *   STRIKETHROUGH   1000   thickness 1..4 px                  line colour
 *   WEIGHT           900   -3..3 px of ink growth/shrink      -
 *   OUTLINE          900   thickness 1..3 px                  outline colour
 *   CRYPTO_GRADE    1000   0 none 1 digest 2 keyed chain      (session setting)
 *   POSITION_X      1000   -width..width px, content moves    -
 *   POSITION_Y      1000   -height..height px                 -
 *   Z_ORDER         1000   0..255, higher draws later (on top) -
 *   SIZE             800   25..400 %, about the element centre -
 *   SLANT            800   -100..100 shear %, baseline fixed   -
 *   SPACING          700   -4..32 px between glyphs            -
 *   LINE_HEIGHT      700   -8..64 px between lines             -
 *   TEXT             600   new characters (apply_text)         -
 *   FONT_FAMILY      600   0..15, passed to the glyph source   -
 *
 * Notes on what each one is:
 *   - SIZE, SLANT, SPACING and LINE_HEIGHT work on the element's own pixels (no
 *     font is involved): SIZE resamples bilinearly, SLANT shears, SPACING and
 *     LINE_HEIGHT cut the element at blank columns/rows and move the pieces.
 *     They are pixel operations, not glyph re-rendering, and the rating says so.
 *   - TEXT and FONT_FAMILY re-render characters, which needs a GLYPH SOURCE that
 *     the caller sets with ballum_interp_set_glyph_source(). Without one they
 *     return UNSUPPORTED (ballum_contact_available() says so up front).
 *   - POSITION_X/Y move the element's content INSIDE its rectangle; what moves
 *     out is clipped and the exposed area takes the paper colour.
 *   - Z_ORDER changes the order in which the frame's alterations are applied
 *     (by element z, then by when they were made), so where two altered elements
 *     overlap, the higher z shows on top. The frame is rebuilt from the program
 *     frame in that order.
 *   - Points that need the element's paper colour (opacity, size, slant,
 *     spacing, line-height, position, visibility 0, text) return UNSUPPORTED for
 *     an element without an opaque one; points that need ink (colors, weight,
 *     outline) also need an ink colour that differs from the paper. Nothing is
 *     guessed.
 *
 * CRYPTO_GRADE is about the record of what was altered, not about secrecy:
 *   0  no record
 *   1  every applied alteration appends an audit entry holding the SHA-256
 *      (FIPS 180-4) of the resulting element pixels
 *   2  as 1, and the entries form a keyed chain: each entry carries
 *      HMAC-SHA-256 (FIPS 198-1) over the previous MAC and the entry's own
 *      fields, so a removed, reordered or edited entry breaks verification
 * Both use the repository's crypto/ module, which its own notes describe as
 * correct reference code and not hardened or constant-time: the audit trail is
 * tamper-EVIDENT for a holder of the key, not a defence against a local attacker
 * who can read the key. While a grade above 0 is on, an alteration is refused
 * (AUDIT_FULL) rather than applied unrecorded when the log is full. A digest is
 * of the element pixels at the moment of that alteration; a later Z_ORDER
 * rebuild can change them, and records its own entry.
 * ======================================================================== */
typedef enum {
    BALLUM_CONTACT_COLOR_FG = 0,
    BALLUM_CONTACT_COLOR_BG,
    BALLUM_CONTACT_OPACITY,
    BALLUM_CONTACT_BRIGHTNESS,
    BALLUM_CONTACT_CONTRAST,
    BALLUM_CONTACT_SATURATION,
    BALLUM_CONTACT_VISIBILITY,
    BALLUM_CONTACT_EFFECT,
    BALLUM_CONTACT_BLUR,
    BALLUM_CONTACT_UNDERLINE,
    BALLUM_CONTACT_STRIKETHROUGH,
    BALLUM_CONTACT_WEIGHT,
    BALLUM_CONTACT_CRYPTO_GRADE,
    BALLUM_CONTACT_SIZE,
    BALLUM_CONTACT_SLANT,
    BALLUM_CONTACT_TEXT,
    BALLUM_CONTACT_SPACING,
    BALLUM_CONTACT_LINE_HEIGHT,
    BALLUM_CONTACT_FONT_FAMILY,
    BALLUM_CONTACT_OUTLINE,
    BALLUM_CONTACT_POSITION_X,
    BALLUM_CONTACT_POSITION_Y,
    BALLUM_CONTACT_Z_ORDER,
    BALLUM_CONTACT__COUNT          /* == 23 */
} ballum_contact_t;

enum { BALLUM_CRYPTO_NONE = 0, BALLUM_CRYPTO_DIGEST = 1, BALLUM_CRYPTO_CHAIN = 2 };

typedef struct {
    ballum_contact_t point;
    int32_t          value;      /* meaning per point, see the table above */
    ballum_color_t   color;      /* COLOR_*, UNDERLINE, STRIKETHROUGH      */
} ballum_change_t;

const char *ballum_contact_name(ballum_contact_t point);          /* "color-fg", ... */
int         ballum_contact_implemented(ballum_contact_t point);   /* 1 for all 23; -1 if invalid */
int         ballum_contact_by_name(const char *name, ballum_contact_t *out);   /* 1 found */
/* Design confidence of the point, 0..1000 (see the table above); -1 if invalid. */
int         ballum_contact_confidence(ballum_contact_t point);
/* Is the point usable right now on this interpreter? 0 for TEXT and FONT_FAMILY
 * until a glyph source is set, 1 otherwise; -1 if invalid. */
int         ballum_contact_available(const ballum_interp_t *it, ballum_contact_t point);
/* `base` with its Confidence axis lowered to the point's rating (never raised). */
ballum_filter_scores_t ballum_filter_scores_capped(const ballum_filter_scores_t *base,
                                                   ballum_contact_t point);

/* A GLYPH SOURCE turns characters into pixels for TEXT and FONT_FAMILY. render()
 * draws code point `codepoint` in font `family` for a line `height` pixels tall:
 * it fills `coverage` (at most `cap` bytes, row-major, 0..255 per pixel) and sets
 * *w, *h (h <= height, w*h <= cap) and *advance (pixels to the next glyph, 0 to
 * 4*height). It returns 1 on success and 0 when it has no glyph. The glyph's top
 * is placed on the element's top row. The source is the caller's: Ballum ships
 * none, so no font licence is bundled. */
typedef struct {
    int   (*render)(void *user, uint32_t codepoint, int family, uint32_t height,
                    uint8_t *coverage, size_t cap, uint32_t *w, uint32_t *h, int32_t *advance);
    void  *user;
} ballum_glyph_source_t;

ballum_status_t ballum_interp_set_glyph_source(ballum_interp_t *it, const ballum_glyph_source_t *src);

/* TEXT: replace the element's characters (UTF-8, at most 4096 bytes). The element
 * is cleared to its paper colour and the text drawn in its ink colour with the
 * glyph source. Same band gating, audit and mode rules as ballum_interp_apply. */
ballum_alter_result_t ballum_interp_apply_text(ballum_interp_t *it, size_t index,
                                               const char *text, size_t text_len,
                                               const ballum_filter_scores_t *scores);

/* Apply one change to element `index`, guarded by the filter band. NOTATION
 * returns MODE_NOTATION and draws nothing. For CRYPTO_GRADE the index is ignored
 * (it changes the session, not an element) and the band still gates it. */
ballum_alter_result_t ballum_interp_apply(ballum_interp_t *it, size_t index,
                                          const ballum_change_t *change,
                                          const ballum_filter_scores_t *scores);

/* ---- audit trail (CRYPTO_GRADE 1 and 2) ---------------------------------- */

typedef struct {
    uint64_t         seq;           /* 1-based, continues across frames        */
    uint64_t         frame;         /* frame number the alteration happened in */
    size_t           element;       /* element index within that frame         */
    int              point;         /* ballum_contact_t, or -1 for a raw alter */
    ballum_rect_t    rect;
    uint8_t          digest[32];    /* SHA-256 of the resulting rect pixels (RGBA, row-major) */
    uint8_t          mac[32];       /* chain MAC at grade 2, else all zero      */
} ballum_audit_entry_t;

/* Key for grade 2 (1..64 bytes, copied). Required before grade 2 can be set. */
ballum_status_t ballum_interp_set_audit_key(ballum_interp_t *it, const uint8_t *key, size_t len);
int    ballum_interp_crypto_grade(const ballum_interp_t *it);
size_t ballum_interp_audit_count(const ballum_interp_t *it);
int    ballum_interp_audit_at(const ballum_interp_t *it, size_t index, ballum_audit_entry_t *out);
/* Recompute the chain with `key` and compare: 1 intact, 0 broken or no chain. */
int    ballum_interp_audit_verify(const ballum_interp_t *it, const uint8_t *key, size_t len);

/* A 32-byte summary of the whole trail so far, for signing (see ballum_lms.h):
 * SHA-256 of "BALLUM-AUDIT-HEAD" || entry count (8 bytes, big endian) || the last
 * entry's chain MAC at grade 2, or its digest at grade 1. Returns 1 and writes
 * `out`, or 0 when there are no entries. At grade 2 the MAC chains every earlier
 * entry, so signing the head covers the trail; at grade 1 it covers the last entry
 * only, and entries are not chained. */
int    ballum_interp_audit_head(const ballum_interp_t *it, uint8_t out[32]);

/* ---- input follows the picture ------------------------------------------------ */

/* Some alterations MOVE what an element shows: position-x, position-y, size and
 * slant. A pointer event at a place on the altered picture must reach the place in
 * the program's own picture that is drawn there, or a click on moved text would hit
 * whatever the program has at the old position. Given a point on the SHOWN frame,
 * this finds the topmost element that was moved and undoes its moves (in reverse
 * order) to give the point in the program's frame. Returns 1 when a move was undone,
 * 0 when the point is returned unchanged (nothing moved there, or NOTATION).
 *
 * Not undone, and said so: spacing and line-height (the pieces move by amounts that
 * depend on the artwork), text and font-family (a re-rendered line has new glyph
 * positions), blur and the colour points (they do not move anything). The result is
 * clamped into the element, so a point on paper exposed by a move maps to the
 * nearest edge of the original. */
int ballum_interp_map_point(const ballum_interp_t *it, int32_t x, int32_t y, int32_t *ox, int32_t *oy);

/* ---- reading colours off the frame ------------------------------------------- */

/* An element decoded from drawing requests may not know its colours: a RENDER
 * glyph run composites over whatever was there, so its paper is unknown (bg.a ==
 * 0), and a glyph drawn through a non-solid source has no known ink (fg.a == 0).
 * This reads them off the program's own frame:
 *   paper = the most common colour in a one pixel ring just OUTSIDE the element
 *           (inside the element when it touches the frame edge); it must be at
 *           least 60% of the ring, otherwise the paper stays unknown;
 *   ink   = the pixel inside the element farthest from the paper; if the element
 *           holds nothing but paper the ink stays unknown.
 * Only unknown colours are filled in; a known colour is never overwritten. Returns
 * the number of colours it set (0..2). The ink and paper points (color-fg,
 * color-bg, weight, outline) need both. */
int ballum_element_infer_colors(const ballum_color_t *frame, uint32_t frame_w, uint32_t frame_h,
                                ballum_element_t *e);

/* ---- queries -------------------------------------------------------------- */

/* Counters since create(), for the Throughput/Confidence accounting and tests. */
typedef struct {
    uint64_t frames;
    uint64_t elements;
    uint64_t alterations_applied;
    uint64_t alterations_blocked;
    uint64_t pixels_altered;
} ballum_interp_stats_t;

void ballum_interp_stats(const ballum_interp_t *it, ballum_interp_stats_t *out);
const char *ballum_interp_mode_name(ballum_interp_mode_t mode);
const char *ballum_alter_result_name(ballum_alter_result_t r);

#ifdef __cplusplus
}
#endif

#endif /* SLEELA_BALLUM_INTERP_H */
