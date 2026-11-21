/*
 * ballum-host -- put a program inside the Ballum environment.
 *
 *   ballum-host --list
 *   ballum-host --profile sleela-terminal
 *   ballum-host --profile xterm --enter disintegrate --leave fade
 *   ballum-host [options] -- /usr/bin/xterm -e top
 *
 * The screen turns white, the program enters with the chosen effect, and every
 * key and mouse event goes to it 1:1. Leave with Ctrl+Alt+Esc (or end the
 * program). Exit status: 0 ran and ended, 2 usage, 3 cannot host here.
 */
#include "sleela_ballum/ballum_host.h"
#include "sleela_ballum/ballum_host_profiles.h"
#include "sleela_ballum/ballum_sandbox.h"
#include "sleela_ballum/ballum_charsource.h"
#include "sleela_ballum/ballum_header.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(FILE *f) {
    fprintf(f,
        "usage: ballum-host [options] [-- command [args...]]\n"
        "  --list                 show profiles and transitions\n"
        "  --profile NAME         start a profile (see --list)\n"
        "  --enter NAME           entry transition   (default: profile or disintegrate)\n"
        "  --leave NAME           exit transition    (default: profile or fade)\n"
        "  --enter-ms N           entry duration, ms (default 700)\n"
        "  --display ID           display id         (default: the primary display)\n"
        "  --fps N                frame rate         (default 60)\n"
        "  --max-seconds N        end the session after N seconds\n"
        "  --header N             header height in px (0 = none, default 32)\n"
        "  --title TEXT           app name shown in the header (default: profile or program)\n"
        "  --no-clock             leave the date and time out of the header\n"
        "  --interpret MODE       pass frames through the interpreter: notation (unchanged,\n"
        "                         observed) or alteration (unchanged until altered)\n"
        "  --observe              put the X11 relay between the program and its display, so\n"
        "                         what it draws (text, fills, images) is seen as elements\n"
        "  --census               implies --observe: print what was seen and how much, to stderr\n"
        "                         (the quickest way to check the relay against a real program)\n"
        "  --atlas FILE.pgm       character source: a PGM pixel map to recognise glyphs with\n"
        "  --cell WxH             cell size of the atlas (default 8x16)\n"
        "  --charset NAME         what the atlas holds, in order: ascii latin1 digits alnum\n"
        "  --atlas-light          the atlas has light ink on a dark page\n"
        "  --no-grab              do not grab keyboard and pointer\n"
        "  --no-sandbox           run the program without the seccomp/no_new_privs layer\n"
        "Leave the session with Ctrl+Alt+Esc.\n");
}

static void list_all(void) {
    puts("profiles:");
    for (size_t i = 0; i < ballum_profile_count(); ++i) {
        const ballum_profile_t *p = ballum_profile_at(i);
        printf("  %-16s %s  [enter %s, leave %s]\n", p->name, p->description, p->enter, p->leave);
    }
    puts("transitions:");
    printf(" ");
    for (int i = 0; i < ballum_transition_count(); ++i) printf(" %s", ballum_transition_name_at(i));
    putchar('\n');
}

static int parse_uint(const char *s, unsigned long max, unsigned long *out) {
    char *end = NULL;
    errno = 0;
    unsigned long v = strtoul(s, &end, 10);
    if (errno || end == s || *end || v > max) return 0;
    *out = v;
    return 1;
}

/* --census: show what the host saw, a few times, so the relay can be checked against
 * a real program without any other tooling. */
static void census_hook(void *user, ballum_interp_t *it, uint64_t now_ms) {
    static int shown;
    static uint64_t last_ms;
    (void)user;
    size_t n = ballum_interp_element_count(it);
    if (n == 0 || shown >= 6 || now_ms - last_ms < 1500) return;
    last_ms = now_ms; ++shown;
    fprintf(stderr, "census: %zu elements in the frame\n", n);
    for (size_t i = 0; i < n && i < 24; ++i) {
        ballum_element_t e;
        if (!ballum_interp_element_at(it, i, &e)) continue;
        static const char *const kinds[] = { "text", "fill", "copy", "image" };
        fprintf(stderr, "  %-5s (%4u,%4u) %4ux%-4u known=%d ink=#%02x%02x%02x%s paper=#%02x%02x%02x%s",
                (unsigned)e.kind < 4u ? kinds[e.kind] : "?", e.rect.x, e.rect.y, e.rect.width, e.rect.height,
                e.glyph_known, e.fg.r, e.fg.g, e.fg.b, e.fg.a ? "" : "?", e.bg.r, e.bg.g, e.bg.b, e.bg.a ? "" : "?");
        if (e.kind == BALLUM_ELEM_TEXT && e.text) fprintf(stderr, "  \"%.*s\"", (int)(e.text_len > 60 ? 60 : e.text_len), e.text);
        fputc('\n', stderr);
    }
}

int main(int argc, char **argv) {
    const char *profile_name = NULL, *enter = NULL, *leave = NULL;
    unsigned long display = 0, fps = 60, max_s = 0, header = 32, enter_ms = 700;
    int interp = 0, observe = 0, census = 0, atlas_light = 0;
    const char *atlas = NULL, *charset = "ascii";
    unsigned long cell_w = 8, cell_h = 16;
    int have_display = 0, grab = 1, sandbox = 1, have_header = 0, have_enter_ms = 0;
    char **cmd = NULL;
    const char *title = NULL;
    int no_clock = 0;

    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        const char *v = (i + 1 < argc) ? argv[i + 1] : NULL;
        if (strcmp(a, "--") == 0) { cmd = &argv[i + 1]; break; }
        else if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) { usage(stdout); return 0; }
        else if (strcmp(a, "--list") == 0) { list_all(); return 0; }
        else if (strcmp(a, "--no-grab") == 0) grab = 0;
        else if (strcmp(a, "--no-clock") == 0) no_clock = 1;
        else if (strcmp(a, "--observe") == 0) observe = 1;
        else if (strcmp(a, "--census") == 0) census = 1;
        else if (strcmp(a, "--atlas-light") == 0) atlas_light = 1;
        else if (strcmp(a, "--no-sandbox") == 0) sandbox = 0;
        else if (!v) { fprintf(stderr, "ballum-host: %s needs a value\n", a); usage(stderr); return 2; }
        else if (strcmp(a, "--profile") == 0) { profile_name = v; ++i; }
        else if (strcmp(a, "--title") == 0) { title = v; ++i; }
        else if (strcmp(a, "--enter") == 0) { enter = v; ++i; }
        else if (strcmp(a, "--leave") == 0) { leave = v; ++i; }
        else if (strcmp(a, "--display") == 0) { if (!parse_uint(v, 0xFFFFFFFFul, &display)) goto bad_value; have_display = 1; ++i; }
        else if (strcmp(a, "--fps") == 0) { if (!parse_uint(v, 240, &fps) || fps == 0) goto bad_value; ++i; }
        else if (strcmp(a, "--max-seconds") == 0) { if (!parse_uint(v, 86400ul * 7, &max_s)) goto bad_value; ++i; }
        else if (strcmp(a, "--header") == 0) { if (!parse_uint(v, 512, &header)) goto bad_value; have_header = 1; ++i; }
        else if (strcmp(a, "--interpret") == 0) {
            if (strcmp(v, "notation") == 0) interp = 1; else if (strcmp(v, "alteration") == 0) interp = 2;
            else { fprintf(stderr, "ballum-host: --interpret takes notation or alteration\n"); return 2; }
            ++i;
        }
        else if (strcmp(a, "--atlas") == 0) { atlas = v; ++i; }
        else if (strcmp(a, "--charset") == 0) { charset = v; ++i; }
        else if (strcmp(a, "--cell") == 0) {
            char *x = NULL;
            cell_w = strtoul(v, &x, 10);
            if (!x || *x != 'x' || (cell_h = strtoul(x + 1, &x, 10)) == 0 || *x || cell_w == 0 || cell_w > 256 || cell_h > 256) {
                fprintf(stderr, "ballum-host: --cell takes WxH, for example 8x16\n"); return 2;
            }
            ++i;
        }
        else if (strcmp(a, "--enter-ms") == 0) { if (!parse_uint(v, 10000, &enter_ms)) goto bad_value; have_enter_ms = 1; ++i; }
        else { fprintf(stderr, "ballum-host: unknown option %s\n", a); usage(stderr); return 2; }
        continue;
    bad_value:
        fprintf(stderr, "ballum-host: invalid value for %s: %s\n", a, v);
        return 2;
    }

    const ballum_profile_t *profile = NULL;
    if (profile_name) {
        profile = ballum_profile_find(profile_name);
        if (!profile) { fprintf(stderr, "ballum-host: unknown profile '%s' (see --list)\n", profile_name); return 2; }
    }
    if ((cmd && cmd[0]) == (profile != NULL)) {
        fprintf(stderr, "ballum-host: give exactly one of --profile NAME or -- command...\n");
        usage(stderr);
        return 2;
    }

    ballum_host_config_t cfg;
    ballum_host_config_default(&cfg, 0);
    if (profile) {
        ballum_transition_by_name(profile->enter, &cfg.enter);
        ballum_transition_by_name(profile->leave, &cfg.leave);
    }
    if (enter && !ballum_transition_by_name(enter, &cfg.enter)) { fprintf(stderr, "ballum-host: unknown transition '%s' (see --list)\n", enter); return 2; }
    if (leave && !ballum_transition_by_name(leave, &cfg.leave)) { fprintf(stderr, "ballum-host: unknown transition '%s' (see --list)\n", leave); return 2; }
    if (have_header) cfg.header_height = (uint32_t)header;
    if (have_enter_ms) cfg.enter_ms = (uint32_t)enter_ms;
    cfg.max_runtime_ms = (uint64_t)max_s * 1000u;
    cfg.grab_input = grab;
    if (census) observe = 1;
    if (census && !interp) interp = 1;                       /* the hook runs inside the interpreter */
    if (interp) { cfg.interpret = 1; cfg.interp_mode = interp == 2 ? BALLUM_INTERP_ALTERATION : BALLUM_INTERP_NOTATION; }

    /* Protect Ballum itself before it starts monitoring input. A failure is
     * reported, not ignored: the user should know the protection is absent. */
    ballum_status_t st = ballum_sandbox_harden_self();
    if (st != BALLUM_STATUS_OK)
        fprintf(stderr, "ballum-host: warning: could not harden this process (%s)\n", ballum_status_name(st));

    ballum_manager_t mgr;
    st = ballum_manager_init(&mgr, NULL, NULL);
    ballum_display_t displays[BALLUM_MAX_DISPLAYS];
    size_t count = 0;
    if (st == BALLUM_STATUS_OK) st = ballum_manager_enumerate(&mgr, displays, BALLUM_MAX_DISPLAYS, &count);
    if (st != BALLUM_STATUS_OK || count == 0) {
        fprintf(stderr, "ballum-host: no usable display (%s). Run from a graphical X11 session.\n", ballum_status_name(st));
        return 3;
    }
    cfg.display_id = displays[0].id;
    for (size_t i = 0; i < count; ++i) {
        if (have_display ? displays[i].id == (uint32_t)display : displays[i].is_primary) { cfg.display_id = displays[i].id; break; }
    }
    if (have_display && cfg.display_id != (uint32_t)display) { fprintf(stderr, "ballum-host: display %lu not found\n", display); return 2; }

    ballum_program_t prog;
    memset(&prog, 0, sizeof(prog));
    prog.argv = profile ? profile->argv : (const char *const *)cmd;
    prog.title = profile ? profile->title : cmd[0];
    prog.sandbox = sandbox;
    prog.log_fd = -1;
    prog.observe = observe;

    ballum_host_source_t source;
    void *sctx = NULL;
    st = ballum_host_source_x11_create(&prog, &source, &sctx);
    if (st != BALLUM_STATUS_OK) { fprintf(stderr, "ballum-host: cannot host programs on this build (%s)\n", ballum_status_name(st)); return 3; }

    ballum_host_t *host = NULL;
    st = ballum_host_create(&mgr, &cfg, &source, sctx, &host);
    if (st != BALLUM_STATUS_OK) {
        fprintf(stderr, "ballum-host: cannot create the host (%s)\n", ballum_status_name(st));
        ballum_host_source_x11_destroy(sctx);
        return 3;
    }
    /* Header: app name on the left, date and time on the right (unless disabled). */
    static ballum_header_state_t hs_name, hs_clock;
    ballum_component_t hc;
    {
        const char *nm = title ? title : (profile_name ? profile_name : (cmd && cmd[0] ? cmd[0] : "Ballum"));
        ballum_color_t ink = { 0x20, 0x20, 0x20, 255 };
        if (ballum_header_app_name(&hs_name, nm, 2, ink, &hc) == BALLUM_STATUS_OK)
            (void)ballum_host_set_slot(host, BALLUM_SLOT_APP_NAME, &hc);
        if (!no_clock && ballum_header_date_time(&hs_clock, 0, 2, ink, NULL, NULL, &hc) == BALLUM_STATUS_OK)
            (void)ballum_host_set_slot(host, BALLUM_SLOT_DATE_TIME, &hc);
    }
    ballum_charsource_t *chars = NULL;
    if (atlas) {
        ballum_charset_t cset;
        FILE *af = ballum_charset_by_name(charset, &cset) ? fopen(atlas, "rb") : NULL;
        chars = ballum_charsource_create();
        uint8_t *buf = NULL; size_t len = 0, cap = 0;
        while (af && chars) {
            if (len == cap) { cap = cap ? cap * 2 : 65536; if (cap > (16u << 20)) break; uint8_t *nb = (uint8_t *)realloc(buf, cap); if (!nb) break; buf = nb; }
            size_t got = fread(buf + len, 1, cap - len, af);
            if (got == 0) break;
            len += got;
        }
        if (af) fclose(af);
        size_t added = 0;
        ballum_status_t ast = (af && chars) ? ballum_charsource_add_pgm(chars, buf, len, (uint32_t)cell_w, (uint32_t)cell_h, cset, 0,
                                                                       atlas_light ? BALLUM_COVER_LIGHT_INK : BALLUM_COVER_DARK_INK, &added)
                                            : BALLUM_STATUS_INVALID_ARGUMENT;
        free(buf);
        if (ast != BALLUM_STATUS_OK) {
            fprintf(stderr, "ballum-host: cannot use atlas '%s' (%s): check the file, --cell and --charset\n", atlas, ballum_status_name(ast));
            ballum_host_destroy(host); ballum_host_source_x11_destroy(sctx); ballum_charsource_destroy(chars);
            return 2;
        }
        fprintf(stderr, "ballum-host: character source: %zu glyphs from %s\n", added, atlas);
        ballum_host_source_x11_set_charsource(sctx, chars);
    }
    if (census) ballum_host_set_interpreter_hook(host, census_hook, NULL);
    st = ballum_host_run(host, (unsigned)fps);
    if (census) {
        ballum_xdraw_stats_t ds; ballum_x11proxy_stats_t rs;
        ballum_host_source_x11_stats(sctx, &ds, &rs);
        fprintf(stderr, "census: relay observing=%d connections=%llu to-server=%llu to-client=%llu unobserved=%llu\n"
                        "census: requests=%llu drawing=%llu elements=%llu dropped=%llu glyphs=%llu known=%llu text-unsized=%llu\n",
                ballum_host_source_x11_observing(sctx), (unsigned long long)rs.connections, (unsigned long long)rs.bytes_to_server,
                (unsigned long long)rs.bytes_to_client, (unsigned long long)rs.unobserved, (unsigned long long)ds.requests,
                (unsigned long long)ds.drawing, (unsigned long long)ds.elements, (unsigned long long)ds.dropped,
                (unsigned long long)ds.glyphs, (unsigned long long)ds.glyphs_known, (unsigned long long)ds.text_unsized);
    }
    ballum_exit_reason_t why = ballum_host_exit_reason(host);
    ballum_host_destroy(host);
    ballum_host_source_x11_destroy(sctx);
    ballum_charsource_destroy(chars);

    if (st != BALLUM_STATUS_OK) {
        fprintf(stderr, "ballum-host: %s (%s)\n",
                why == BALLUM_EXIT_STARTUP_FAILED
                    ? "the program could not be started: is Xvfb, libXtst and the program installed?"
                    : "the session ended with an error",
                ballum_status_name(st));
        return 3;
    }
    fprintf(stderr, "ballum-host: session ended (%s)\n", ballum_exit_reason_name(why));
    return why == BALLUM_EXIT_STARTUP_FAILED || why == BALLUM_EXIT_ERROR ? 3 : 0;
}
