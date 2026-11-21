/*
 * Ballum Wayland backend -- wl_shm fullscreen surface + pointer + keyboard.
 *
 * UNVERIFIED: written against the stable Wayland client API and the stable xdg-shell
 * protocol, but never built or run in the authoring environment (no Wayland
 * libraries, no compositor). Treat it as untested until `make PLATFORM=wayland` and
 * a run on a real compositor pass.
 *
 * What Wayland allows, and what it does not:
 *   - A client cannot read or draw on the desktop, so per-pixel control exists only
 *     on a surface Ballum claims (set_pixel/get_pixel/blit work on that surface and
 *     return UNSUPPORTED otherwise). No brightness, warmth or gamma: those belong to
 *     the compositor.
 *   - A client cannot grab all input. A fullscreen toplevel receives keyboard and
 *     pointer focus like any window, so set_input_grab is UNSUPPORTED and compositor
 *     shortcuts (workspace switch, etc.) still work.
 *   - The compositor does not auto-repeat keys for the client; this backend sends
 *     one press and one release per physical key, with no repeat.
 *   - Keysyms come from xkbcommon, using the unshifted symbol like the X11 backend.
 *
 * Programs are still hosted on the private Xvfb source, which does not depend on the
 * user's display server, so hosting works on a Wayland desktop with this backend.
 */
#define _GNU_SOURCE 1
#include "sleela_ballum/ballum_manager.h"

#if defined(BALLUM_BACKEND_WAYLAND)

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#include "xdg-shell-client-protocol.h"

#define MAX_OUT BALLUM_MAX_DISPLAYS
#define QCAP 256

typedef struct {
    struct wl_output *out;
    int32_t  w, h, refresh_mhz;
    char     name[BALLUM_DISPLAY_NAME_BYTES];
    int      done;
} wl_out_t;

static struct {
    int ready, failed;
    struct wl_display    *dpy;
    struct wl_registry   *reg;
    struct wl_compositor *comp;
    struct wl_shm        *shm;
    struct xdg_wm_base   *wm;
    struct wl_seat       *seat;
    struct wl_pointer    *ptr;
    struct wl_keyboard   *kbd;
    wl_out_t outs[MAX_OUT];
    int      nouts;

    struct xkb_context *xkb;
    struct xkb_keymap  *keymap;
    struct xkb_state   *xstate;

    /* the claimed surface */
    int active;
    int configured;
    uint32_t display;
    uint32_t w, h;
    uint32_t *fb;
    size_t   fb_bytes;
    struct wl_surface   *surf;
    struct xdg_surface  *xsurf;
    struct xdg_toplevel *top;
    struct wl_buffer    *buf;
    struct wl_shm_pool  *pool;
    int      closed;

    int32_t  px, py;                /* last pointer position (surface-local pixels) */
    uint32_t mods;
} W;

static ballum_pointer_event_t g_pq[QCAP]; static int g_pq_h, g_pq_t;
static ballum_key_event_t     g_kq[QCAP]; static int g_kq_h, g_kq_t;
static void pq_push(const ballum_pointer_event_t *e) { int n = (g_pq_t + 1) % QCAP; if (n == g_pq_h) return; g_pq[g_pq_t] = *e; g_pq_t = n; }
static void kq_push(const ballum_key_event_t *e) { int n = (g_kq_t + 1) % QCAP; if (n == g_kq_h) return; g_kq[g_kq_t] = *e; g_kq_t = n; }

/* ---- outputs ------------------------------------------------------------------ */
static void out_geometry(void *d, struct wl_output *o, int32_t x, int32_t y, int32_t pw, int32_t ph,
                         int32_t sp, const char *make, const char *model, int32_t tr) {
    (void)d; (void)o; (void)x; (void)y; (void)pw; (void)ph; (void)sp; (void)make; (void)model; (void)tr;
}
static wl_out_t *find_out(struct wl_output *o) {
    for (int i = 0; i < W.nouts; ++i) if (W.outs[i].out == o) return &W.outs[i];
    return NULL;
}
static void out_mode(void *d, struct wl_output *o, uint32_t flags, int32_t w, int32_t h, int32_t r) {
    (void)d;
    wl_out_t *x = find_out(o);
    if (x && (flags & WL_OUTPUT_MODE_CURRENT)) { x->w = w; x->h = h; x->refresh_mhz = r; }
}
static void out_done(void *d, struct wl_output *o) { (void)d; wl_out_t *x = find_out(o); if (x) x->done = 1; }
static void out_scale(void *d, struct wl_output *o, int32_t f) { (void)d; (void)o; (void)f; }
static const struct wl_output_listener k_out = { out_geometry, out_mode, out_done, out_scale };

/* ---- seat: pointer ------------------------------------------------------------ */
static void push_ptr(ballum_event_type_t t, ballum_pointer_button_t b) {
    ballum_pointer_event_t e;
    memset(&e, 0, sizeof(e));
    e.type = t; e.display_id = W.display; e.x = W.px; e.y = W.py; e.button = b;
    e.source = BALLUM_POINTER_MOUSE; e.modifiers = W.mods;
    pq_push(&e);
}
static void p_enter(void *d, struct wl_pointer *p, uint32_t s, struct wl_surface *sf, wl_fixed_t x, wl_fixed_t y) {
    (void)d; (void)p; (void)s; (void)sf; W.px = wl_fixed_to_int(x); W.py = wl_fixed_to_int(y);
}
static void p_leave(void *d, struct wl_pointer *p, uint32_t s, struct wl_surface *sf) { (void)d; (void)p; (void)s; (void)sf; }
static void p_motion(void *d, struct wl_pointer *p, uint32_t t, wl_fixed_t x, wl_fixed_t y) {
    (void)d; (void)p; (void)t;
    W.px = wl_fixed_to_int(x); W.py = wl_fixed_to_int(y);
    push_ptr(BALLUM_EVENT_POINTER_MOVE, BALLUM_BUTTON_NONE);
}
static void p_button(void *d, struct wl_pointer *p, uint32_t s, uint32_t t, uint32_t b, uint32_t st) {
    (void)d; (void)p; (void)s; (void)t;
    ballum_pointer_button_t bb = BALLUM_BUTTON_NONE;
    if (b == 0x110) bb = BALLUM_BUTTON_LEFT;          /* BTN_LEFT   */
    else if (b == 0x111) bb = BALLUM_BUTTON_RIGHT;    /* BTN_RIGHT  */
    else if (b == 0x112) bb = BALLUM_BUTTON_MIDDLE;   /* BTN_MIDDLE */
    if (bb != BALLUM_BUTTON_NONE)
        push_ptr(st == WL_POINTER_BUTTON_STATE_PRESSED ? BALLUM_EVENT_POINTER_DOWN : BALLUM_EVENT_POINTER_UP, bb);
}
static void p_axis(void *d, struct wl_pointer *p, uint32_t t, uint32_t axis, wl_fixed_t v) {
    (void)d; (void)p; (void)t;
    int32_t val = wl_fixed_to_int(v);
    ballum_pointer_button_t b = BALLUM_BUTTON_NONE;
    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) b = val < 0 ? BALLUM_BUTTON_WHEEL_UP : (val > 0 ? BALLUM_BUTTON_WHEEL_DOWN : BALLUM_BUTTON_NONE);
    else b = val < 0 ? BALLUM_BUTTON_WHEEL_LEFT : (val > 0 ? BALLUM_BUTTON_WHEEL_RIGHT : BALLUM_BUTTON_NONE);
    if (b != BALLUM_BUTTON_NONE) { push_ptr(BALLUM_EVENT_POINTER_DOWN, b); push_ptr(BALLUM_EVENT_POINTER_UP, b); }
}
static void p_frame(void *d, struct wl_pointer *p) { (void)d; (void)p; }
static void p_axis_source(void *d, struct wl_pointer *p, uint32_t s) { (void)d; (void)p; (void)s; }
static void p_axis_stop(void *d, struct wl_pointer *p, uint32_t t, uint32_t a) { (void)d; (void)p; (void)t; (void)a; }
static void p_axis_discrete(void *d, struct wl_pointer *p, uint32_t a, int32_t v) { (void)d; (void)p; (void)a; (void)v; }
static const struct wl_pointer_listener k_ptr = { p_enter, p_leave, p_motion, p_button, p_axis, p_frame,
                                                  p_axis_source, p_axis_stop, p_axis_discrete };

/* ---- seat: keyboard (xkbcommon) ------------------------------------------------ */
static void k_keymap(void *d, struct wl_keyboard *k, uint32_t fmt, int32_t fd, uint32_t size) {
    (void)d; (void)k;
    if (fmt != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 || size == 0) { close(fd); return; }
    char *map = (char *)mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED) return;
    struct xkb_keymap *km = xkb_keymap_new_from_buffer(W.xkb, map, size - 1, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map, size);
    if (!km) return;
    struct xkb_state *st = xkb_state_new(km);
    if (!st) { xkb_keymap_unref(km); return; }
    if (W.xstate) xkb_state_unref(W.xstate);
    if (W.keymap) xkb_keymap_unref(W.keymap);
    W.keymap = km; W.xstate = st;
}
static void k_enter(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *sf, struct wl_array *keys) { (void)d; (void)k; (void)s; (void)sf; (void)keys; }
static void k_leave(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *sf) { (void)d; (void)k; (void)s; (void)sf; }
static void k_key(void *d, struct wl_keyboard *k, uint32_t s, uint32_t t, uint32_t key, uint32_t state) {
    (void)d; (void)k; (void)s; (void)t;
    if (!W.keymap) return;
    xkb_keycode_t kc = key + 8;                        /* evdev -> xkb */
    const xkb_keysym_t *syms = NULL;
    int n = xkb_keymap_key_get_syms_by_level(W.keymap, kc, 0, 0, &syms);
    if (n < 1 || !syms) return;                        /* unmapped: dropped, not guessed */
    ballum_key_event_t e;
    memset(&e, 0, sizeof(e));
    e.symbol = (uint32_t)syms[0]; e.code = kc;
    e.pressed = state == WL_KEYBOARD_KEY_STATE_PRESSED;
    e.modifiers = W.mods;
    kq_push(&e);
}
static void k_mods(void *d, struct wl_keyboard *k, uint32_t s, uint32_t dep, uint32_t lat, uint32_t lock, uint32_t grp) {
    (void)d; (void)k; (void)s;
    if (!W.xstate) return;
    xkb_state_update_mask(W.xstate, dep, lat, lock, 0, 0, grp);
    uint32_t m = 0;
    if (xkb_state_mod_name_is_active(W.xstate, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0) m |= BALLUM_MOD_SHIFT;
    if (xkb_state_mod_name_is_active(W.xstate, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0) m |= BALLUM_MOD_CTRL;
    if (xkb_state_mod_name_is_active(W.xstate, XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE) > 0) m |= BALLUM_MOD_ALT;
    if (xkb_state_mod_name_is_active(W.xstate, XKB_MOD_NAME_LOGO, XKB_STATE_MODS_EFFECTIVE) > 0) m |= BALLUM_MOD_SUPER;
    W.mods = m;
}
static void k_repeat(void *d, struct wl_keyboard *k, int32_t rate, int32_t delay) { (void)d; (void)k; (void)rate; (void)delay; }
static const struct wl_keyboard_listener k_kbd = { k_keymap, k_enter, k_leave, k_key, k_mods, k_repeat };

static void seat_caps(void *d, struct wl_seat *s, uint32_t caps) {
    (void)d;
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !W.ptr) { W.ptr = wl_seat_get_pointer(s); wl_pointer_add_listener(W.ptr, &k_ptr, NULL); }
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !W.kbd) { W.kbd = wl_seat_get_keyboard(s); wl_keyboard_add_listener(W.kbd, &k_kbd, NULL); }
}
static void seat_name(void *d, struct wl_seat *s, const char *n) { (void)d; (void)s; (void)n; }
static const struct wl_seat_listener k_seat = { seat_caps, seat_name };

static void wm_ping(void *d, struct xdg_wm_base *wm, uint32_t serial) { (void)d; xdg_wm_base_pong(wm, serial); }
static const struct xdg_wm_base_listener k_wm = { wm_ping };

/* ---- registry ----------------------------------------------------------------- */
static void reg_global(void *d, struct wl_registry *r, uint32_t name, const char *iface, uint32_t ver) {
    (void)d;
    if (!strcmp(iface, "wl_compositor")) W.comp = (struct wl_compositor *)wl_registry_bind(r, name, &wl_compositor_interface, ver < 4 ? ver : 4);
    else if (!strcmp(iface, "wl_shm")) W.shm = (struct wl_shm *)wl_registry_bind(r, name, &wl_shm_interface, 1);
    else if (!strcmp(iface, "xdg_wm_base")) { W.wm = (struct xdg_wm_base *)wl_registry_bind(r, name, &xdg_wm_base_interface, 1); xdg_wm_base_add_listener(W.wm, &k_wm, NULL); }
    else if (!strcmp(iface, "wl_seat") && !W.seat) { W.seat = (struct wl_seat *)wl_registry_bind(r, name, &wl_seat_interface, ver < 5 ? ver : 5); wl_seat_add_listener(W.seat, &k_seat, NULL); }
    else if (!strcmp(iface, "wl_output") && W.nouts < (int)MAX_OUT) {
        wl_out_t *o = &W.outs[W.nouts++];
        memset(o, 0, sizeof(*o));
        o->out = (struct wl_output *)wl_registry_bind(r, name, &wl_output_interface, 2);
        snprintf(o->name, sizeof(o->name), "wayland-output-%d", W.nouts);
        wl_output_add_listener(o->out, &k_out, NULL);
    }
}
static void reg_remove(void *d, struct wl_registry *r, uint32_t name) { (void)d; (void)r; (void)name; }
static const struct wl_registry_listener k_reg = { reg_global, reg_remove };

static int ensure_init(void) {
    if (W.ready) return 0;
    if (W.failed) return -1;
    W.dpy = wl_display_connect(NULL);
    if (!W.dpy) { W.failed = 1; return -1; }
    W.xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    W.reg = wl_display_get_registry(W.dpy);
    wl_registry_add_listener(W.reg, &k_reg, NULL);
    wl_display_roundtrip(W.dpy);          /* globals */
    wl_display_roundtrip(W.dpy);          /* output modes, seat capabilities, keymap */
    if (!W.comp || !W.shm || !W.wm || !W.xkb || W.nouts == 0) { W.failed = 1; return -1; }
    W.ready = 1;
    return 0;
}

static void pump(void) {
    if (!W.ready) return;
    wl_display_flush(W.dpy);
    while (wl_display_prepare_read(W.dpy) != 0) wl_display_dispatch_pending(W.dpy);
    struct pollfd pf = { wl_display_get_fd(W.dpy), POLLIN, 0 };
    if (poll(&pf, 1, 0) > 0) wl_display_read_events(W.dpy); else wl_display_cancel_read(W.dpy);
    wl_display_dispatch_pending(W.dpy);
}

/* ---- backend functions --------------------------------------------------------- */
static ballum_status_t wl_enumerate(void *ctx, ballum_display_t *out, size_t cap, size_t *n) {
    (void)ctx;
    if (n) *n = 0;
    if (ensure_init() != 0) return BALLUM_STATUS_BACKEND_UNAVAILABLE;
    size_t c = 0;
    for (int i = 0; i < W.nouts && c < cap; ++i) {
        ballum_display_t d;
        memset(&d, 0, sizeof(d));
        d.id = (uint32_t)(i + 1);
        snprintf(d.name, sizeof(d.name), "%s", W.outs[i].name);
        d.width = (uint32_t)W.outs[i].w; d.height = (uint32_t)W.outs[i].h;
        d.refresh_mhz = (uint32_t)W.outs[i].refresh_mhz;
        d.is_primary = i == 0;
        d.capabilities = BALLUM_CAP_FULLSCREEN_SURFACE | BALLUM_CAP_POINTER_INPUT | BALLUM_CAP_PER_PIXEL | BALLUM_CAP_REGION_BLIT;
        out[c++] = d;
    }
    if (n) *n = c;
    return BALLUM_STATUS_OK;
}

static ballum_status_t wl_set_pixel(void *ctx, uint32_t id, ballum_point_t at, ballum_color_t c) {
    (void)ctx;
    if (!W.active || W.display != id) return BALLUM_STATUS_UNSUPPORTED;    /* no desktop drawing on Wayland */
    if (at.x >= W.w || at.y >= W.h) return BALLUM_STATUS_OUT_OF_BOUNDS;
    W.fb[(size_t)at.y * W.w + at.x] = 0xFF000000u | ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | c.b;
    return BALLUM_STATUS_OK;
}
static ballum_status_t wl_get_pixel(void *ctx, uint32_t id, ballum_point_t at, ballum_color_t *o) {
    (void)ctx;
    if (!o) return BALLUM_STATUS_INVALID_ARGUMENT;
    if (!W.active || W.display != id) return BALLUM_STATUS_UNSUPPORTED;
    if (at.x >= W.w || at.y >= W.h) return BALLUM_STATUS_OUT_OF_BOUNDS;
    uint32_t v = W.fb[(size_t)at.y * W.w + at.x];
    o->r = (uint8_t)(v >> 16); o->g = (uint8_t)(v >> 8); o->b = (uint8_t)v; o->a = 255;
    return BALLUM_STATUS_OK;
}
static ballum_status_t wl_blit(void *ctx, uint32_t id, ballum_rect_t r, const ballum_color_t *px, size_t n) {
    (void)ctx;
    if (!px || n != (size_t)r.width * r.height) return BALLUM_STATUS_INVALID_ARGUMENT;
    if (!W.active || W.display != id) return BALLUM_STATUS_UNSUPPORTED;
    for (uint32_t y = 0; y < r.height && r.y + y < W.h; ++y)
        for (uint32_t x = 0; x < r.width && r.x + x < W.w; ++x) {
            ballum_color_t c = px[(size_t)y * r.width + x];
            W.fb[(size_t)(r.y + y) * W.w + r.x + x] = 0xFF000000u | ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | c.b;
        }
    return BALLUM_STATUS_OK;
}

static void xs_configure(void *d, struct xdg_surface *xs, uint32_t serial) {
    (void)d; xdg_surface_ack_configure(xs, serial); W.configured = 1;
}
static const struct xdg_surface_listener k_xs = { xs_configure };
static void top_configure(void *d, struct xdg_toplevel *t, int32_t w, int32_t h, struct wl_array *st) {
    (void)d; (void)t; (void)w; (void)h; (void)st;       /* the buffer keeps the output's size */
}
static void top_close(void *d, struct xdg_toplevel *t) { (void)d; (void)t; W.closed = 1; }
static const struct xdg_toplevel_listener k_top = { top_configure, top_close };

static void teardown(void) {
    if (!W.active && !W.surf) return;
    if (W.buf) wl_buffer_destroy(W.buf);
    if (W.pool) wl_shm_pool_destroy(W.pool);
    if (W.top) xdg_toplevel_destroy(W.top);
    if (W.xsurf) xdg_surface_destroy(W.xsurf);
    if (W.surf) wl_surface_destroy(W.surf);
    if (W.fb) munmap(W.fb, W.fb_bytes);
    if (W.dpy) wl_display_flush(W.dpy);
    W.buf = NULL; W.pool = NULL; W.top = NULL; W.xsurf = NULL; W.surf = NULL; W.fb = NULL;
    W.active = 0; W.configured = 0; W.closed = 0;
    g_pq_h = g_pq_t = g_kq_h = g_kq_t = 0;
}

static ballum_status_t wl_claim(void *ctx, uint32_t id) {
    (void)ctx;
    if (ensure_init() != 0) return BALLUM_STATUS_BACKEND_UNAVAILABLE;
    if (id < 1 || id > (uint32_t)W.nouts) return BALLUM_STATUS_DISPLAY_NOT_FOUND;
    wl_out_t *o = &W.outs[id - 1];
    if (o->w <= 0 || o->h <= 0) return BALLUM_STATUS_DISPLAY_NOT_FOUND;
    if (W.active) teardown();
    uint32_t w = (uint32_t)o->w, h = (uint32_t)o->h;
    size_t bytes = (size_t)w * h * 4u;

    int fd = memfd_create("ballum-wl", MFD_CLOEXEC);
    if (fd < 0) return BALLUM_STATUS_BACKEND_ERROR;
    if (ftruncate(fd, (off_t)bytes) != 0) { close(fd); return BALLUM_STATUS_BACKEND_ERROR; }
    void *map = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) { close(fd); return BALLUM_STATUS_BACKEND_ERROR; }
    W.pool = wl_shm_create_pool(W.shm, fd, (int32_t)bytes);
    close(fd);
    if (!W.pool) { munmap(map, bytes); return BALLUM_STATUS_BACKEND_ERROR; }
    W.buf = wl_shm_pool_create_buffer(W.pool, 0, (int32_t)w, (int32_t)h, (int32_t)(w * 4u), WL_SHM_FORMAT_XRGB8888);
    W.fb = (uint32_t *)map; W.fb_bytes = bytes; W.w = w; W.h = h; W.display = id;
    memset(map, 0, bytes);

    W.surf = wl_compositor_create_surface(W.comp);
    W.xsurf = xdg_wm_base_get_xdg_surface(W.wm, W.surf);
    xdg_surface_add_listener(W.xsurf, &k_xs, NULL);
    W.top = xdg_surface_get_toplevel(W.xsurf);
    xdg_toplevel_add_listener(W.top, &k_top, NULL);
    xdg_toplevel_set_title(W.top, "Ballum");
    xdg_toplevel_set_app_id(W.top, "ballum");
    xdg_toplevel_set_fullscreen(W.top, o->out);
    wl_surface_commit(W.surf);                       /* ask for the first configure */
    W.active = 1;
    for (int i = 0; i < 50 && !W.configured; ++i) { wl_display_roundtrip(W.dpy); }
    if (!W.configured) { teardown(); return BALLUM_STATUS_BACKEND_ERROR; }
    wl_surface_attach(W.surf, W.buf, 0, 0);
    wl_surface_damage_buffer(W.surf, 0, 0, (int32_t)w, (int32_t)h);
    wl_surface_commit(W.surf);
    wl_display_flush(W.dpy);
    return BALLUM_STATUS_OK;
}

static ballum_status_t wl_present(void *ctx, uint32_t id) {
    (void)ctx;
    if (!W.active || W.display != id) return BALLUM_STATUS_UNSUPPORTED;
    wl_surface_attach(W.surf, W.buf, 0, 0);
    wl_surface_damage_buffer(W.surf, 0, 0, (int32_t)W.w, (int32_t)W.h);
    wl_surface_commit(W.surf);
    pump();
    return BALLUM_STATUS_OK;
}
static ballum_status_t wl_release(void *ctx, uint32_t id) {
    (void)ctx;
    if (!W.active || W.display != id) return BALLUM_STATUS_UNSUPPORTED;
    teardown();
    return BALLUM_STATUS_OK;
}
static ballum_status_t wl_poll_event(void *ctx, ballum_pointer_event_t *out) {
    (void)ctx;
    if (!out) return BALLUM_STATUS_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out)); out->type = BALLUM_EVENT_NONE;
    if (!W.active) return BALLUM_STATUS_UNSUPPORTED;
    if (g_pq_h == g_pq_t) pump();
    if (g_pq_h != g_pq_t) { *out = g_pq[g_pq_h]; g_pq_h = (g_pq_h + 1) % QCAP; }
    return BALLUM_STATUS_OK;
}
static ballum_status_t wl_poll_key_event(void *ctx, ballum_key_event_t *out) {
    (void)ctx;
    if (!out) return BALLUM_STATUS_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if (!W.active) return BALLUM_STATUS_UNSUPPORTED;
    if (g_kq_h == g_kq_t) pump();
    if (g_kq_h != g_kq_t) { *out = g_kq[g_kq_h]; g_kq_h = (g_kq_h + 1) % QCAP; }
    return BALLUM_STATUS_OK;
}
/* Edge-only session keys: derived from the same queue the full events use. */
static ballum_status_t wl_poll_key(void *ctx, int *out) {
    (void)ctx;
    if (!out) return BALLUM_STATUS_INVALID_ARGUMENT;
    *out = BALLUM_KEY_NONE;
    if (!W.active) return BALLUM_STATUS_OK;
    if (g_kq_h == g_kq_t) pump();
    for (int i = g_kq_h; i != g_kq_t; i = (i + 1) % QCAP) {
        if (!g_kq[i].pressed) continue;
        uint32_t s = g_kq[i].symbol;
        if (s == 0xFF1Bu) *out = BALLUM_KEY_ESC;
        else if (s == 0xFF0Du) *out = BALLUM_KEY_ENTER;
        else if (s == 0x20u) *out = BALLUM_KEY_SPACE;
        else continue;
        /* consume that one event so it is not reported twice */
        for (int j = i; (j + 1) % QCAP != g_kq_t; j = (j + 1) % QCAP) g_kq[j] = g_kq[(j + 1) % QCAP];
        g_kq_t = (g_kq_t + QCAP - 1) % QCAP;
        break;
    }
    return BALLUM_STATUS_OK;
}

static const ballum_backend_t k_wayland = {
    "wayland-shm",
    wl_enumerate, wl_set_pixel, wl_get_pixel, wl_blit,
    /* brightness, warmth, gamma: the compositor owns them */ NULL, NULL, NULL,
    wl_poll_event, wl_poll_key,
    wl_claim, wl_present, wl_release,
    wl_poll_key_event, /* set_input_grab: Wayland has no global grab */ NULL
};

const ballum_backend_t *ballum_backend_wayland(void) { return &k_wayland; }

#else  /* !BALLUM_BACKEND_WAYLAND */

typedef int ballum_wayland_translation_unit_not_selected;

#endif
