#include "ballum_vm_bridge.h"
#include "../../ballum/include/sleela_ballum/ballum_manager.h"
#include "../../ballum/include/sleela_ballum/ballum_image.h"
#include "../../ballum/include/sleela_ballum/ballum_effects.h"
#include "../../ballum/include/sleela_ballum/ballum_host.h"
#include "../../ballum/include/sleela_ballum/ballum_host_profiles.h"
#include "../../ballum/include/sleela_ballum/ballum_charsource.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * One process-global Ballum manager over the compiled-in per-OS backend, lazily
 * initialized on first use. The cached display list is owned here; names are
 * copied into a small static buffer at the boundary so the VM never holds a
 * pointer into native storage.
 */
static ballum_manager_t g_manager;
static ballum_display_t g_displays[BALLUM_MAX_DISPLAYS];
static size_t           g_count;
static int              g_initialized;
static char             g_name[BALLUM_DISPLAY_NAME_BYTES];

static int ensure_manager(void) {
    ballum_status_t status;
    if (g_initialized) return BALLUM_STATUS_OK;
    status = ballum_manager_init(&g_manager, ballum_backend_default(), NULL);
    if (status == BALLUM_STATUS_OK) g_initialized = 1;
    return (int)status;
}

static const ballum_display_t *display_at(int index) {
    return index >= 0 && (size_t)index < g_count ? &g_displays[index] : NULL;
}

int sleela_ballum_vm_supported(void) { return ballum_backend_supported(); }

const char *sleela_ballum_vm_backend_name(void) {
    const ballum_backend_t *b = ballum_backend_default();
    return (b && b->name) ? b->name : "unavailable";
}

int sleela_ballum_vm_enumerate(void) {
    size_t count = 0u;
    int init = ensure_manager();
    ballum_status_t status;
    if (init != BALLUM_STATUS_OK) return init;
    status = ballum_manager_enumerate(&g_manager, g_displays, BALLUM_MAX_DISPLAYS, &count);
    if (status == BALLUM_STATUS_OK || status == BALLUM_STATUS_BUFFER_TOO_SMALL)
        g_count = count > BALLUM_MAX_DISPLAYS ? BALLUM_MAX_DISPLAYS : count;
    else
        g_count = 0u;
    return (int)status;
}

int sleela_ballum_vm_display_count(void) { return (int)g_count; }

int sleela_ballum_vm_display_id(int index) {
    const ballum_display_t *d = display_at(index);
    return d ? (int)d->id : -1;
}

const char *sleela_ballum_vm_display_name(int index) {
    const ballum_display_t *d = display_at(index);
    if (!d) return "";
    memcpy(g_name, d->name, sizeof(g_name));
    g_name[sizeof(g_name) - 1u] = '\0';
    return g_name;
}

int sleela_ballum_vm_display_width(int index) {
    const ballum_display_t *d = display_at(index);
    return d ? (int)d->width : 0;
}

int sleela_ballum_vm_display_height(int index) {
    const ballum_display_t *d = display_at(index);
    return d ? (int)d->height : 0;
}

int sleela_ballum_vm_display_is_primary(int index) {
    const ballum_display_t *d = display_at(index);
    return d ? (d->is_primary ? 1 : 0) : -1;
}

int sleela_ballum_vm_display_caps(int index) {
    const ballum_display_t *d = display_at(index);
    return d ? (int)d->capabilities : 0;
}

static int clamp_byte(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

int sleela_ballum_vm_set_pixel(int display_id, int x, int y,
                               int r, int g, int b, int a) {
    ballum_point_t at;
    ballum_color_t color;
    int init = ensure_manager();
    if (init != BALLUM_STATUS_OK) return init;
    if (x < 0 || y < 0) return BALLUM_STATUS_OUT_OF_BOUNDS;
    at.x = (uint32_t)x;
    at.y = (uint32_t)y;
    color = ballum_color_rgba((uint8_t)clamp_byte(r), (uint8_t)clamp_byte(g),
                              (uint8_t)clamp_byte(b), (uint8_t)clamp_byte(a));
    return (int)ballum_manager_set_pixel(&g_manager, (uint32_t)display_id, at, color);
}

int sleela_ballum_vm_get_pixel(int display_id, int x, int y) {
    ballum_point_t at;
    ballum_color_t out;
    int init = ensure_manager();
    if (init != BALLUM_STATUS_OK) return -1;
    if (x < 0 || y < 0) return -1;
    at.x = (uint32_t)x;
    at.y = (uint32_t)y;
    if (ballum_manager_get_pixel(&g_manager, (uint32_t)display_id, at, &out) != BALLUM_STATUS_OK)
        return -1;
    return ((int)out.a << 24) | ((int)out.r << 16) | ((int)out.g << 8) | (int)out.b;
}

int sleela_ballum_vm_set_brightness(int display_id, int percent) {
    int init = ensure_manager();
    if (init != BALLUM_STATUS_OK) return init;
    return (int)ballum_manager_set_brightness(&g_manager, (uint32_t)display_id, percent);
}

int sleela_ballum_vm_set_warmth(int display_id, int kelvin) {
    int init = ensure_manager();
    if (init != BALLUM_STATUS_OK) return init;
    if (kelvin < 0) kelvin = 0;
    return (int)ballum_manager_set_warmth(&g_manager, (uint32_t)display_id, (uint32_t)kelvin);
}

int sleela_ballum_vm_set_gamma(int display_id, int gamma_scaled) {
    int init = ensure_manager();
    if (init != BALLUM_STATUS_OK) return init;
    if (gamma_scaled < 0) gamma_scaled = 0;
    return (int)ballum_manager_set_gamma(&g_manager, (uint32_t)display_id, (uint32_t)gamma_scaled);
}

const char *sleela_ballum_vm_status_name(int status) {
    return ballum_status_name((ballum_status_t)status);
}

/* ---- fullscreen surface (total per-pixel control) ------------------------ */
/* Claim an override-redirect fullscreen framebuffer over the display; while
 * claimed, set_pixel/get_pixel and logo_blend write to the framebuffer (fast,
 * no X round-trips) and present() pushes the whole frame in one shot. */
int sleela_ballum_vm_has_surface(int display_id) {
    if (ensure_manager() != BALLUM_STATUS_OK) return 0;
    return ballum_manager_has_surface(&g_manager, (uint32_t)display_id) == 1 ? 1 : 0;
}

int sleela_ballum_vm_claim_surface(int display_id) {
    int init = ensure_manager();
    if (init != BALLUM_STATUS_OK) return init;
    return (int)ballum_manager_claim_surface(&g_manager, (uint32_t)display_id);
}

int sleela_ballum_vm_present(int display_id) {
    int init = ensure_manager();
    if (init != BALLUM_STATUS_OK) return init;
    return (int)ballum_manager_present(&g_manager, (uint32_t)display_id);
}

int sleela_ballum_vm_release_surface(int display_id) {
    int init = ensure_manager();
    if (init != BALLUM_STATUS_OK) return init;
    return (int)ballum_manager_release_surface(&g_manager, (uint32_t)display_id);
}

/* ---- pointer input ------------------------------------------------------- */
/* The most recently polled event, latched so the SLeeLa side can read its
 * fields after poll() reports the type. */
static ballum_pointer_event_t g_event;

int sleela_ballum_vm_has_input(void) {
    if (ensure_manager() != BALLUM_STATUS_OK) return 0;
    return ballum_manager_has_input(&g_manager) == 1 ? 1 : 0;
}

int sleela_ballum_vm_poll(void) {
    ballum_pointer_event_t ev;
    if (ensure_manager() != BALLUM_STATUS_OK) { g_event.type = BALLUM_EVENT_NONE; return BALLUM_EVENT_NONE; }
    if (ballum_manager_poll_event(&g_manager, &ev) != BALLUM_STATUS_OK) {
        g_event.type = BALLUM_EVENT_NONE;
        return BALLUM_EVENT_NONE;
    }
    g_event = ev;
    return (int)g_event.type;
}

int sleela_ballum_vm_event_x(void)         { return (int)g_event.x; }
int sleela_ballum_vm_event_y(void)         { return (int)g_event.y; }
int sleela_ballum_vm_event_button(void)    { return (int)g_event.button; }
int sleela_ballum_vm_event_source(void)    { return (int)g_event.source; }
int sleela_ballum_vm_event_contact(void)   { return (int)g_event.contact_id; }
int sleela_ballum_vm_event_modifiers(void) { return (int)g_event.modifiers; }
const char *sleela_ballum_vm_event_type_name(int type) {
    return ballum_event_type_name((ballum_event_type_t)type);
}

/* ---- keyboard input ------------------------------------------------------ */
int sleela_ballum_vm_has_keyboard(void) {
    if (ensure_manager() != BALLUM_STATUS_OK) return 0;
    return ballum_manager_has_keyboard(&g_manager) == 1 ? 1 : 0;
}

int sleela_ballum_vm_poll_key(void) {
    int key = 0;
    if (ensure_manager() != BALLUM_STATUS_OK) return 0;
    if (ballum_manager_poll_key(&g_manager, &key) != BALLUM_STATUS_OK) return 0;
    return key;
}

/* ---- logo splash: real image handler + effects --------------------------
 * The bridge now decodes a logo (JPEG via the bundled nanojpeg, through
 * ballum_image) and composites it onto the display with a selectable transition
 * effect (ballum_effects) for a given progress. A session loads the logo once
 * (uiBallumLogoLoad), picks an effect (uiBallumLogoSetEffect), then calls
 * logo_blend each frame. Fail closed: if no logo is loaded, logo_ready()==0 and
 * logo_blend is a clean no-op, so the white fill + timeline + ESC still run and
 * nothing false is drawn. */
static ballum_image_t g_logo;
static int            g_logo_loaded;
static ballum_fx_t    g_logo_fx = BALLUM_FX_FADE;

int sleela_ballum_vm_logo_load(const char *path) {
    if (!path || path[0] == '\0') return (int)BALLUM_STATUS_INVALID_ARGUMENT;
    if (g_logo_loaded) { ballum_image_free(&g_logo); g_logo_loaded = 0; }
    ballum_image_init(&g_logo);
    ballum_status_t st = ballum_image_load_file(&g_logo, path);
    g_logo_loaded = (st == BALLUM_STATUS_OK && ballum_image_ready(&g_logo)) ? 1 : 0;
    return (int)st;
}

int sleela_ballum_vm_logo_set_effect(int fx) {
    if (fx < 0 || fx >= (int)BALLUM_FX__COUNT) return (int)BALLUM_STATUS_INVALID_ARGUMENT;
    g_logo_fx = (ballum_fx_t)fx;
    return (int)BALLUM_STATUS_OK;
}

int sleela_ballum_vm_logo_ready(void) {
    return (g_logo_loaded && ballum_image_ready(&g_logo)) ? 1 : 0;
}

int sleela_ballum_vm_logo_blend(int display_id, int opacity_percent,
                                int screen_w, int screen_h,
                                int bg_r, int bg_g, int bg_b) {
    if (!sleela_ballum_vm_logo_ready()) return (int)BALLUM_STATUS_UNSUPPORTED;
    if (ensure_manager() != BALLUM_STATUS_OK) return (int)BALLUM_STATUS_BACKEND_UNAVAILABLE;
    if (screen_w <= 0 || screen_h <= 0) return (int)BALLUM_STATUS_INVALID_ARGUMENT;

    ballum_color_t bg = ballum_color_rgb((uint8_t)bg_r, (uint8_t)bg_g, (uint8_t)bg_b);
    int t = opacity_percent < 0 ? 0 : (opacity_percent > 100 ? 100 : opacity_percent);

    /* Composite the whole frame: for each pixel, sample the logo fitted/centered
     * on the display, run the selected effect at progress t, and plot it. */
    for (int y = 0; y < screen_h; ++y) {
        for (int x = 0; x < screen_w; ++x) {
            ballum_color_t fg = ballum_image_sample_fit(&g_logo, screen_w, screen_h, x, y);
            ballum_color_t out = ballum_fx_pixel(g_logo_fx, t, screen_w, screen_h, x, y, fg, bg);
            ballum_point_t at; at.x = x; at.y = y;
            ballum_manager_set_pixel(&g_manager, (uint32_t)display_id, at, out);
        }
    }
    return (int)BALLUM_STATUS_OK;
}

/* ---- effect catalog passthroughs ---------------------------------------- */
int sleela_ballum_vm_fx_count(void) { return ballum_fx_count(); }
const char *sleela_ballum_vm_fx_name(int fx) { return ballum_fx_name((ballum_fx_t)fx); }
int sleela_ballum_vm_fx_by_name(const char *name) { return (int)ballum_fx_by_name(name); }

/* ---- program host ---------------------------------------------------------- */

#define HOST_MAX_ARGS 62
#define HOST_ARG_BYTES 4096

static ballum_host_t        *g_host;
static ballum_host_source_t  g_host_source;
static void                 *g_host_source_ctx;
static int                   g_host_header = 32, g_host_enter_ms = 700, g_host_max_s;
/* owned copy of a custom program's argv (the source copies it again) */
static char                  g_host_argv_buf[HOST_ARG_BYTES];
static const char           *g_host_argv[HOST_MAX_ARGS + 2];

static uint64_t host_now_ms(void) {
#if defined(CLOCK_MONOTONIC)
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
        return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
#endif
    return (uint64_t)clock() * 1000u / (uint64_t)CLOCKS_PER_SEC;
}

int sleela_ballum_vm_host_profile_count(void) { return (int)ballum_profile_count(); }

const char *sleela_ballum_vm_host_profile_name(int index) {
    const ballum_profile_t *p = index >= 0 ? ballum_profile_at((size_t)index) : NULL;
    return p ? p->name : "";
}

const char *sleela_ballum_vm_host_profile_title(int index) {
    const ballum_profile_t *p = index >= 0 ? ballum_profile_at((size_t)index) : NULL;
    return p ? p->title : "";
}

int sleela_ballum_vm_host_transition_count(void) { return ballum_transition_count(); }

const char *sleela_ballum_vm_host_transition_name(int index) {
    const char *n = ballum_transition_name_at(index);
    return n ? n : "";
}

int sleela_ballum_vm_host_configure(int header_height, int enter_ms, int max_seconds) {
    if (header_height < 0 || header_height > 512 || enter_ms < 0 || enter_ms > 10000 ||
        max_seconds < 0 || max_seconds > 86400 * 7)
        return (int)BALLUM_STATUS_INVALID_ARGUMENT;
    g_host_header = header_height; g_host_enter_ms = enter_ms; g_host_max_s = max_seconds;
    return (int)BALLUM_STATUS_OK;
}


/* ---- observation, character source, interpreter rules ---------------------------- */

#define RULES_MAX      32
#define SNAP_MAX       256
#define SNAP_TEXT      96
#define PGM_MAX_BYTES  (16u * 1024u * 1024u)

typedef struct {
    char             match[SNAP_TEXT];   /* exact text, or "*" for any text element */
    ballum_change_t  change;
} bridge_rule_t;

static ballum_charsource_t  *g_chars;
static int                   g_observe;
static int                   g_interp_mode;                 /* 0 off, 1 notation, 2 alteration */
static ballum_filter_band_t  g_band;
static int                   g_band_set;
static ballum_filter_scores_t g_scores = { { 900, 900, 900, 900 } };
static bridge_rule_t         g_rules[RULES_MAX];
static int                   g_nrules;

static struct { int kind, x, y, w, h, known; char text[SNAP_TEXT]; } g_snap[SNAP_MAX];
static int                   g_nsnap;
static ballum_interp_stats_t g_istats;

/* Runs inside the host for every interpreted frame: remember what is on screen so
 * SLeeLa can read it, then apply the standing rules. */
static void bridge_interp_hook(void *user, ballum_interp_t *it, uint64_t now_ms) {
    (void)user; (void)now_ms;
    size_t n = ballum_interp_element_count(it);
    g_nsnap = 0;
    for (size_t i = 0; i < n && g_nsnap < SNAP_MAX; ++i) {
        ballum_element_t e;
        if (!ballum_interp_element_at(it, i, &e)) continue;
        int k = g_nsnap++;
        g_snap[k].kind = (int)e.kind;
        g_snap[k].x = (int)e.rect.x; g_snap[k].y = (int)e.rect.y;
        g_snap[k].w = (int)e.rect.width; g_snap[k].h = (int)e.rect.height;
        g_snap[k].known = e.glyph_known ? 1 : 0;
        size_t tl = e.text_len < SNAP_TEXT - 1u ? e.text_len : SNAP_TEXT - 1u;
        if (e.text && tl) memcpy(g_snap[k].text, e.text, tl);
        g_snap[k].text[tl] = '\0';
    }
    if (ballum_interp_mode(it) == BALLUM_INTERP_ALTERATION) {
        for (int r = 0; r < g_nrules; ++r) {
            size_t mlen = strlen(g_rules[r].match);
            ballum_filter_scores_t sc = ballum_filter_scores_capped(&g_scores, g_rules[r].change.point);
            for (size_t i = 0; i < n; ++i) {
                ballum_element_t e;
                if (!ballum_interp_element_at(it, i, &e) || e.kind != BALLUM_ELEM_TEXT) continue;
                int hit = (mlen == 1 && g_rules[r].match[0] == '*') ||
                          (e.glyph_known && e.text_len == mlen && e.text && memcmp(e.text, g_rules[r].match, mlen) == 0);
                if (hit) ballum_interp_apply(it, i, &g_rules[r].change, &sc);
            }
        }
    }
    ballum_interp_stats(it, &g_istats);
}

static void host_release(void) {
    if (g_host) { ballum_host_destroy(g_host); g_host = NULL; }
    if (g_host_source_ctx) { ballum_host_source_x11_destroy(g_host_source_ctx); g_host_source_ctx = NULL; }
    memset(&g_host_source, 0, sizeof(g_host_source));
}

static int host_begin(int display_id, const ballum_program_t *prog, const char *enter,
                      const char *leave, const char *def_enter, const char *def_leave) {
    int st = sleela_ballum_vm_enumerate();
    if (st != (int)BALLUM_STATUS_OK) return st;
    if (g_host && ballum_host_state(g_host) != BALLUM_HOST_DONE &&
        ballum_host_state(g_host) != BALLUM_HOST_IDLE)
        return (int)BALLUM_STATUS_INVALID_ARGUMENT;              /* one session at a time */
    host_release();

    ballum_host_config_t cfg;
    ballum_host_config_default(&cfg, (uint32_t)display_id);
    cfg.header_height = (uint32_t)g_host_header;
    cfg.enter_ms = (uint32_t)g_host_enter_ms;
    cfg.max_runtime_ms = (uint64_t)g_host_max_s * 1000u;
    const char *en = (enter && *enter) ? enter : def_enter;
    const char *lv = (leave && *leave) ? leave : def_leave;
    if (g_interp_mode > 0) {
        cfg.interpret = 1;
        cfg.interp_mode = g_interp_mode == 2 ? BALLUM_INTERP_ALTERATION : BALLUM_INTERP_NOTATION;
        if (g_band_set) cfg.interp_band = g_band;
    }
    if (en && !ballum_transition_by_name(en, &cfg.enter)) return (int)BALLUM_STATUS_INVALID_ARGUMENT;
    if (lv && !ballum_transition_by_name(lv, &cfg.leave)) return (int)BALLUM_STATUS_INVALID_ARGUMENT;

    st = (int)ballum_host_source_x11_create(prog, &g_host_source, &g_host_source_ctx);
    if (st != (int)BALLUM_STATUS_OK) { host_release(); return st; }
    st = (int)ballum_host_create(&g_manager, &cfg, &g_host_source, g_host_source_ctx, &g_host);
    if (st != (int)BALLUM_STATUS_OK) { host_release(); return st; }
    if (g_chars) ballum_host_source_x11_set_charsource(g_host_source_ctx, g_chars);
    if (g_interp_mode > 0) ballum_host_set_interpreter_hook(g_host, bridge_interp_hook, NULL);
    g_nsnap = 0;
    st = (int)ballum_host_start(g_host, host_now_ms());
    if (st != (int)BALLUM_STATUS_OK) {
        ballum_host_stop(g_host);       /* release anything half-started; keep the host so
                                           host_state()/host_exit_reason() stay readable */
        return st;
    }
    return (int)BALLUM_STATUS_OK;
}

int sleela_ballum_vm_host_start(int display_id, const char *profile, const char *enter,
                                const char *leave) {
    const ballum_profile_t *p = ballum_profile_find(profile);
    if (!p) return (int)BALLUM_STATUS_INVALID_ARGUMENT;
    ballum_program_t prog;
    memset(&prog, 0, sizeof(prog));
    prog.argv = p->argv; prog.title = p->title; prog.sandbox = 1; prog.log_fd = -1; prog.observe = g_observe;
    return host_begin(display_id, &prog, enter, leave, p->enter, p->leave);
}

int sleela_ballum_vm_host_start_program(int display_id, const char *program, const char *args,
                                        const char *enter, const char *leave) {
    if (!program || !*program) return (int)BALLUM_STATUS_INVALID_ARGUMENT;
    size_t plen = strlen(program), alen = args ? strlen(args) : 0u;
    if (plen + alen + 4u > sizeof(g_host_argv_buf)) return (int)BALLUM_STATUS_INVALID_ARGUMENT;
    /* argv[0] = program, then the space-separated words of `args`; no shell, no quoting */
    char *w = g_host_argv_buf;
    size_t n = 0;
    memcpy(w, program, plen + 1u);
    g_host_argv[n++] = w;
    w += plen + 1u;
    if (args) {
        memcpy(w, args, alen + 1u);
        char *c = w;
        while (*c) {
            while (*c == ' ' || *c == '\t') *c++ = '\0';
            if (!*c) break;
            if (n >= HOST_MAX_ARGS) return (int)BALLUM_STATUS_INVALID_ARGUMENT;
            g_host_argv[n++] = c;
            while (*c && *c != ' ' && *c != '\t') ++c;
        }
    }
    g_host_argv[n] = NULL;
    ballum_program_t prog;
    memset(&prog, 0, sizeof(prog));
    prog.argv = g_host_argv; prog.title = program; prog.sandbox = 1; prog.log_fd = -1; prog.observe = g_observe;
    return host_begin(display_id, &prog, enter, leave, "disintegrate", "fade");
}

int sleela_ballum_vm_host_step(void) {
    if (!g_host) return (int)BALLUM_HOST_IDLE;
    ballum_host_step(g_host, host_now_ms());      /* a device failure ends the session; read exit_reason */
    return (int)ballum_host_state(g_host);
}

int sleela_ballum_vm_host_state(void) { return g_host ? (int)ballum_host_state(g_host) : (int)BALLUM_HOST_IDLE; }

const char *sleela_ballum_vm_host_state_name(int state) {
    return ballum_host_state_name((ballum_host_state_t)state);
}

int sleela_ballum_vm_host_exit_reason(void) {
    return g_host ? (int)ballum_host_exit_reason(g_host) : (int)BALLUM_EXIT_NONE;
}

const char *sleela_ballum_vm_host_exit_reason_name(int reason) {
    return ballum_exit_reason_name((ballum_exit_reason_t)reason);
}

int sleela_ballum_vm_host_stop(void) {
    if (g_host) ballum_host_stop(g_host);
    host_release();
    return (int)BALLUM_STATUS_OK;
}

/* ---- character source ---------------------------------------------------------------- */

int sleela_ballum_vm_charset_count(void) { return (int)BALLUM_CHARSET__COUNT; }
const char *sleela_ballum_vm_charset_name(int i) {
    return (i >= 0 && i < (int)BALLUM_CHARSET__COUNT) ? ballum_charset_name((ballum_charset_t)i) : "";
}
int sleela_ballum_vm_charset_size(int i) {
    return (i >= 0 && i < (int)BALLUM_CHARSET__COUNT) ? (int)ballum_charset_size((ballum_charset_t)i) : 0;
}

static int not_running(void);

static ballum_charsource_t *chars_get(void) {
    if (!g_chars) g_chars = ballum_charsource_create();
    return g_chars;
}

int sleela_ballum_vm_charsource_clear(void) {
    if (!not_running()) return (int)BALLUM_STATUS_INVALID_ARGUMENT;      /* the session reads it from another thread */
    if (g_chars) ballum_charsource_clear(g_chars);
    return (int)BALLUM_STATUS_OK;
}

int sleela_ballum_vm_charsource_glyph_count(void) { return g_chars ? (int)ballum_charsource_glyph_count(g_chars) : 0; }

int sleela_ballum_vm_charsource_load_pgm(const char *path, int cell_w, int cell_h, const char *charset,
                                         int family, int light_ink) {
    ballum_charset_t cs;
    if (!not_running()) return (int)BALLUM_STATUS_INVALID_ARGUMENT;
    if (!path || !*path || cell_w < 1 || cell_h < 1 || cell_w > 256 || cell_h > 256 || family < 0 || family > 15 ||
        !ballum_charset_by_name(charset, &cs))
        return (int)BALLUM_STATUS_INVALID_ARGUMENT;
    FILE *f = fopen(path, "rb");
    if (!f) return (int)BALLUM_STATUS_DISPLAY_NOT_FOUND;                  /* no such file */
    uint8_t *buf = NULL; size_t len = 0, cap = 0;
    for (;;) {
        if (len == cap) {
            size_t nc = cap ? cap * 2u : 65536u;
            if (nc > PGM_MAX_BYTES) { free(buf); fclose(f); return (int)BALLUM_STATUS_BUFFER_TOO_SMALL; }
            uint8_t *nb = (uint8_t *)realloc(buf, nc);
            if (!nb) { free(buf); fclose(f); return (int)BALLUM_STATUS_BACKEND_ERROR; }
            buf = nb; cap = nc;
        }
        size_t got = fread(buf + len, 1, cap - len, f);
        if (got == 0) break;
        len += got;
    }
    fclose(f);
    ballum_charsource_t *c = chars_get();
    int st = c ? (int)ballum_charsource_add_pgm(c, buf, len, (uint32_t)cell_w, (uint32_t)cell_h, cs, family,
                                                light_ink ? BALLUM_COVER_LIGHT_INK : BALLUM_COVER_DARK_INK, NULL)
               : (int)BALLUM_STATUS_BACKEND_ERROR;
    free(buf);
    return st;
}

/* ---- the contact points ------------------------------------------------------------------ */

int sleela_ballum_vm_contact_count(void) { return (int)BALLUM_CONTACT__COUNT; }
const char *sleela_ballum_vm_contact_name(int i) {
    return (i >= 0 && i < (int)BALLUM_CONTACT__COUNT) ? ballum_contact_name((ballum_contact_t)i) : "";
}
int sleela_ballum_vm_contact_confidence(int i) {
    return (i >= 0 && i < (int)BALLUM_CONTACT__COUNT) ? ballum_contact_confidence((ballum_contact_t)i) : -1;
}

/* ---- observation and the interpreter, configured before a session ------------------------------ */

static int not_running(void) {
    return !g_host || ballum_host_state(g_host) == BALLUM_HOST_DONE || ballum_host_state(g_host) == BALLUM_HOST_IDLE;
}

int sleela_ballum_vm_host_observe(int on) {
    if (!not_running()) return (int)BALLUM_STATUS_INVALID_ARGUMENT;
    g_observe = on ? 1 : 0;
    return (int)BALLUM_STATUS_OK;
}

int sleela_ballum_vm_host_interpret(int mode) {
    if (mode < 0 || mode > 2 || !not_running()) return (int)BALLUM_STATUS_INVALID_ARGUMENT;
    g_interp_mode = mode;
    return (int)BALLUM_STATUS_OK;
}

int sleela_ballum_vm_host_band(int axis, int low, int high) {
    if (axis < 0 || axis >= (int)BALLUM_FILTER__COUNT || low < 0 || high > 1000 || !not_running())
        return (int)BALLUM_STATUS_INVALID_ARGUMENT;
    if (!g_band_set) { ballum_filter_band_default(&g_band); g_band_set = 1; }
    g_band.axis[axis].low = low; g_band.axis[axis].high = high;
    return (int)BALLUM_STATUS_OK;
}

int sleela_ballum_vm_host_scores(int m, int c, int t, int f) {
    int v[4] = { m, c, t, f };
    for (int i = 0; i < 4; ++i) if (v[i] < 0 || v[i] > 1000) return (int)BALLUM_STATUS_INVALID_ARGUMENT;
    for (int i = 0; i < 4; ++i) g_scores.score[i] = v[i];
    return (int)BALLUM_STATUS_OK;
}

int sleela_ballum_vm_host_rule(const char *match, const char *point, int value, int rgb) {
    ballum_contact_t pt;
    if (!match || !*match || strlen(match) >= SNAP_TEXT || !point || !ballum_contact_by_name(point, &pt) ||
        pt == BALLUM_CONTACT_TEXT || pt == BALLUM_CONTACT_CRYPTO_GRADE)
        return (int)BALLUM_STATUS_INVALID_ARGUMENT;
    if (g_nrules >= RULES_MAX) return (int)BALLUM_STATUS_BUFFER_TOO_SMALL;
    bridge_rule_t *r = &g_rules[g_nrules];
    memset(r, 0, sizeof(*r));
    strcpy(r->match, match);
    r->change.point = pt; r->change.value = value;
    r->change.color.r = (uint8_t)(rgb >> 16); r->change.color.g = (uint8_t)(rgb >> 8);
    r->change.color.b = (uint8_t)rgb; r->change.color.a = 255;
    ++g_nrules;
    return (int)BALLUM_STATUS_OK;
}

int sleela_ballum_vm_host_rule_clear(void) { g_nrules = 0; return (int)BALLUM_STATUS_OK; }
int sleela_ballum_vm_host_rule_count(void) { return g_nrules; }

/* ---- what the host saw ------------------------------------------------------------------------ */

int sleela_ballum_vm_host_element_count(void) { return g_nsnap; }
const char *sleela_ballum_vm_host_element_text(int i) { return (i >= 0 && i < g_nsnap) ? g_snap[i].text : ""; }
int sleela_ballum_vm_host_element_kind(int i)  { return (i >= 0 && i < g_nsnap) ? g_snap[i].kind : -1; }
int sleela_ballum_vm_host_element_x(int i)     { return (i >= 0 && i < g_nsnap) ? g_snap[i].x : -1; }
int sleela_ballum_vm_host_element_y(int i)     { return (i >= 0 && i < g_nsnap) ? g_snap[i].y : -1; }
int sleela_ballum_vm_host_element_w(int i)     { return (i >= 0 && i < g_nsnap) ? g_snap[i].w : -1; }
int sleela_ballum_vm_host_element_h(int i)     { return (i >= 0 && i < g_nsnap) ? g_snap[i].h : -1; }
int sleela_ballum_vm_host_element_known(int i) { return (i >= 0 && i < g_nsnap) ? g_snap[i].known : -1; }

int sleela_ballum_vm_host_stat(int which) {
    ballum_xdraw_stats_t xs;
    memset(&xs, 0, sizeof(xs));
    if (which >= 5 && g_host && g_host_source_ctx) ballum_host_source_x11_stats(g_host_source_ctx, &xs, NULL);
    switch (which) {
    case 0: return (int)g_istats.frames;
    case 1: return (int)g_istats.elements;
    case 2: return (int)g_istats.alterations_applied;
    case 3: return (int)g_istats.alterations_blocked;
    case 4: return (int)g_istats.pixels_altered;
    case 5: return (g_host && g_host_source_ctx) ? ballum_host_source_x11_observing(g_host_source_ctx) : 0;
    case 6: return (int)xs.glyphs;
    case 7: return (int)xs.glyphs_known;
    default: return -1;
    }
}
