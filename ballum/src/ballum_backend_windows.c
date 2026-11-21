/*
 * Ballum Windows 10+ backend -- Win32 + GDI, a borrowed-nothing graphic base.
 *
 * - enumerate       : EnumDisplayMonitors -> displays, physical-pixel bounds and the
 *                     current refresh rate (EnumDisplaySettings). The process is made
 *                     per-monitor DPI aware (v2, falling back to system-aware) before
 *                     any window exists, so a display's width and height are REAL
 *                     pixels, not scaled units.
 * - fullscreen surface (claim_surface / present / release_surface):
 *                     a borderless, topmost popup window over the monitor with a
 *                     32-bit top-down DIB section as the framebuffer. While claimed,
 *                     set_pixel / get_pixel / blit_region are plain memory writes and
 *                     present() is one BitBlt, exactly like the X11 fullscreen path.
 * - input           : the window's own message pump, drained non-blockingly by the
 *                     poll calls (single thread, the one that claimed the surface).
 *                     Pointer: move, buttons 1-3, wheel notches (4-7), with the
 *                     modifier state. Keyboard: every key press and release, with key
 *                     repeat reported as release+press pairs. Key symbols are in the
 *                     X11 keysym vocabulary the host expects (Escape is 0xFF1B).
 * - input grab      : set_input_grab confines the pointer to the surface with
 *                     ClipCursor and brings the window to the foreground. It is
 *                     revocable, dropped when the window loses the foreground, and
 *                     released on teardown. It does not and cannot intercept
 *                     Ctrl+Alt+Del or the Windows key combinations the shell owns, so
 *                     a hung session can always be left by the operating system.
 * - set/get_pixel, blit_region without a surface: SetPixel / GetPixel on the
 *                     screen DC (transient; the compositor may repaint).
 * - set_brightness / set_warmth / set_gamma: SetDeviceGammaRamp on the monitor's own DC.
 *
 * STATUS: written against the documented Win32 API and checked for syntax against a
 * hand-written subset of <windows.h>; it has NOT been built with a real Windows
 * toolchain or run on Windows here. The CI workflow builds it with MinGW on a
 * Windows runner; run `make test` and `ballum-host --list` there first.
 *
 * Only meaningful when built with BALLUM_BACKEND_WINDOWS (the Makefile adds
 * -lgdi32 -luser32 on Windows). Without the define this is an empty TU so
 * non-Windows builds never need the Win32 SDK.
 */

#include "sleela_ballum/ballum_manager.h"

#if defined(BALLUM_BACKEND_WINDOWS)

#ifndef WINVER
#define WINVER 0x0A00          /* Windows 10 */
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <windowsx.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define MAX_MON 16
#define QCAP    256u

/* ---- the monitors ------------------------------------------------------------------ */

typedef struct { uint32_t id; RECT rc; char device[CCHDEVICENAME]; } mon_t;
static mon_t   g_mon[MAX_MON];
static size_t  g_nmon;
static int     g_dpi_done;

static void make_dpi_aware(void) {
    if (g_dpi_done) return;
    g_dpi_done = 1;
    HMODULE u = GetModuleHandleA("user32.dll");
    typedef BOOL (WINAPI *set_ctx_fn)(HANDLE);
    typedef BOOL (WINAPI *set_aware_fn)(void);
    FARPROC p = u ? GetProcAddress(u, "SetProcessDpiAwarenessContext") : NULL;
    if (p) {
        set_ctx_fn fn;
        memcpy(&fn, &p, sizeof(fn));
        if (fn((HANDLE)-4)) return;                              /* PER_MONITOR_AWARE_V2 */
    }
    FARPROC q = u ? GetProcAddress(u, "SetProcessDPIAware") : NULL;
    if (q) { set_aware_fn fn2; memcpy(&fn2, &q, sizeof(fn2)); fn2(); }
}

typedef struct {
    ballum_display_t *out;
    size_t capacity;
    size_t count;
} enum_ctx_t;

static BOOL CALLBACK monitor_cb(HMONITOR mon, HDC hdc, LPRECT rect, LPARAM lp) {
    (void)hdc; (void)rect;
    enum_ctx_t *e = (enum_ctx_t *)lp;
    if (e->count >= e->capacity || e->count >= BALLUM_MAX_DISPLAYS || e->count >= MAX_MON) return FALSE;
    MONITORINFOEXA mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoA(mon, (MONITORINFO *)&mi)) return TRUE;
    ballum_display_t d;
    memset(&d, 0, sizeof(d));
    d.id = (uint32_t)(e->count + 1);
    strncpy(d.name, mi.szDevice, sizeof(d.name) - 1);
    d.width = (uint32_t)(mi.rcMonitor.right - mi.rcMonitor.left);
    d.height = (uint32_t)(mi.rcMonitor.bottom - mi.rcMonitor.top);
    DEVMODEA dm;
    memset(&dm, 0, sizeof(dm));
    dm.dmSize = sizeof(dm);
    if (EnumDisplaySettingsA(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1)
        d.refresh_mhz = (uint32_t)dm.dmDisplayFrequency * 1000u;
    d.is_primary = (mi.dwFlags & MONITORINFOF_PRIMARY) ? 1 : 0;
    d.capabilities = BALLUM_CAP_PER_PIXEL | BALLUM_CAP_REGION_BLIT |
                     BALLUM_CAP_BRIGHTNESS | BALLUM_CAP_COLOR_TEMPERATURE |
                     BALLUM_CAP_GAMMA | BALLUM_CAP_POINTER_INPUT | BALLUM_CAP_FULLSCREEN_SURFACE;
    g_mon[e->count].id = d.id;
    g_mon[e->count].rc = mi.rcMonitor;
    strncpy(g_mon[e->count].device, mi.szDevice, sizeof(g_mon[e->count].device) - 1);
    e->out[e->count++] = d;
    return TRUE;
}

static ballum_status_t win_enumerate(void *ctx, ballum_display_t *out, size_t capacity, size_t *out_count) {
    (void)ctx;
    make_dpi_aware();
    enum_ctx_t e;
    e.out = out; e.capacity = capacity; e.count = 0;
    memset(g_mon, 0, sizeof(g_mon));
    EnumDisplayMonitors(NULL, NULL, monitor_cb, (LPARAM)&e);
    g_nmon = e.count;
    if (out_count) *out_count = e.count;
    return BALLUM_STATUS_OK;
}

static const mon_t *find_mon(uint32_t id) {
    for (size_t i = 0; i < g_nmon; ++i) if (g_mon[i].id == id) return &g_mon[i];
    return NULL;
}

/* ---- the surface and its input ----------------------------------------------------------- */

typedef struct {
    int       active;
    uint32_t  output;
    HWND      hwnd;
    HDC       memdc;
    HBITMAP   dib, old;
    uint32_t *fb;                    /* w*h, 0x00RRGGBB (memory order B,G,R,X) */
    int       x, y;                  /* origin on the virtual desktop */
    uint32_t  w, h;
    int       grabbed;
    uint32_t  buttons;               /* mouse buttons held, bit n = button n */
} surf_t;

static surf_t g_s;
static int    g_class_registered;
static const char k_class[] = "BallumSurface";

static ballum_pointer_event_t g_pq[QCAP]; static unsigned g_pq_head, g_pq_n;
static ballum_key_event_t     g_kq[QCAP]; static unsigned g_kq_head, g_kq_n;

static void pq_push(const ballum_pointer_event_t *e) {
    if (e->type == BALLUM_EVENT_POINTER_MOVE && g_pq_n > 0) {            /* coalesce motion */
        unsigned last = (g_pq_head + g_pq_n - 1u) % QCAP;
        if (g_pq[last].type == BALLUM_EVENT_POINTER_MOVE) { g_pq[last] = *e; return; }
    }
    if (g_pq_n == QCAP) {
        if (e->type == BALLUM_EVENT_POINTER_MOVE) return;
        g_pq_head = (g_pq_head + 1u) % QCAP; g_pq_n--;
    }
    g_pq[(g_pq_head + g_pq_n) % QCAP] = *e; g_pq_n++;
}
static void kq_push(const ballum_key_event_t *e) {
    if (g_kq_n == QCAP) { g_kq_head = (g_kq_head + 1u) % QCAP; g_kq_n--; }
    g_kq[(g_kq_head + g_kq_n) % QCAP] = *e; g_kq_n++;
}

static uint32_t cur_mods(void) {
    uint32_t m = 0;
    if (GetKeyState(VK_SHIFT) & 0x8000) m |= BALLUM_MOD_SHIFT;
    if (GetKeyState(VK_CONTROL) & 0x8000) m |= BALLUM_MOD_CTRL;
    if (GetKeyState(VK_MENU) & 0x8000) m |= BALLUM_MOD_ALT;
    if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) m |= BALLUM_MOD_SUPER;
    return m;
}

/* Windows virtual key -> X11 keysym, the vocabulary the host and its sources use.
 * Printable keys give the UNSHIFTED character (a keysym for a Latin-1 character is
 * its code), exactly as the X11 backend does. */
static uint32_t vk_to_keysym(WPARAM vk, LPARAM lp) {
    int extended = (lp >> 24) & 1;
    unsigned scan = (unsigned)((lp >> 16) & 0xFF);
    if (vk >= 'A' && vk <= 'Z') return (uint32_t)(vk - 'A' + 'a');
    if (vk >= '0' && vk <= '9') return (uint32_t)vk;
    if (vk >= VK_F1 && vk <= VK_F24) return 0xFFBEu + (uint32_t)(vk - VK_F1);
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return 0xFFB0u + (uint32_t)(vk - VK_NUMPAD0);
    switch (vk) {
    case VK_BACK:     return 0xFF08u;
    case VK_TAB:      return 0xFF09u;
    case VK_RETURN:   return extended ? 0xFF8Du : 0xFF0Du;
    case VK_PAUSE:    return 0xFF13u;
    case VK_ESCAPE:   return 0xFF1Bu;
    case VK_SPACE:    return 0x20u;
    case VK_PRIOR:    return 0xFF55u;
    case VK_NEXT:     return 0xFF56u;
    case VK_END:      return 0xFF57u;
    case VK_HOME:     return 0xFF50u;
    case VK_LEFT:     return 0xFF51u;
    case VK_UP:       return 0xFF52u;
    case VK_RIGHT:    return 0xFF53u;
    case VK_DOWN:     return 0xFF54u;
    case VK_INSERT:   return 0xFF63u;
    case VK_DELETE:   return 0xFFFFu;
    case VK_SNAPSHOT: return 0xFF61u;
    case VK_APPS:     return 0xFF67u;
    case VK_CAPITAL:  return 0xFFE5u;
    case VK_NUMLOCK:  return 0xFF7Fu;
    case VK_SCROLL:   return 0xFF14u;
    case VK_LWIN:     return 0xFFEBu;
    case VK_RWIN:     return 0xFFECu;
    case VK_MULTIPLY: return 0xFFAAu;
    case VK_ADD:      return 0xFFABu;
    case VK_SUBTRACT: return 0xFFADu;
    case VK_DECIMAL:  return 0xFFAEu;
    case VK_DIVIDE:   return 0xFFAFu;
    case VK_SHIFT:    return scan == 0x36 ? 0xFFE2u : 0xFFE1u;                 /* right / left by scan code */
    case VK_CONTROL:  return extended ? 0xFFE4u : 0xFFE3u;
    case VK_MENU:     return extended ? 0xFFEAu : 0xFFE9u;
    default: break;
    }
    UINT ch = MapVirtualKeyA((UINT)vk, MAPVK_VK_TO_CHAR) & 0x7FFFFFFFu;          /* punctuation, unshifted */
    return (ch >= 0x20 && ch < 0x7F) ? (uint32_t)ch : 0u;
}

static void push_key(WPARAM vk, LPARAM lp, int down) {
    ballum_key_event_t k;
    memset(&k, 0, sizeof(k));
    k.symbol = vk_to_keysym(vk, lp);
    k.code = (uint32_t)((lp >> 16) & 0xFF) | (((lp >> 24) & 1) ? 0x100u : 0u);
    if (k.code == 0) k.code = (uint32_t)vk | 0x200u;                              /* never 0: 0 means "nothing pending" */
    k.pressed = down;
    k.modifiers = cur_mods();
    if (down && ((lp >> 30) & 1)) {                                               /* auto-repeat: release, then press */
        ballum_key_event_t up = k; up.pressed = 0; kq_push(&up);
    }
    kq_push(&k);
}

static void push_pointer(ballum_event_type_t type, int x, int y, ballum_pointer_button_t b) {
    ballum_pointer_event_t e;
    memset(&e, 0, sizeof(e));
    e.type = type; e.display_id = g_s.output; e.x = x; e.y = y; e.button = b;
    e.source = BALLUM_POINTER_MOUSE; e.modifiers = cur_mods();
    pq_push(&e);
}

static void release_grab(void) {
    if (g_s.grabbed) { ClipCursor(NULL); g_s.grabbed = 0; }
}

static void button_event(ballum_pointer_button_t b, int down, LPARAM lp) {
    if (down) { if (!g_s.buttons) SetCapture(g_s.hwnd); g_s.buttons |= 1u << (int)b; }
    else      { g_s.buttons &= ~(1u << (int)b); if (!g_s.buttons) ReleaseCapture(); }
    push_pointer(down ? BALLUM_EVENT_POINTER_DOWN : BALLUM_EVENT_POINTER_UP, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), b);
}

static void wheel_event(WPARAM wp, LPARAM lp, int horizontal) {
    static int acc_v, acc_h;
    int *acc = horizontal ? &acc_h : &acc_v;
    *acc += GET_WHEEL_DELTA_WPARAM(wp);
    POINT pt; pt.x = GET_X_LPARAM(lp); pt.y = GET_Y_LPARAM(lp);                    /* wheel messages carry SCREEN coordinates */
    ScreenToClient(g_s.hwnd, &pt);
    for (int n = 0; *acc >= WHEEL_DELTA && n < 8; ++n, *acc -= WHEEL_DELTA) {
        ballum_pointer_button_t b = horizontal ? BALLUM_BUTTON_WHEEL_RIGHT : BALLUM_BUTTON_WHEEL_UP;
        push_pointer(BALLUM_EVENT_POINTER_DOWN, pt.x, pt.y, b); push_pointer(BALLUM_EVENT_POINTER_UP, pt.x, pt.y, b);
    }
    for (int n = 0; *acc <= -WHEEL_DELTA && n < 8; ++n, *acc += WHEEL_DELTA) {
        ballum_pointer_button_t b = horizontal ? BALLUM_BUTTON_WHEEL_LEFT : BALLUM_BUTTON_WHEEL_DOWN;
        push_pointer(BALLUM_EVENT_POINTER_DOWN, pt.x, pt.y, b); push_pointer(BALLUM_EVENT_POINTER_UP, pt.x, pt.y, b);
    }
}

static void paint_surface(HDC dc) {
    if (g_s.active && g_s.memdc) BitBlt(dc, 0, 0, (int)g_s.w, (int)g_s.h, g_s.memdc, 0, 0, SRCCOPY);
}

static LRESULT CALLBACK surf_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND:   return 1;                                             /* the framebuffer is everything */
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        paint_surface(dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_CLOSE:        return 0;                                             /* only the host decides when it ends */
    case WM_SYSCOMMAND:
        if ((wp & 0xFFF0) == SC_KEYMENU || (wp & 0xFFF0) == SC_CLOSE) return 0; /* Alt must not open a menu, Alt+F4 must not close */
        break;
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE) release_grab();                          /* never hold the pointer in a window that lost focus */
        break;
    case WM_KILLFOCUS:    release_grab(); break;
    case WM_MOUSEMOVE:    push_pointer(BALLUM_EVENT_POINTER_MOVE, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), BALLUM_BUTTON_NONE); return 0;
    case WM_LBUTTONDOWN:  button_event(BALLUM_BUTTON_LEFT, 1, lp);   return 0;
    case WM_LBUTTONUP:    button_event(BALLUM_BUTTON_LEFT, 0, lp);   return 0;
    case WM_MBUTTONDOWN:  button_event(BALLUM_BUTTON_MIDDLE, 1, lp); return 0;
    case WM_MBUTTONUP:    button_event(BALLUM_BUTTON_MIDDLE, 0, lp); return 0;
    case WM_RBUTTONDOWN:  button_event(BALLUM_BUTTON_RIGHT, 1, lp);  return 0;
    case WM_RBUTTONUP:    button_event(BALLUM_BUTTON_RIGHT, 0, lp);  return 0;
    case WM_MOUSEWHEEL:   wheel_event(wp, lp, 0); return 0;
    case WM_MOUSEHWHEEL:  wheel_event(wp, lp, 1); return 0;
    case WM_KEYDOWN: case WM_SYSKEYDOWN: push_key(wp, lp, 1); return 0;
    case WM_KEYUP:   case WM_SYSKEYUP:   push_key(wp, lp, 0); return 0;
    default: break;
    }
    return DefWindowProcA(h, msg, wp, lp);
}

static void pump_messages(void) {
    MSG m;
    while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) {
        if (m.message == WM_QUIT) continue;
        DispatchMessageA(&m);                           /* no TranslateMessage: keysyms come from the virtual keys */
    }
}

static void surface_teardown(void) {
    release_grab();
    if (g_s.memdc && g_s.old) SelectObject(g_s.memdc, g_s.old);
    if (g_s.dib) DeleteObject(g_s.dib);
    if (g_s.memdc) DeleteDC(g_s.memdc);
    if (g_s.hwnd) DestroyWindow(g_s.hwnd);
    memset(&g_s, 0, sizeof(g_s));
    g_pq_head = g_pq_n = g_kq_head = g_kq_n = 0;
}

static ballum_status_t win_claim_surface(void *ctx, uint32_t id) {
    (void)ctx;
    make_dpi_aware();
    const mon_t *m = find_mon(id);
    if (!m) return BALLUM_STATUS_DISPLAY_NOT_FOUND;
    if (g_s.active) surface_teardown();
    int w = m->rc.right - m->rc.left, h = m->rc.bottom - m->rc.top;
    if (w <= 0 || h <= 0) return BALLUM_STATUS_DISPLAY_NOT_FOUND;

    HINSTANCE inst = GetModuleHandleA(NULL);
    if (!g_class_registered) {
        WNDCLASSEXA wc;
        memset(&wc, 0, sizeof(wc));
        wc.cbSize = sizeof(wc);
        wc.style = CS_OWNDC;
        wc.lpfnWndProc = surf_proc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
        wc.lpszClassName = k_class;
        if (!RegisterClassExA(&wc)) return BALLUM_STATUS_BACKEND_ERROR;
        g_class_registered = 1;
    }
    HWND hwnd = CreateWindowExA(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, k_class, "Ballum", WS_POPUP,
                                m->rc.left, m->rc.top, w, h, NULL, NULL, inst, NULL);
    if (!hwnd) return BALLUM_STATUS_BACKEND_ERROR;

    HDC wdc = GetDC(hwnd);
    HDC mdc = wdc ? CreateCompatibleDC(wdc) : NULL;
    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;                         /* top-down */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void *bits = NULL;
    HBITMAP dib = mdc ? CreateDIBSection(mdc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0) : NULL;
    if (wdc) ReleaseDC(hwnd, wdc);
    if (!mdc || !dib || !bits) {
        if (dib) DeleteObject(dib);
        if (mdc) DeleteDC(mdc);
        DestroyWindow(hwnd);
        return BALLUM_STATUS_BACKEND_ERROR;
    }
    memset(&g_s, 0, sizeof(g_s));
    g_s.hwnd = hwnd; g_s.memdc = mdc; g_s.dib = dib; g_s.old = (HBITMAP)SelectObject(mdc, dib);
    g_s.fb = (uint32_t *)bits; g_s.x = m->rc.left; g_s.y = m->rc.top; g_s.w = (uint32_t)w; g_s.h = (uint32_t)h;
    g_s.output = id; g_s.active = 1;
    memset(g_s.fb, 0, (size_t)w * (size_t)h * 4u);

    SetWindowPos(hwnd, HWND_TOPMOST, m->rc.left, m->rc.top, w, h, SWP_SHOWWINDOW | SWP_FRAMECHANGED);
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
    pump_messages();
    return BALLUM_STATUS_OK;
}

static ballum_status_t win_present(void *ctx, uint32_t id) {
    (void)ctx; (void)id;
    if (!g_s.active) return BALLUM_STATUS_UNSUPPORTED;
    HDC dc = GetDC(g_s.hwnd);
    if (!dc) return BALLUM_STATUS_BACKEND_ERROR;
    BOOL ok = BitBlt(dc, 0, 0, (int)g_s.w, (int)g_s.h, g_s.memdc, 0, 0, SRCCOPY);
    ReleaseDC(g_s.hwnd, dc);
    ValidateRect(g_s.hwnd, NULL);
    return ok ? BALLUM_STATUS_OK : BALLUM_STATUS_BACKEND_ERROR;
}

static ballum_status_t win_release_surface(void *ctx, uint32_t id) {
    (void)ctx; (void)id;
    if (g_s.active) surface_teardown();
    return BALLUM_STATUS_OK;
}

static ballum_status_t win_set_input_grab(void *ctx, int on) {
    (void)ctx;
    if (!g_s.active) return BALLUM_STATUS_UNSUPPORTED;
    if (!on) { release_grab(); return BALLUM_STATUS_OK; }
    if (g_s.grabbed) return BALLUM_STATUS_OK;
    SetForegroundWindow(g_s.hwnd);
    SetFocus(g_s.hwnd);
    RECT rc;
    rc.left = g_s.x; rc.top = g_s.y; rc.right = g_s.x + (LONG)g_s.w; rc.bottom = g_s.y + (LONG)g_s.h;
    if (!ClipCursor(&rc)) return BALLUM_STATUS_PERMISSION_DENIED;
    g_s.grabbed = 1;
    return BALLUM_STATUS_OK;
}

static uint32_t pack_xrgb(ballum_color_t c) {
    return ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | (uint32_t)c.b;
}

static int in_surface(int32_t x, int32_t y, size_t *idx) {
    if (!g_s.active || x < 0 || y < 0 || (uint32_t)x >= g_s.w || (uint32_t)y >= g_s.h) return 0;
    *idx = (size_t)y * g_s.w + (size_t)x;
    return 1;
}

/* ---- pixels ----------------------------------------------------------------------------- */

static ballum_status_t win_set_pixel(void *ctx, uint32_t id, ballum_point_t at, ballum_color_t color) {
    (void)ctx; (void)id;
    if (g_s.active) {
        size_t i;
        if (!in_surface((int32_t)at.x, (int32_t)at.y, &i)) return BALLUM_STATUS_OUT_OF_BOUNDS;
        g_s.fb[i] = pack_xrgb(color);
        return BALLUM_STATUS_OK;
    }
    HDC dc = GetDC(NULL);
    if (!dc) return BALLUM_STATUS_BACKEND_ERROR;
    SetPixel(dc, (int)at.x, (int)at.y, RGB(color.r, color.g, color.b));
    ReleaseDC(NULL, dc);
    return BALLUM_STATUS_OK;
}

static ballum_status_t win_get_pixel(void *ctx, uint32_t id, ballum_point_t at, ballum_color_t *out) {
    (void)ctx; (void)id;
    if (!out) return BALLUM_STATUS_INVALID_ARGUMENT;
    if (g_s.active) {
        size_t i;
        if (!in_surface((int32_t)at.x, (int32_t)at.y, &i)) return BALLUM_STATUS_OUT_OF_BOUNDS;
        uint32_t p = g_s.fb[i];
        out->r = (uint8_t)(p >> 16); out->g = (uint8_t)(p >> 8); out->b = (uint8_t)p; out->a = 255;
        return BALLUM_STATUS_OK;
    }
    HDC dc = GetDC(NULL);
    if (!dc) return BALLUM_STATUS_BACKEND_ERROR;
    COLORREF c = GetPixel(dc, (int)at.x, (int)at.y);
    ReleaseDC(NULL, dc);
    if (c == CLR_INVALID) return BALLUM_STATUS_OUT_OF_BOUNDS;
    out->r = GetRValue(c); out->g = GetGValue(c); out->b = GetBValue(c); out->a = 255;
    return BALLUM_STATUS_OK;
}

static ballum_status_t win_blit(void *ctx, uint32_t id, ballum_rect_t rect, const ballum_color_t *pixels, size_t n) {
    (void)ctx; (void)id;
    if (!pixels || n != (size_t)rect.width * rect.height) return BALLUM_STATUS_INVALID_ARGUMENT;
    if (g_s.active) {                                        /* the fast path: memory only until present() */
        for (uint32_t row = 0; row < rect.height; ++row) {
            int64_t y = (int64_t)rect.y + row;
            if (y < 0 || y >= (int64_t)g_s.h) continue;
            for (uint32_t col = 0; col < rect.width; ++col) {
                int64_t x = (int64_t)rect.x + col;
                if (x < 0 || x >= (int64_t)g_s.w) continue;
                g_s.fb[(size_t)y * g_s.w + (size_t)x] = pack_xrgb(pixels[(size_t)row * rect.width + col]);
            }
        }
        return BALLUM_STATUS_OK;
    }
    HDC dc = GetDC(NULL);
    if (!dc) return BALLUM_STATUS_BACKEND_ERROR;
    for (uint32_t row = 0; row < rect.height; ++row)
        for (uint32_t col = 0; col < rect.width; ++col) {
            const ballum_color_t p = pixels[(size_t)row * rect.width + col];
            SetPixel(dc, (int)(rect.x + col), (int)(rect.y + row), RGB(p.r, p.g, p.b));
        }
    ReleaseDC(NULL, dc);
    return BALLUM_STATUS_OK;
}

/* ---- gamma, brightness, warmth ------------------------------------------------------------ */

/* Build and set a 256x3 WORD gamma ramp on the monitor's own device context. */
static ballum_status_t apply_ramp(uint32_t id, double rmul, double gmul, double bmul, double exponent) {
    const mon_t *m = find_mon(id);
    HDC dc = m ? CreateDCA(m->device, NULL, NULL, NULL) : GetDC(NULL);
    if (!dc) return BALLUM_STATUS_BACKEND_ERROR;
    WORD ramp[3][256];
    for (int i = 0; i < 256; ++i) {
        double t = (double)i / 255.0;
        double v = pow(t, exponent);
        double rv = v * rmul; if (rv > 1.0) rv = 1.0;
        double gv = v * gmul; if (gv > 1.0) gv = 1.0;
        double bv = v * bmul; if (bv > 1.0) bv = 1.0;
        ramp[0][i] = (WORD)(rv * 65535.0);
        ramp[1][i] = (WORD)(gv * 65535.0);
        ramp[2][i] = (WORD)(bv * 65535.0);
    }
    BOOL ok = SetDeviceGammaRamp(dc, ramp);
    if (m) DeleteDC(dc); else ReleaseDC(NULL, dc);
    return ok ? BALLUM_STATUS_OK : BALLUM_STATUS_UNSUPPORTED;
}

static ballum_status_t win_set_brightness(void *ctx, uint32_t id, int percent) {
    (void)ctx;
    double m = (double)percent / 100.0;
    return apply_ramp(id, m, m, m, 1.0);
}

static void warmth_multipliers(uint32_t kelvin, double *rm, double *gm, double *bm) {
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
    *rm = r / 255.0; *gm = g / 255.0; *bm = b / 255.0;
}

static ballum_status_t win_set_warmth(void *ctx, uint32_t id, uint32_t kelvin) {
    (void)ctx;
    double rm, gm, bm; warmth_multipliers(kelvin, &rm, &gm, &bm);
    return apply_ramp(id, rm, gm, bm, 1.0);
}

static ballum_status_t win_set_gamma(void *ctx, uint32_t id, uint32_t gamma_scaled) {
    (void)ctx;
    double gamma = (double)gamma_scaled / (double)BALLUM_GAMMA_SCALE;
    if (gamma <= 0.0) return BALLUM_STATUS_INVALID_ARGUMENT;
    return apply_ramp(id, 1.0, 1.0, 1.0, 1.0 / gamma);
}

/* ---- input polling --------------------------------------------------------------------------- */

static ballum_status_t win_poll_event(void *ctx, ballum_pointer_event_t *out) {
    (void)ctx;
    if (!out) return BALLUM_STATUS_INVALID_ARGUMENT;
    out->type = BALLUM_EVENT_NONE;
    if (!g_s.active) return BALLUM_STATUS_OK;                  /* no surface, no pointer events: none pending */
    pump_messages();
    if (g_pq_n == 0) return BALLUM_STATUS_OK;
    *out = g_pq[g_pq_head];
    g_pq_head = (g_pq_head + 1u) % QCAP; g_pq_n--;
    return BALLUM_STATUS_OK;
}

static ballum_status_t win_poll_key_event(void *ctx, ballum_key_event_t *out) {
    (void)ctx;
    if (!out) return BALLUM_STATUS_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if (!g_s.active) return BALLUM_STATUS_OK;
    pump_messages();
    if (g_kq_n == 0) return BALLUM_STATUS_OK;
    *out = g_kq[g_kq_head];
    g_kq_head = (g_kq_head + 1u) % QCAP; g_kq_n--;
    return BALLUM_STATUS_OK;
}

/* Edge-only Escape/Enter/Space for the session controls; independent of any window. */
static ballum_status_t win_poll_key(void *ctx, int *out) {
    static int was_escape_down;
    static int was_enter_down;
    static int was_space_down;
    int escape_down, enter_down, space_down;
    (void)ctx;
    if (!out) return BALLUM_STATUS_INVALID_ARGUMENT;
    *out = BALLUM_KEY_NONE;
    escape_down = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    enter_down = (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;
    space_down = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
    if (escape_down && !was_escape_down) *out = BALLUM_KEY_ESC;
    else if (enter_down && !was_enter_down) *out = BALLUM_KEY_ENTER;
    else if (space_down && !was_space_down) *out = BALLUM_KEY_SPACE;
    was_escape_down = escape_down;
    was_enter_down = enter_down;
    was_space_down = space_down;
    return BALLUM_STATUS_OK;
}

static const ballum_backend_t k_windows = {
    "windows-gdi",
    win_enumerate, win_set_pixel, win_get_pixel, win_blit,
    win_set_brightness, win_set_warmth, win_set_gamma, win_poll_event,
    win_poll_key,
    win_claim_surface, win_present, win_release_surface,
    win_poll_key_event, win_set_input_grab
};

const ballum_backend_t *ballum_backend_windows(void) { return &k_windows; }

#else  /* !BALLUM_BACKEND_WINDOWS */

typedef int ballum_windows_translation_unit_not_selected;

#endif /* BALLUM_BACKEND_WINDOWS */
