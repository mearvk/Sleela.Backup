#ifndef SLEELA_BALLUM_VM_BRIDGE_H
#define SLEELA_BALLUM_VM_BRIDGE_H
#ifdef __cplusplus
extern "C" {
#endif
/*
 * VM-safe facade over the native Ballum display-control core (ballum/).
 *
 * The uiBallum* SLVM built-ins lower to OP_UI_CALL and route through
 * sleela_ui_vm.c into these flat entry points, which drive the single process
 * ballum_manager_t over the compiled-in per-OS backend. Returned strings are
 * borrowed until the next call to the same accessor. Every geometry/control
 * request is validated by the native manager (bounds, capability, range) before
 * it touches the device; a backend that cannot perform an operation yields a
 * clean status code, never a false success.
 *
 * All ids are the backend-stable display ids reported by enumerate.
 */
int         sleela_ballum_vm_supported(void);                 /* 1 real backend, 0 fallback          */
const char *sleela_ballum_vm_backend_name(void);              /* compiled-in backend name            */
int         sleela_ballum_vm_enumerate(void);                 /* refresh display list -> status      */
int         sleela_ballum_vm_display_count(void);             /* cached display count                */
int         sleela_ballum_vm_display_id(int index);           /* id of cached display #index, or -1  */
const char *sleela_ballum_vm_display_name(int index);         /* name of cached display #index       */
int         sleela_ballum_vm_display_width(int index);        /* width px, or 0                      */
int         sleela_ballum_vm_display_height(int index);       /* height px, or 0                     */
int         sleela_ballum_vm_display_is_primary(int index);   /* 1 primary, 0 not, -1 bad index      */
int         sleela_ballum_vm_display_caps(int index);         /* capability bitmask, or 0            */
int         sleela_ballum_vm_set_pixel(int display_id, int x, int y,
                                       int r, int g, int b, int a); /* -> status                     */
int         sleela_ballum_vm_get_pixel(int display_id, int x, int y); /* packed 0xAARRGGBB, or -1    */
int         sleela_ballum_vm_set_brightness(int display_id, int percent);  /* -> status              */
int         sleela_ballum_vm_set_warmth(int display_id, int kelvin);       /* -> status              */
int         sleela_ballum_vm_set_gamma(int display_id, int gamma_scaled);  /* -> status              */
const char *sleela_ballum_vm_status_name(int status);         /* human-readable status text          */
/* Fullscreen surface: total, owned, fast per-pixel control. claim_surface()
 * creates an override-redirect fullscreen framebuffer the program owns; while
 * claimed, set_pixel/get_pixel/logo_blend write to the framebuffer (no X
 * round-trips) and present() pushes the whole frame in one XPutImage (MIT-SHM
 * when available). A backend without the capability yields a clean status and
 * has_surface() reports 0 -- fail closed, never a false success. */
int         sleela_ballum_vm_has_surface(int display_id);     /* 1 claimable, 0 not                  */
int         sleela_ballum_vm_claim_surface(int display_id);   /* -> status                           */
int         sleela_ballum_vm_present(int display_id);         /* -> status (push framebuffer)        */
int         sleela_ballum_vm_release_surface(int display_id); /* -> status                           */
/* Pointer input (mouse/touch). poll() dequeues one event into a VM-local latch
 * and returns its type (0 none, 1 down, 2 move, 3 up); the accessors read the
 * latched event. has_input() reports whether the backend supplies any input. */
int         sleela_ballum_vm_has_input(void);                 /* 1 input available, 0 not            */
int         sleela_ballum_vm_poll(void);                      /* latch one event -> type (0..3)      */
int         sleela_ballum_vm_event_x(void);                   /* latched event pixel x               */
int         sleela_ballum_vm_event_y(void);                   /* latched event pixel y               */
int         sleela_ballum_vm_event_button(void);             /* 0 none,1 left,2 middle,3 right       */
int         sleela_ballum_vm_event_source(void);             /* 0 mouse, 1 touch                     */
int         sleela_ballum_vm_event_contact(void);            /* touch contact id (0 for mouse)       */
int         sleela_ballum_vm_event_modifiers(void);          /* BALLUM_MOD_* bitmask                 */
const char *sleela_ballum_vm_event_type_name(int type);      /* "none"/"down"/"move"/"up"            */
/* Keyboard input. poll_key() dequeues one key and returns its ballum_key_t code
 * (0 none; 27 ESC; 13 ENTER; 32 SPACE; printable ASCII as itself). has_keyboard()
 * reports whether the backend supplies keys; a backend without a keyboard queue
 * returns 0 keys -- fail closed, never a false key. */
int         sleela_ballum_vm_has_keyboard(void);             /* 1 keyboard available, 0 not          */
int         sleela_ballum_vm_poll_key(void);                 /* dequeue one key -> code (0 none)     */
/* Logo splash compositing hook, used by a full-screen SLBallumSession. The host
 * (where an image decoder is available) decodes the session's logo and blends
 * it over the background at a given opacity; without a decoder these are honest
 * no-ops -- logo_ready() reports 0 and logo_blend() does nothing and returns a
 * clean status, so the session's white fill + fade timeline + ESC still run and
 * never report a false success. */
int         sleela_ballum_vm_logo_ready(void);               /* 1 logo decoded + loaded, 0 not */
int         sleela_ballum_vm_logo_load(const char *path);    /* decode a JPEG logo from a file -> status */
int         sleela_ballum_vm_logo_set_effect(int fx);        /* choose a transition effect (ballum_fx_t) -> status */
int         sleela_ballum_vm_logo_blend(int display_id, int opacity_percent,
                                        int screen_w, int screen_h,
                                        int bg_r, int bg_g, int bg_b); /* composite one frame -> status */
int         sleela_ballum_vm_fx_count(void);                 /* number of transition effects */
const char *sleela_ballum_vm_fx_name(int fx);                /* effect name, e.g. "iris-in" */
int         sleela_ballum_vm_fx_by_name(const char *name);   /* effect id for a name (fade if unknown) */
/* Program host: run a terminal or admin UI inside the Ballum environment (see
 * ballum/HOST.md). One session at a time. start*() claims the display, shows the
 * white environment and launches the program on a private display; step() is
 * called once per frame and returns the session state. Every call validates its
 * arguments and returns a ballum_status_t (0 ok) or a state/reason code; nothing
 * reports success it did not achieve. Returned strings are static. */
int         sleela_ballum_vm_host_profile_count(void);              /* number of starter profiles      */
const char *sleela_ballum_vm_host_profile_name(int index);          /* "" if out of range              */
const char *sleela_ballum_vm_host_profile_title(int index);         /* text for the APP NAME slot      */
int         sleela_ballum_vm_host_transition_count(void);           /* selectable entry/exit effects   */
const char *sleela_ballum_vm_host_transition_name(int index);       /* "" if out of range              */
int         sleela_ballum_vm_host_configure(int header_height, int enter_ms, int max_seconds); /* -> status; before start */
int         sleela_ballum_vm_host_start(int display_id, const char *profile,
                                        const char *enter, const char *leave);          /* -> status */
int         sleela_ballum_vm_host_start_program(int display_id, const char *program,
                                        const char *args, const char *enter,
                                        const char *leave);          /* args split on spaces; -> status */
int         sleela_ballum_vm_host_step(void);                       /* advance one frame -> state      */
int         sleela_ballum_vm_host_state(void);                      /* 0 idle 1 starting 2 entering 3 running 4 leaving 5 done */
const char *sleela_ballum_vm_host_state_name(int state);
int         sleela_ballum_vm_host_exit_reason(void);                /* ballum_exit_reason_t            */
const char *sleela_ballum_vm_host_exit_reason_name(int reason);
int         sleela_ballum_vm_host_stop(void);                       /* end the session at once -> status */
/* Character sourcing, observation and the interpreter (see ballum/HOST.md).
 *
 * The program host can SEE what a hosted program draws (observe), turn glyph
 * images into characters with a character source, and act on elements through the
 * interpreter. SLeeLa configures this BEFORE a session starts, then reads what was
 * seen and sets standing rules; the native host applies the rules to every
 * interpreted frame. Calls return a ballum_status_t (0 ok) unless stated. */
int         sleela_ballum_vm_charset_count(void);                      /* known charsets                       */
const char *sleela_ballum_vm_charset_name(int index);                  /* "" if out of range                   */
int         sleela_ballum_vm_charset_size(int index);                  /* characters in it, 0 if out of range  */
int         sleela_ballum_vm_charsource_clear(void);
int         sleela_ballum_vm_charsource_load_pgm(const char *path, int cell_w, int cell_h,
                                                 const char *charset, int family, int light_ink);   /* -> status */
int         sleela_ballum_vm_charsource_glyph_count(void);
int         sleela_ballum_vm_contact_count(void);                      /* the 23 contact points                */
const char *sleela_ballum_vm_contact_name(int index);
int         sleela_ballum_vm_contact_confidence(int index);            /* design rating 0..1000, -1 if invalid */
int         sleela_ballum_vm_host_observe(int on);                     /* before start                          */
int         sleela_ballum_vm_host_interpret(int mode);                 /* 0 off, 1 notation, 2 alteration; before start */
int         sleela_ballum_vm_host_band(int axis, int low, int high);   /* 0 morals 1 cleanliness 2 throughput 3 confidence */
int         sleela_ballum_vm_host_scores(int morals, int cleanliness, int throughput, int confidence);
int         sleela_ballum_vm_host_rule(const char *match, const char *point, int value, int rgb);   /* add a standing rule */
int         sleela_ballum_vm_host_rule_clear(void);
int         sleela_ballum_vm_host_rule_count(void);
int         sleela_ballum_vm_host_element_count(void);                 /* elements in the last interpreted frame */
const char *sleela_ballum_vm_host_element_text(int index);
int         sleela_ballum_vm_host_element_kind(int index);             /* 0 text 1 fill 2 copy 3 image, -1 bad  */
int         sleela_ballum_vm_host_element_x(int index);
int         sleela_ballum_vm_host_element_y(int index);
int         sleela_ballum_vm_host_element_w(int index);
int         sleela_ballum_vm_host_element_h(int index);
int         sleela_ballum_vm_host_element_known(int index);            /* 1 when every character was identified */
int         sleela_ballum_vm_host_stat(int which);                     /* 0 frames 1 elements 2 applied 3 blocked 4 pixels 5 observing 6 glyphs 7 glyphs known */
#ifdef __cplusplus
}
#endif
#endif
