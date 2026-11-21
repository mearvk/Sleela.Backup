/*
 * Ballum macOS backend -- Core Graphics / Quartz (open system frameworks).
 *
 * - enumerate       : CGGetActiveDisplayList -> displays + CGDisplayBounds.
 * - set/get_pixel   : a CGDisplay capture/draw path (per-pixel via a CGContext).
 * - blit_region     : draws a caller RGBA buffer into a display context.
 * - set_brightness  : scales the display gamma transfer (CGSetDisplayTransfer
 *                     ByFormula min/max), the portable Quartz brightness path.
 * - set_warmth      : per-channel gamma min offsets approximating a white point.
 * - set_gamma       : CGSetDisplayTransferByFormula gamma exponent.
 *
 * - claim/present/release_surface, poll_event, poll_key_event, set_input_grab:
 *                     borderless shielding-level NSWindow over a BGRA
 *                     framebuffer (Retina pixel exact), full pointer/key/wheel
 *                     queues, kiosk presentation options while grabbed.
 *                     UNVERIFIED: never built on macOS in the authoring env.
 *
 * Only meaningful when built with BALLUM_BACKEND_MACOS (the Makefile adds
 * -framework CoreGraphics -framework ApplicationServices -framework Cocoa). Without the
 * define this compiles to an empty TU so non-mac builds never need the SDK.
 */

#include "sleela_ballum/ballum_manager.h"

#if defined(BALLUM_BACKEND_MACOS)

#import <ApplicationServices/ApplicationServices.h>
#import <Cocoa/Cocoa.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Fullscreen surface state (one surface at a time) ------------------- *
 * UNVERIFIED: this surface/input path has never been built or run on macOS in
 * the authoring environment (no Apple SDK). It follows the documented Cocoa
 * API; treat it as untested until `make PLATFORM=macos test` and a manual
 * run on a real Mac pass. Manual retain/release (no ARC).
 * The framebuffer is in backing PIXELS (Retina aware); events arrive in points
 * and are scaled to pixels with the origin flipped to top-left. */
@interface BallumWindow : NSWindow
@end
@implementation BallumWindow
- (BOOL)canBecomeKeyWindow { return YES; }
- (BOOL)canBecomeMainWindow { return YES; }
@end

@interface BallumView : NSView
@end

static struct {
    int       active;
    uint32_t  display;
    uint32_t  w, h;           /* framebuffer size in pixels */
    double    scale;          /* pixels per point            */
    uint32_t *fb;             /* 0xFFRRGGBB little-endian BGRA, top-left origin */
    NSWindow *window;
    BallumView *view;
    int       grabbed;
    int       cursor_hidden;
    NSApplicationPresentationOptions saved_presentation;
} g_s;

@implementation BallumView
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)isOpaque { return YES; }
- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    if (!g_s.active || !g_s.fb) return;
    CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef bm = CGBitmapContextCreate(g_s.fb, g_s.w, g_s.h, 8, (size_t)g_s.w * 4u, cs,
        (CGBitmapInfo)kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little);
    CGColorSpaceRelease(cs);
    if (!bm) return;
    CGImageRef img = CGBitmapContextCreateImage(bm);
    CGContextRelease(bm);
    if (!img) return;
    CGContextRef dst = [[NSGraphicsContext currentContext] CGContext];
    CGContextSetInterpolationQuality(dst, kCGInterpolationNone);
    CGContextDrawImage(dst, NSRectToCGRect([self bounds]), img);
    CGImageRelease(img);
}
@end

/* Event queues filled by pump_events(). */
#define BQ 256
static ballum_pointer_event_t g_pq[BQ]; static int g_pq_h, g_pq_t;
static ballum_key_event_t     g_kq[BQ]; static int g_kq_h, g_kq_t;
static void pq_push(const ballum_pointer_event_t *e) {
    int n = (g_pq_t + 1) % BQ; if (n == g_pq_h) return; g_pq[g_pq_t] = *e; g_pq_t = n;
}
static void kq_push(const ballum_key_event_t *e) {
    int n = (g_kq_t + 1) % BQ; if (n == g_kq_h) return; g_kq[g_kq_t] = *e; g_kq_t = n;
}

static ballum_status_t macos_enumerate(void *ctx, ballum_display_t *out,
                                        size_t capacity, size_t *out_count) {
    (void)ctx;
    if (out_count) *out_count = 0;
    CGDirectDisplayID ids[BALLUM_MAX_DISPLAYS];
    uint32_t n = 0;
    if (CGGetActiveDisplayList(BALLUM_MAX_DISPLAYS, ids, &n) != kCGErrorSuccess)
        return BALLUM_STATUS_BACKEND_ERROR;

    size_t count = 0;
    for (uint32_t i = 0; i < n && count < capacity && count < BALLUM_MAX_DISPLAYS; ++i) {
        CGRect b = CGDisplayBounds(ids[i]);
        ballum_display_t d;
        memset(&d, 0, sizeof(d));
        d.id = (uint32_t)ids[i];
        snprintf(d.name, sizeof(d.name), "display-%u", (unsigned)ids[i]);
        /* Real pixels, not points: a Retina display's pixel grid is larger than
         * its CGDisplayBounds point size. */
        d.width = (uint32_t)b.size.width;
        d.height = (uint32_t)b.size.height;
        d.refresh_mhz = 0;
        CGDisplayModeRef mode = CGDisplayCopyDisplayMode(ids[i]);
        if (mode) {
            size_t pw = CGDisplayModeGetPixelWidth(mode), ph = CGDisplayModeGetPixelHeight(mode);
            if (pw > 0 && ph > 0) { d.width = (uint32_t)pw; d.height = (uint32_t)ph; }
            double hz = CGDisplayModeGetRefreshRate(mode); /* 0 on built-in panels */
            if (hz > 0.0) d.refresh_mhz = (uint32_t)(hz * 1000.0 + 0.5);
            CGDisplayModeRelease(mode);
        }
        d.is_primary = CGDisplayIsMain(ids[i]) ? 1 : 0;
        d.capabilities = BALLUM_CAP_PER_PIXEL | BALLUM_CAP_REGION_BLIT |
                         BALLUM_CAP_BRIGHTNESS | BALLUM_CAP_COLOR_TEMPERATURE |
                         BALLUM_CAP_GAMMA | BALLUM_CAP_FULLSCREEN_SURFACE |
                         BALLUM_CAP_POINTER_INPUT;
        out[count++] = d;
    }
    if (out_count) *out_count = count;
    return BALLUM_STATUS_OK;
}

/* Per-pixel drawing uses a one-pixel fill rect on the display's context. */
static ballum_status_t macos_set_pixel(void *ctx, uint32_t id,
                                       ballum_point_t at, ballum_color_t color) {
    (void)ctx;
    if (g_s.active && g_s.display == id) {
        if (at.x >= g_s.w || at.y >= g_s.h) return BALLUM_STATUS_OUT_OF_BOUNDS;
        g_s.fb[(size_t)at.y * g_s.w + at.x] = 0xFF000000u | ((uint32_t)color.r << 16) |
                                              ((uint32_t)color.g << 8) | color.b;
        return BALLUM_STATUS_OK;
    }
    CGDirectDisplayID disp = (CGDirectDisplayID)id;
    CGContextRef c = CGDisplayGetDrawingContext(disp);
    if (!c) return BALLUM_STATUS_UNSUPPORTED;
    CGContextSetRGBFillColor(c, color.r / 255.0, color.g / 255.0,
                             color.b / 255.0, color.a / 255.0);
    CGContextFillRect(c, CGRectMake(at.x, at.y, 1, 1));
    CGContextFlush(c);
    return BALLUM_STATUS_OK;
}

static ballum_status_t macos_get_pixel(void *ctx, uint32_t id,
                                       ballum_point_t at, ballum_color_t *out) {
    (void)ctx;
    if (!out) return BALLUM_STATUS_INVALID_ARGUMENT;
    if (g_s.active && g_s.display == id) {
        /* Reading back our own framebuffer is exact and needs no permission. */
        if (at.x >= g_s.w || at.y >= g_s.h) return BALLUM_STATUS_OUT_OF_BOUNDS;
        uint32_t v = g_s.fb[(size_t)at.y * g_s.w + at.x];
        out->r = (uint8_t)(v >> 16); out->g = (uint8_t)(v >> 8); out->b = (uint8_t)v; out->a = 255;
        return BALLUM_STATUS_OK;
    }
    /* macOS 15 marks CGDisplayCreateImageForRect unavailable. ScreenCaptureKit
       is the supported replacement, but it requires an asynchronous capture
       session and user-granted screen-recording permission. Do not call the
       removed API or claim synchronous readback; drawing/blitting remain
       available through the Core Graphics backend. */
    return BALLUM_STATUS_UNSUPPORTED;
}

static ballum_status_t macos_blit(void *ctx, uint32_t id, ballum_rect_t rect,
                                  const ballum_color_t *pixels, size_t n) {
    (void)ctx;
    if (!pixels || n != (size_t)rect.width * rect.height) return BALLUM_STATUS_INVALID_ARGUMENT;
    if (g_s.active && g_s.display == id) {
        for (uint32_t row = 0; row < rect.height; ++row) {
            if (rect.y + row >= g_s.h) break;
            for (uint32_t col = 0; col < rect.width; ++col) {
                if (rect.x + col >= g_s.w) break;
                const ballum_color_t px = pixels[(size_t)row * rect.width + col];
                g_s.fb[(size_t)(rect.y + row) * g_s.w + rect.x + col] =
                    0xFF000000u | ((uint32_t)px.r << 16) | ((uint32_t)px.g << 8) | px.b;
            }
        }
        return BALLUM_STATUS_OK;
    }
    CGDirectDisplayID disp = (CGDirectDisplayID)id;
    CGContextRef c = CGDisplayGetDrawingContext(disp);
    if (!c) return BALLUM_STATUS_UNSUPPORTED;
    for (uint32_t row = 0; row < rect.height; ++row) {
        for (uint32_t col = 0; col < rect.width; ++col) {
            const ballum_color_t px = pixels[(size_t)row * rect.width + col];
            CGContextSetRGBFillColor(c, px.r / 255.0, px.g / 255.0, px.b / 255.0, px.a / 255.0);
            CGContextFillRect(c, CGRectMake(rect.x + col, rect.y + row, 1, 1));
        }
    }
    CGContextFlush(c);
    return BALLUM_STATUS_OK;
}

static ballum_status_t macos_set_brightness(void *ctx, uint32_t id, int percent) {
    (void)ctx;
    CGDirectDisplayID disp = (CGDirectDisplayID)id;
    CGGammaValue m = (CGGammaValue)percent / 100.0f;
    /* Linear ramp scaled by brightness: min 0, max = m, gamma 1.0. */
    if (CGSetDisplayTransferByFormula(disp, 0, m, 1.0f, 0, m, 1.0f, 0, m, 1.0f) != kCGErrorSuccess)
        return BALLUM_STATUS_BACKEND_ERROR;
    return BALLUM_STATUS_OK;
}

static void warmth_multipliers(uint32_t kelvin, float *rm, float *gm, float *bm) {
    double t = (double)kelvin / 100.0;
    double r, g, b;
    if (t <= 66.0) r = 255.0; else r = 329.698727446 * pow(t - 60.0, -0.1332047592);
    if (t <= 66.0) g = 99.4708025861 * log(t) - 161.1195681661;
    else           g = 288.1221695283 * pow(t - 60.0, -0.0755148492);
    if (t >= 66.0) b = 255.0; else if (t <= 19.0) b = 0.0;
    else           b = 138.5177312231 * log(t - 10.0) - 305.0447927307;
    if (r < 0) { r = 0; } else if (r > 255) { r = 255; }
    if (g < 0) { g = 0; } else if (g > 255) { g = 255; }
    if (b < 0) { b = 0; } else if (b > 255) { b = 255; }
    *rm = (float)(r / 255.0); *gm = (float)(g / 255.0); *bm = (float)(b / 255.0);
}

static ballum_status_t macos_set_warmth(void *ctx, uint32_t id, uint32_t kelvin) {
    (void)ctx;
    CGDirectDisplayID disp = (CGDirectDisplayID)id;
    float rm, gm, bm; warmth_multipliers(kelvin, &rm, &gm, &bm);
    if (CGSetDisplayTransferByFormula(disp, 0, rm, 1.0f, 0, gm, 1.0f, 0, bm, 1.0f) != kCGErrorSuccess)
        return BALLUM_STATUS_BACKEND_ERROR;
    return BALLUM_STATUS_OK;
}

static ballum_status_t macos_set_gamma(void *ctx, uint32_t id, uint32_t gamma_scaled) {
    (void)ctx;
    CGDirectDisplayID disp = (CGDirectDisplayID)id;
    float gamma = (float)gamma_scaled / (float)BALLUM_GAMMA_SCALE;
    if (gamma <= 0.0f) return BALLUM_STATUS_INVALID_ARGUMENT;
    if (CGSetDisplayTransferByFormula(disp, 0, 1.0f, gamma, 0, 1.0f, gamma, 0, 1.0f, gamma) != kCGErrorSuccess)
        return BALLUM_STATUS_BACKEND_ERROR;
    return BALLUM_STATUS_OK;
}

/* ---- Input translation ------------------------------------------------- */
static uint32_t mods_from_flags(NSEventModifierFlags f) {
    uint32_t m = 0;
    if (f & NSEventModifierFlagShift)   m |= BALLUM_MOD_SHIFT;
    if (f & NSEventModifierFlagControl) m |= BALLUM_MOD_CTRL;
    if (f & NSEventModifierFlagOption)  m |= BALLUM_MOD_ALT;
    if (f & NSEventModifierFlagCommand) m |= BALLUM_MOD_SUPER;
    return m;
}

/* macOS virtual key code -> X11 keysym (the vocabulary the hosted program and
 * the Linux/Windows backends use). 0 = unmapped. */
static uint32_t keysym_for(unsigned short kc, NSEvent *e) {
    static const unsigned short fk[12] = {122,120,99,118,96,97,98,100,101,109,103,111};
    for (int i = 0; i < 12; ++i) if (fk[i] == kc) return 0xFFBEu + (uint32_t)i;
    switch (kc) {
    case 36: case 76: return 0xFF0D;   /* Return, keypad Enter */
    case 48: return 0xFF09;            /* Tab        */
    case 49: return 0x20;              /* Space      */
    case 51: return 0xFF08;            /* Backspace  */
    case 53: return 0xFF1B;            /* Escape     */
    case 117: return 0xFFFF;           /* Delete     */
    case 115: return 0xFF50;           /* Home       */
    case 119: return 0xFF57;           /* End        */
    case 116: return 0xFF55;           /* Page Up    */
    case 121: return 0xFF56;           /* Page Down  */
    case 123: return 0xFF51;           /* Left       */
    case 126: return 0xFF52;           /* Up         */
    case 124: return 0xFF53;           /* Right      */
    case 125: return 0xFF54;           /* Down       */
    default: break;
    }
    NSString *s = [e charactersIgnoringModifiers];
    if (s && [s length] > 0) {
        unichar c = [s characterAtIndex:0];
        if (c >= 'A' && c <= 'Z') c = (unichar)(c + 32);
        if (c >= 0x20 && c < 0x7F) return (uint32_t)c;
    }
    return 0;
}

static void push_pointer(NSEvent *e, ballum_event_type_t type, ballum_pointer_button_t b) {
    NSPoint p = [e locationInWindow];       /* points, origin bottom-left */
    NSRect fr = [[g_s.view window] contentRectForFrameRect:[[g_s.view window] frame]];
    double hpts = fr.size.height;
    ballum_pointer_event_t pe;
    memset(&pe, 0, sizeof(pe));
    pe.type = type; pe.display_id = g_s.display; pe.button = b;
    pe.source = BALLUM_POINTER_MOUSE;
    pe.x = (int32_t)floor(p.x * g_s.scale);
    pe.y = (int32_t)floor((hpts - p.y) * g_s.scale);
    pe.modifiers = mods_from_flags([e modifierFlags]);
    pq_push(&pe);
}

static void pump_events(void) {
    if (!g_s.active) return;
    @autoreleasepool {
        for (;;) {
            NSEvent *e = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:nil
                                               inMode:NSDefaultRunLoopMode dequeue:YES];
            if (!e) break;
            switch ([e type]) {
            case NSEventTypeLeftMouseDown:  push_pointer(e, BALLUM_EVENT_POINTER_DOWN, BALLUM_BUTTON_LEFT); break;
            case NSEventTypeLeftMouseUp:    push_pointer(e, BALLUM_EVENT_POINTER_UP,   BALLUM_BUTTON_LEFT); break;
            case NSEventTypeRightMouseDown: push_pointer(e, BALLUM_EVENT_POINTER_DOWN, BALLUM_BUTTON_RIGHT); break;
            case NSEventTypeRightMouseUp:   push_pointer(e, BALLUM_EVENT_POINTER_UP,   BALLUM_BUTTON_RIGHT); break;
            case NSEventTypeOtherMouseDown: push_pointer(e, BALLUM_EVENT_POINTER_DOWN, BALLUM_BUTTON_MIDDLE); break;
            case NSEventTypeOtherMouseUp:   push_pointer(e, BALLUM_EVENT_POINTER_UP,   BALLUM_BUTTON_MIDDLE); break;
            case NSEventTypeMouseMoved: case NSEventTypeLeftMouseDragged:
            case NSEventTypeRightMouseDragged: case NSEventTypeOtherMouseDragged:
                push_pointer(e, BALLUM_EVENT_POINTER_MOVE, BALLUM_BUTTON_NONE); break;
            case NSEventTypeScrollWheel: {
                /* One wheel button press+release per scroll event, by sign. */
                double dy = [e scrollingDeltaY], dx = [e scrollingDeltaX];
                ballum_pointer_button_t b = BALLUM_BUTTON_NONE;
                if (dy > 0) b = BALLUM_BUTTON_WHEEL_UP; else if (dy < 0) b = BALLUM_BUTTON_WHEEL_DOWN;
                else if (dx > 0) b = BALLUM_BUTTON_WHEEL_LEFT; else if (dx < 0) b = BALLUM_BUTTON_WHEEL_RIGHT;
                if (b != BALLUM_BUTTON_NONE) {
                    push_pointer(e, BALLUM_EVENT_POINTER_DOWN, b);
                    push_pointer(e, BALLUM_EVENT_POINTER_UP, b);
                }
                break;
            }
            case NSEventTypeKeyDown: case NSEventTypeKeyUp: {
                ballum_key_event_t ke;
                memset(&ke, 0, sizeof(ke));
                ke.symbol = keysym_for([e keyCode], e);
                ke.code = [e keyCode];
                ke.pressed = ([e type] == NSEventTypeKeyDown);
                ke.modifiers = mods_from_flags([e modifierFlags]);
                /* Auto-repeat arrives as release+press pairs, as on X11. */
                if (ke.pressed && [e isARepeat]) { ke.pressed = 0; kq_push(&ke); ke.pressed = 1; }
                if (ke.symbol) kq_push(&ke);
                break;                  /* not forwarded: no system beep */
            }
            default:
                [NSApp sendEvent:e];
                break;
            }
        }
    }
}

static void surface_teardown(void);

static ballum_status_t macos_set_input_grab(void *ctx, int on) {
    (void)ctx;
    if (!g_s.active) return BALLUM_STATUS_UNSUPPORTED;
    if (on && !g_s.grabbed) {
        g_s.saved_presentation = [NSApp presentationOptions];
        [NSApp setPresentationOptions:NSApplicationPresentationHideDock |
                                      NSApplicationPresentationHideMenuBar |
                                      NSApplicationPresentationDisableProcessSwitching |
                                      NSApplicationPresentationDisableHideApplication];
        if (!g_s.cursor_hidden) { CGDisplayHideCursor(kCGDirectMainDisplay); g_s.cursor_hidden = 1; }
        [g_s.window makeKeyAndOrderFront:nil];
        [g_s.window makeFirstResponder:g_s.view];
        g_s.grabbed = 1;
    } else if (!on && g_s.grabbed) {
        [NSApp setPresentationOptions:g_s.saved_presentation];
        if (g_s.cursor_hidden) { CGDisplayShowCursor(kCGDirectMainDisplay); g_s.cursor_hidden = 0; }
        g_s.grabbed = 0;
    }
    return BALLUM_STATUS_OK;
}

static void surface_teardown(void) {
    if (!g_s.active) return;
    macos_set_input_grab(NULL, 0);
    [g_s.window orderOut:nil];
    [g_s.window close];
    [g_s.window release];      /* drops the window's reference to the view */
    [g_s.view release];        /* then our own */
    free(g_s.fb);
    memset(&g_s, 0, sizeof(g_s));
    g_pq_h = g_pq_t = g_kq_h = g_kq_t = 0;
}

static ballum_status_t macos_claim_surface(void *ctx, uint32_t id) {
    (void)ctx;
    @autoreleasepool {
        CGDirectDisplayID disp = (CGDirectDisplayID)id;
        NSScreen *screen = nil;
        for (NSScreen *sc in [NSScreen screens]) {
            NSNumber *n = [[sc deviceDescription] objectForKey:@"NSScreenNumber"];
            if (n && [n unsignedIntValue] == (unsigned)disp) { screen = sc; break; }
        }
        if (!screen) return BALLUM_STATUS_DISPLAY_NOT_FOUND;
        if (g_s.active) surface_teardown();
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        static int launched;
        if (!launched) { [NSApp finishLaunching]; launched = 1; }

        NSRect frame = [screen frame];
        double scale = [screen backingScaleFactor];
        if (scale < 1.0) scale = 1.0;
        uint32_t w = (uint32_t)(frame.size.width * scale + 0.5);
        uint32_t h = (uint32_t)(frame.size.height * scale + 0.5);
        if (!w || !h) return BALLUM_STATUS_DISPLAY_NOT_FOUND;
        uint32_t *fb = (uint32_t *)calloc((size_t)w * h, 4u);
        if (!fb) return BALLUM_STATUS_BACKEND_ERROR;

        BallumWindow *win = [[BallumWindow alloc] initWithContentRect:frame
                                styleMask:NSWindowStyleMaskBorderless
                                backing:NSBackingStoreBuffered defer:NO screen:screen];
        if (!win) { free(fb); return BALLUM_STATUS_BACKEND_ERROR; }
        [win setReleasedWhenClosed:NO];
        [win setLevel:CGShieldingWindowLevel()];
        [win setOpaque:YES];
        [win setBackgroundColor:[NSColor blackColor]];
        [win setAcceptsMouseMovedEvents:YES];
        [win setCollectionBehavior:NSWindowCollectionBehaviorCanJoinAllSpaces |
                                   NSWindowCollectionBehaviorFullScreenAuxiliary];
        BallumView *view = [[BallumView alloc] initWithFrame:[[win contentView] bounds]];
        [win setContentView:view];

        g_s.display = id; g_s.w = w; g_s.h = h; g_s.scale = scale; g_s.fb = fb;
        g_s.window = win; g_s.view = view; g_s.active = 1;
        [win makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
        pump_events();
    }
    return BALLUM_STATUS_OK;
}

static ballum_status_t macos_present(void *ctx, uint32_t id) {
    (void)ctx;
    if (!g_s.active || g_s.display != id) return BALLUM_STATUS_UNSUPPORTED;
    @autoreleasepool {
        [g_s.view setNeedsDisplay:YES];
        [g_s.view displayIfNeeded];
    }
    pump_events();
    return BALLUM_STATUS_OK;
}

static ballum_status_t macos_release_surface(void *ctx, uint32_t id) {
    (void)ctx;
    if (!g_s.active || g_s.display != id) return BALLUM_STATUS_UNSUPPORTED;
    @autoreleasepool { surface_teardown(); }
    return BALLUM_STATUS_OK;
}

static ballum_status_t macos_poll_event(void *ctx, ballum_pointer_event_t *out) {
    (void)ctx;
    if (!out) return BALLUM_STATUS_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    out->type = BALLUM_EVENT_NONE;
    if (!g_s.active) return BALLUM_STATUS_UNSUPPORTED; /* needs Accessibility + event tap otherwise */
    if (g_pq_h == g_pq_t) pump_events();
    if (g_pq_h != g_pq_t) { *out = g_pq[g_pq_h]; g_pq_h = (g_pq_h + 1) % BQ; }
    return BALLUM_STATUS_OK;
}

static ballum_status_t macos_poll_key_event(void *ctx, ballum_key_event_t *out) {
    (void)ctx;
    if (!out) return BALLUM_STATUS_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if (!g_s.active) return BALLUM_STATUS_UNSUPPORTED;
    if (g_kq_h == g_kq_t) pump_events();
    if (g_kq_h != g_kq_t) { *out = g_kq[g_kq_h]; g_kq_h = (g_kq_h + 1) % BQ; }
    return BALLUM_STATUS_OK;
}

/* Whole-screen pointer capture requires a CGEventTap and Accessibility
 * permission, so pointer events remain unavailable here. Escape polling is a
 * separate, non-blocking Core Graphics key-state query. Key-down edges prevent
 * one held key from triggering both stages of SLBallumSession's exit flow. */
static ballum_status_t macos_poll_key(void *ctx, int *out) {
    static int was_escape_down;
    static int was_enter_down;
    static int was_space_down;
    int escape_down, enter_down, space_down;
    (void)ctx;
    if (!out) return BALLUM_STATUS_INVALID_ARGUMENT;
    *out = BALLUM_KEY_NONE;
    /* Virtual key codes: Escape=53, Return=36, Space=49. */
    escape_down = CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState, 53);
    enter_down = CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState, 36);
    space_down = CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState, 49);
    if (escape_down && !was_escape_down) *out = BALLUM_KEY_ESC;
    else if (enter_down && !was_enter_down) *out = BALLUM_KEY_ENTER;
    else if (space_down && !was_space_down) *out = BALLUM_KEY_SPACE;
    was_escape_down = escape_down;
    was_enter_down = enter_down;
    was_space_down = space_down;
    return BALLUM_STATUS_OK;
}

static const ballum_backend_t k_macos = {
    "macos-coregraphics",
    macos_enumerate, macos_set_pixel, macos_get_pixel, macos_blit,
    macos_set_brightness, macos_set_warmth, macos_set_gamma, macos_poll_event,
    macos_poll_key,
    macos_claim_surface, macos_present, macos_release_surface,
    macos_poll_key_event, macos_set_input_grab
};

extern "C" const ballum_backend_t *ballum_backend_macos(void) { return &k_macos; }

#else  /* !BALLUM_BACKEND_MACOS */

typedef int ballum_macos_translation_unit_not_selected;

#endif /* BALLUM_BACKEND_MACOS */
