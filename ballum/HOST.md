# Ballum™ Program Host

**Package:** `ballum/`  **Date:** 2026-11-21

The Ballum program host puts a real program, such as a terminal or an admin
interface, inside the white Ballum environment. Ballum receives the program's
pixels and acts as its monitor: every key and mouse event arriving at Ballum is
replayed into the program 1:1, as if the program ran on a native X11 or Windows
GUI stack.

```
make host
build/ballum-host --list
build/ballum-host --profile sleela-terminal --enter disintegrate
build/ballum-host --profile xterm
build/ballum-host --enter slide-up -- /usr/bin/xterm -e top
```

Leave a session with **Ctrl+Alt+Esc**. Closing the program also ends it.

## How the pixels are exact

A terminal draws itself with its real toolkit, fonts and colors; re-implementing
a terminal inside Ballum would only approximate it. So Ballum does not render
the program. It runs the program on its **own private X display** (Xvfb) and
captures that display's pixels:

```
program ──draws──► private X display ──capture──► host ──blit──► Ballum surface
program ◄─XTEST──  private X display ◄──router──  host ◄─events─ Ballum surface
```

The capture is the program's own framebuffer, so the result is pixel-for-pixel
what the program shows on a native desktop. There is one conversion step,
BGRX → RGBA, and one memcmp per frame to skip frames that did not change.

Because the display is private, the program cannot see, capture or inject into
the user's real desktop, and the XTEST input reaches only the private display.

## Layout

```
+--------------------------------------------------------------+
| APP NAME slot                          | DATE TIME slot      |  header
+--------+-----------------------------------------+-----------+
| LEFT   |                                         | RIGHT     |
| sibling|            PROGRAM VIEWPORT             | sibling   |
+--------+-----------------------------------------+-----------+
|                      BOTTOM sibling                          |
+--------------------------------------------------------------+
```

Everything is exact integer pixels (`ballum_host_layout`). The program is
started at exactly the viewport size and its first window is pinned to it.

### APP NAME and DATE TIME components

The two header slots sit above the program. **The components themselves do not
exist yet.** A repository check (2026-11-21) found:

| Need | In the repository today |
|---|---|
| App Name component | none. `lib/user-interface/SLStatusBar` is a footer strip and `lib/ui/SLLabel` is a generic label; neither is a Ballum slot component |
| Date Time component | none. `lib/core/SLClock` provides clock access but no drawing component |
| Text drawing inside the Ballum framebuffer | none. Ballum is a pixel/blit surface with no font engine (`ballum/example` embeds a tiny 5×7 font) |

So the host reserves and clears both slots and offers a stable contract for the
components to be built against:

```c
static void draw_app_name(void *user, ballum_surface_view_t *view) { /* draw into view */ }

ballum_component_t name = { draw_app_name, NULL /* optional input */, user, 0 };
ballum_component_t clock = { draw_clock,   NULL, user, 1000 };   /* redraw every second */
ballum_host_set_slot(host, BALLUM_SLOT_APP_NAME, &name);
ballum_host_set_slot(host, BALLUM_SLOT_DATE_TIME, &clock);
```

A component receives a `ballum_surface_view_t` covering exactly its rectangle;
`ballum_view_put` / `ballum_view_fill` clip to it, so a component cannot draw
outside its slot. `refresh_ms` makes the host redraw the slot at that interval
(the date-time clock); `0` draws on layout change only. Starter profiles carry a
`title` for the app-name component to show.

Sibling components attach the same way with `ballum_host_add_sibling` before
the program starts (the viewport size is then final).

## Input: 1:1, with an escape hatch

| Input | Forwarded as |
|---|---|
| pointer motion | position in viewport coordinates (display minus viewport origin) |
| buttons 1–3, wheel 4–7 | button press/release, each preceded by the position |
| keys | symbol + physical code, press and release |
| press inside the viewport, drag outside | still forwarded, clamped to the viewport, until release |
| events over the header or a sibling | delivered to that component only, in local coordinates |

Input arrives from the Ballum surface with `XGrabKeyboard` / `XGrabPointer`
(revocable, and a refused grab is tolerated unless `require_grab` is set).
Keys are looked up by symbol in the program display's own keymap; the physical
code is the fallback. Key repeat arrives as release+press pairs, as the X server
reports it.

**Remapping.** `ballum_host_remap_key(host, from, required_modifiers, to)` and
`ballum_host_remap_button(host, from, to)` rewrite or drop (`to == 0`) input
before it is routed. The escape chord cannot be remapped away.

**Escape chord.** Ctrl+Alt+Esc is consumed by the host and never reaches the
program, so a bare Esc still belongs to vim or a shell. The chord is checked
every step, independently of the program, so a hung program cannot trap the
user.

## Entry and exit effects

`ballum_transition.h` works on whole frames, so effects can move the program
rather than only mask it. Names for `--enter` / `--leave`:

| Name | Effect |
|---|---|
| `disintegrate` | every pixel flies in from a scattered position and settles; played backwards it is the disintegration |
| `slide-left` `slide-right` `slide-up` `slide-down` | the program frame slides in from that edge |
| `pixelize` | a coarse mosaic of the frame sharpens to full resolution |
| `none` | the program appears at once |
| `fade` `wipe-*` `iris-in` `iris-out` `bars` `blinds` `dissolve` `pixelate` `ripple` `checker` `diamond` `clock` `sparkle` `zoom` `spotlight` | the 20-effect catalog of `ballum_effects.h` (its own `slide-left` and `slide-up` are replaced by the true slides above) |

Contract: at progress 100 the output is the program frame exactly; at 0 it is
the background. Exit plays the same effect backwards (`100 − t`). Integer-only
and deterministic.

## Starter profiles

`build/ballum-host --list` shows them; `ballum_host_profiles.h` is the data.

| Profile | Runs | Enter / leave |
|---|---|---|
| `sleela-terminal` | `SleelaTerminal` (GTK/VTE, slsh) | disintegrate / fade |
| `slsh` | `xterm -e slsh` | slide-up / slide-down |
| `xterm` | light xterm | pixelize / fade |
| `xterm-dark` | dark xterm | fade / fade |
| `top` | `xterm -e top`, a small admin view | iris-in / iris-out |

## Security

Aligned with NIST SP 800-53 Rev. 5 controls **AC-6** (least privilege),
**SC-39** (process isolation), **SI-10** (input validation), **CM-7** (least
functionality) and **SC-7** (no network listener). This describes design intent;
the code has not been assessed or certified against any standard.

| Measure | Where |
|---|---|
| program runs `no_new_privs`, with a seccomp-BPF denylist (ptrace, process_vm_*, mount family, module loading, kexec, reboot, bpf, perf_event_open, userfaultfd, keyrings, clock/hostname setters, io_uring, setns); foreign syscall ABIs kill the process | `ballum_sandbox.c` |
| no core dumps, bounded open files, clean allowlisted environment, all inherited descriptors closed, own session and process group, killed if Ballum dies | `ballum_sandbox.c` |
| Ballum marks itself non-dumpable so other processes cannot read the input it monitors | `ballum_sandbox_harden_self` |
| programs resolved only against a fixed trusted path; group/world-writable files and directories rejected (CWE-426); no shell is ever involved, so no injection through arguments | `ballum_sandbox_resolve` |
| private X display over a 0600 `MIT-MAGIC-COOKIE-1` file in a 0700 directory, no TCP | `ballum_host_x11.c` |
| every protection fails closed: if it cannot be established, the program is not started | all |
| host refuses to start without full keyboard and pointer input, so the user can always leave | `ballum_host_create` |
| bounded input queues, validated rectangles and sizes, frames copied into host-owned memory | `ballum_host.c`, X11 backend |

**Limits, stated plainly.** The seccomp layer is a denylist, not a container:
the program still sees the user's files and can run as the user. It limits what
a misbehaving program can do to the system; it is not a boundary against
hostile code. For more, set `BALLUM_SANDBOX_ISOLATE_FS=1` (or
`isolate_fs` in the policy): the program then runs in its own user and mount
namespace with the root file system read-only and an empty private tmpfs over
`$HOME`. It fails closed where the kernel forbids unprivileged user namespaces.
It does not cover separately mounted file systems such as `/tmp` (the X socket
lives there), and the network and SysV IPC stay shared. `XDG_RUNTIME_DIR` is passed through so toolkits can reach
their settings service. A program that is a launcher forking and exiting is
treated as ended. Windows and macOS have no program source and report
`UNSUPPORTED` (see the display-backend section above); Wayland needs a different input path.

## Windows 10+ and macOS display backends

These give Ballum its fullscreen surface, pixel framebuffer and input on the
two other desktops. They are **display backends only**. Hosting a program
(a program source) is still `UNSUPPORTED` on both: Windows would need a hidden
desktop to run the program off-screen, which is the technique remote-access
malware uses, so it is deliberately not built here; macOS has no equivalent of
a private X server.

**UNVERIFIED.** Neither backend has been built or run on its OS. They were
syntax-checked against hand-written stub headers only. The CI workflow
(`.github/workflows/ballum.yml`) compiles both on real runners, but nothing
exercises them at run time. Treat them as untested until `make PLATFORM=...`
and a manual run on a real machine pass.

| | Windows 10+ (`ballum_backend_windows.c`) | macOS (`ballum_backend_macos.mm`) |
|---|---|---|
| Enumeration | `EnumDisplayMonitors`, real pixel size and refresh rate | `CGDisplayCopyDisplayMode` pixel size (Retina exact) and refresh |
| DPI | Per-monitor v2 awareness, so coordinates are physical pixels | Framebuffer in backing pixels; events scaled from points |
| Surface | Borderless topmost popup, top-down 32-bit DIB, `BitBlt` present | Borderless shielding-level `NSWindow`, BGRA bitmap drawn by an `NSView` |
| Pointer | Move, three buttons, wheel notches (both axes) | Move, three buttons, wheel (both axes) |
| Keys | Press/release, VK mapped to X11 keysyms, repeat as release+press | Press/release, key code mapped to X11 keysyms, same repeat rule |
| Grab | `ClipCursor`; dropped when the window loses focus | Hides Dock and menu bar, disables app switching, hides cursor; revocable |
| Not covered | Secure attention sequence (Ctrl+Alt+Del), Win+L and UAC desktops cannot be grabbed | Cmd+Option+Esc force quit and the power button are not grabbed |

The Windows and macOS keysym tables cover Latin-1 printables, Return, Tab,
Backspace, Escape, arrows, Home/End/Page keys, Delete and F1-F12. Windows also maps Insert, the keypad,
Pause, Print Screen, Menu and F13-F24. Keys outside the table are dropped, not
guessed.

## Header, capture speed, Wayland, audit signing

**Header.** `ballum_header.h` provides the APP NAME and DATE TIME slot
components. Text uses a small built-in 5x7 font for printable ASCII, drawn for
this project with no third-party licence; other characters show as a hollow
box. `ballum-host` fills both slots by default (`--title TEXT`, `--no-clock`).
The font is not used for the interpreter's TEXT and FONT_FAMILY points, which
still need a glyph source from the caller.

**Capture.** The X11 source reads frames over MIT-SHM (loaded from libXext at
run time) and falls back to `XGetSubImage` on any failure;
`BALLUM_X11_NO_SHM=1` forces the fallback. XDamage is not used: frames are still
polled, and unchanged frames are skipped by comparison.

**Wayland.** `make PLATFORM=wayland` builds a native backend
(`ballum_backend_wayland.c`: `wl_shm`, xdg-shell fullscreen, pointer, keyboard
through xkbcommon). **Unverified**: it has only been syntax-checked against stub
headers. Wayland gives a client no desktop drawing, no brightness or gamma, no
global input grab and no key repeat, so those are reported as unsupported or
absent. Program hosting is unaffected because it uses a private Xvfb.

**Audit signing.** `ballum_interp_audit_head()` gives a 32-byte summary of the
trail and `ballum_lms.h` signs it with LMS (RFC 8554, SHA-256, 32 signatures per
key). ML-DSA is not implemented in this repository and `crypto/PQ-INTEGRATION.md`
says to vendor it rather than write it, so LMS is used instead. It has not been
checked against the RFC 8554 test vectors; interoperability is unconfirmed. LMS is
stateful: never reuse or restore an older copy of a key.

**Programs on Windows and macOS** remain unsupported. A hidden desktop is the
technique used by remote-access malware and was not built. A visible-window
design (launch the program normally, capture its window, post input to it) would
lose input whenever Ballum's own fullscreen window holds focus, and could not be
tested here, so it was not written either.

## From SLeeLa

`lib/ballum/SLBallumHost.sleela` wraps the `uiBallumHost*` VM built-ins; the
runnable `lib/ballum/host-demo.sleela` lists the profiles and transitions and
starts one.

```
SLBallumHost host = new SLBallumHost();
host.create();
host.open();                                        bind the primary display
host.startProfile("xterm", "disintegrate", "fade"); "" picks the profile default
while (host.isRunning()) { host.step(); }           one call per frame
print(host.exitReasonName());
```

| Built-in | Meaning |
|---|---|
| `uiBallumHostProfileCount/Name/Title(i)` | the starter profiles; the title is for the APP NAME component |
| `uiBallumHostTransitionCount/Name(i)` | the 25 selectable entry and exit effects, each once |
| `uiBallumHostConfigure(header, enterMs, maxSeconds)` | before start; ranges are validated |
| `uiBallumHostStart(display, profile, enter, leave)` | start a profile |
| `uiBallumHostStartProgram(display, program, args, enter, leave)` | start any program; `args` is split on spaces, no shell, no quoting |
| `uiBallumHostStep()` / `State()` / `StateName(s)` | advance one frame; 0 idle, 1 starting, 2 entering, 3 running, 4 leaving, 5 done |
| `uiBallumHostExitReason()` / `ExitReasonName(r)` | why the session ended |
| `uiBallumHostStop()` | end it at once |

One session at a time. A call that cannot be honoured returns a status code
(`uiBallumStatusName` gives the text), never a false success. The wheel buttons
reach SLeeLa as `SLBallumPointerEvent.BUTTON_WHEEL_UP/DOWN/LEFT/RIGHT` (4 to 7).

Native code cannot call back into the VM, and does not need to: SLeeLa drives the
host one frame at a time with `step()`, so between two steps it can read what
the last frame showed and change the rules for the next one.
`SLBallumInterpreter.ruleIfSeen(text, point, value, rgb)` with `clearRules()`
per frame is the pattern (see the class comment). A decision takes effect one
frame later.

## Interpreter: notation or alteration

Between the program's pixels and the screen sits the **interpreter**
(`ballum_interp.h`). Turn it on with `config.interpret = 1` or
`ballum-host --interpret notation|alteration`.

The mode is a **binary choice made once and never changed** for the session:

| Mode | Guarantee |
|---|---|
| `NOTATION` | observe and annotate only. The frame shown is the program's frame **byte for byte**, whatever anything asks. Every alteration request returns `MODE_NOTATION` and draws nothing. |
| `ALTERATION` | the frame shown is the program's frame **unless an element is altered**. Only pixels inside an altered element's rectangle can differ; with nothing altered the output is still identical to the input. |

Both guarantees are tested, including by mutation (breaking the rule makes the
tests fail).

An **element** is one decoded drawing act in the program's own coordinates: a
text run (with its characters when they were recovered), a fill, a copy or an
image. The interpreter takes elements from the program source's optional
`elements()` callback, so it does not depend on how they were obtained. The X11
source produces them when observation is on (next section); with it off, or on a
source that cannot, the interpreter sees an empty element list and alterations can
only target elements a caller submits.

### Filter band

A clean, technological band-pass sits in the middle of the pipeline. An
alteration carries four scores, 0 to 1000, and passes only when **every** axis is
inside its window (edges inclusive):

| Axis | Question |
|---|---|
| Morals | is the altered content allowed |
| Cleanliness | is the alteration well formed |
| Throughput | is it cheap enough for this frame |
| Confidence | how sure is the interpreter of what the element is |

The band does not judge anything itself: the caller supplies the scores, the
band gates on them. The default opens every axis fully except Confidence, which
needs at least 500. A blocked alteration leaves the element exactly as the
program drew it; a contradictory window (low above high) passes nothing; a score
outside 0..1000 is an error, never a pass.

### The 23 contact points

The interface for altering characters or parts of the GUI is a fixed set of 23
points (`ballum_contact_t`, applied with `ballum_interp_apply`; `text` carries a
string and has `ballum_interp_apply_text`). The set and its numbering are
Ballum's own; **no national or industry standard is claimed**. All 23 are
implemented, and every one works strictly inside the element rectangle.

Each point has a **confidence rating** (`ballum_contact_confidence`, 0..1000).
It is a design rating of how exactly the result is defined, not a measured
accuracy. `ballum_filter_scores_capped()` lowers the Confidence score of a
request to the point's rating, so a band with a Confidence floor refuses the
lower-rated points on its own.

| Point | Rating | Value | What it does |
|---|---|---|---|
| `color-fg` | 1000 | colour | recolours the ink in proportion to coverage, so glyph shape and antialiasing survive; a fill element is filled |
| `color-bg` | 1000 | colour | recolours the paper, keeps the ink |
| `opacity` | 1000 | 0..100 | toward the element's paper colour |
| `brightness` | 1000 | -100..100 | |
| `contrast` | 1000 | -100..100 | |
| `saturation` | 1000 | 0..200 | 100 = unchanged |
| `visibility` | 1000 | 0 hide, 1 restore | restore returns the element to what the program drew |
| `effect` | 1000 | 0 none, 1 invert, 2 grayscale, 3 sepia | |
| `blur` | 1000 | radius 1..8 | separable box blur, edges clamped to the element |
| `underline` | 1000 | thickness 1..4, colour | |
| `strikethrough` | 1000 | thickness 1..4, colour | |
| `crypto-grade` | 1000 | 0, 1, 2 | session setting, see below |
| `position-x` `position-y` | 1000 | -width..width, -height..height px | content moves inside the rectangle; what leaves is clipped, the exposed area takes the paper colour |
| `z-order` | 1000 | 0..255 | higher draws later: where two altered elements overlap, the higher z shows on top. The frame is rebuilt from the program frame in z order |
| `weight` | 900 | -3..3 px | grows or shrinks the ink; untouched pixels stay exactly as drawn |
| `outline` | 900 | thickness 1..3, colour | a ring around the ink; the ink is untouched |
| `size` | 800 | 25..400 % | bilinear resample about the centre |
| `slant` | 800 | -100..100 % shear | baseline fixed |
| `spacing` | 700 | -4..32 px | cuts the element at blank columns and moves the pieces |
| `line-height` | 700 | -8..64 px | the same at blank rows |
| `text` | 600 | UTF-8 string | re-renders the characters; needs a glyph source |
| `font-family` | 600 | 0..15 | re-renders the current text in that family; needs a glyph source |

What the lower ratings mean, plainly:

- `size`, `slant`, `spacing` and `line-height` work on the element's **own
  pixels**. No font is involved, so they are resampling, shearing and cutting,
  not glyph re-rendering. `spacing` and `line-height` assume glyphs or lines are
  separated by blank pixels; touching glyphs move together.
- `text` and `font-family` need a **glyph source**
  (`ballum_interp_set_glyph_source`): a callback that turns a code point, a
  family and a line height into coverage pixels. Ballum ships none, so no font
  licence is bundled. Without one they return `UNSUPPORTED`, and
  `ballum_contact_available()` says so up front.
- Points that need the paper colour return `UNSUPPORTED` for an element without
  an opaque one; points that need ink also need an ink colour that differs from
  the paper. Nothing is guessed.
- A frame keeps at most 4096 alterations and 8 MiB of replacement pixels; past
  that, `FRAME_FULL` is returned and nothing is drawn.

### Crypto grade

`crypto-grade` is about the **record** of what was altered, not secrecy:

| Grade | Record |
|---|---|
| 0 | none |
| 1 | every applied alteration appends an audit entry with the SHA-256 of the resulting element pixels |
| 2 | as 1, and the entries form a keyed chain: each carries HMAC-SHA-256 over the previous MAC and its own fields, so a removed, reordered or edited entry fails `ballum_interp_audit_verify` |

It uses the repository's `crypto/` module (FIPS 180-4, FIPS 198-1), which its own
notes call correct reference code, not hardened or constant-time. The trail is
tamper-**evident** to a holder of the key; it is not a defence against a local
attacker who can read the key. If the log (4096 entries) is full, further
alterations are refused (`AUDIT_FULL`) rather than applied unrecorded. A log has
one grade; asymmetric signatures (the repository has ML-DSA) are not wired in.
A digest is of the element pixels at the moment of that alteration; a later
`z-order` rebuild can change them and records its own entry.

### Input follows the picture

When an alteration moves or scales an element, a click on the altered pixels
has to reach the original coordinates. `ballum_interp_map_point()` maps a point
in the displayed (altered) frame back to the program frame, and the host calls
it on every pointer event when an interpreter is attached. It undoes position-x, position-y,
size and slant. It does not undo spacing, line height, text replacement or
font-family changes, because those have no single-point inverse.


## Seeing what the program draws: the hook into the X11 stack

The interpreter acts on ELEMENTS. To get them from a real program, Ballum puts a
relay on the unix socket between the program and its X server
(`ballum_x11proxy.h`, `--observe` / `program.observe = 1`):

```
program --X11--> relay --same bytes--> private X server
                  |
          ballum_xwire   frames the bytes into requests, replies, events
          ballum_xdraw   what was drawn: windows, text, fills, images
          ballum_charsource   glyph image -> character
```

The relay forwards every byte in both directions, unchanged and in order; it
observes and never alters, so the program's own picture stays the unmodified
reference and acting on what was seen happens afterwards, on pixels, in the
interpreter. It does change one thing on purpose: the program is given no X
credentials, and the relay swaps in the cookie for the real server, so the
program never holds it. The relay's socket is mode 0600 and only the same user is
served; at most 8 connections; each direction is buffered to 4 MiB and then the
sender is held back; a stream the framer cannot follow stops being observed but is
still relayed.

What it follows: windows (create, destroy, reparent, map, configure, and the
server's own ConfigureNotify), pixmaps, GCs, core fonts (OpenFont and the
QueryFont reply, which sizes core text), core drawing (ImageText8/16, PolyText8/16,
PolyFillRectangle, PutImage, CopyArea so scrolling moves the text, ClearArea), and
RENDER (pictures, solid fills, glyph sets, AddGlyphs, CompositeGlyphs8/16/32,
FillRectangles, Composite), which is how GTK, Qt, Xft and cairo programs draw text.
Drawing into a pixmap is remembered and moves to the window when it is copied
there.

**Characters.** A core text request carries the characters. A RENDER text request
carries only glyph numbers plus the glyph images uploaded earlier, so each glyph
image is matched against the character source; a clear match (score 900 or more,
margin 20 or more) is a known character, anything else stays a U+FFFD placeholder
and its element is not marked known. A blank glyph with an advance is a space.

**Honest limits.**

- The element list is the program's drawing *requests*, read as they pass. It is
  exact only when the program is idle: the source reports no elements for 8 ms
  after the last drawing request, and the frame can be a frame behind while the
  program draws.
- Colours: a GC pixel is read as 0xRRGGBB (24/32-bit TrueColor, which Ballum's own
  display is). A RENDER glyph run composites over what was there, so its paper is
  not in the request; paper and, for a non-solid source, ink are read back off the
  captured frame (`ballum_element_infer_colors`).
- RENDER's glyph pen is taken as (0,0) with the first delta absolute, as Xft and
  cairo send it.
- Core text is sized from font metrics only if the program queried the font, as
  Xlib's XLoadQueryFont does; otherwise it is counted and not reported.
- Bounded: 512 elements per drawable, fixed tables, an upload over 1 MiB is not
  read. What does not fit is dropped and counted.
- **Not yet run against a live X server.** The framer, tracker and relay are
  tested with synthetic byte streams and a fake server on a unix socket, including
  a 2 MB request relayed byte for byte, hostile input at a guard page and 40,000
  random requests. How GTK and VTE really behave on the wire (and the assumptions
  above) still has to be confirmed on a machine with Xvfb, gedit and a terminal.

## Character source

`ballum_charsource.h` is Ballum's end of sourcing characters: one interface that
takes in a **pixel map** or a **known charset** and gives glyphs Ballum can draw
(it is the interpreter's glyph source for `text` and `font-family`) and recognise.

- Known charsets: `ascii` (95), `latin1` (191), `digits` (10), `alnum` (62), or any
  explicit list of code points.
- A pixel map is an image cut into equal cells; cell *i* holds the *i*-th character
  of the charset. Ink coverage is read from alpha, or from brightness (dark ink on
  light, or light on dark). `ballum_charsource_add_pgm` reads a binary PGM, the
  plainest lossless format. A bitmap font has one size, so it renders only into a
  line at least a cell tall; it is never scaled silently.
- `ballum_charsource_identify` compares a glyph image against every glyph held,
  after trimming both to their ink, and returns the character, a 0..1000 score and
  the margin to the best *different* character, so an ambiguous match (two
  characters with identical pixels) is reported, not hidden.
- Ballum ships no font. The charset only says which characters an image holds, so
  no font licence is bundled; the caller brings the glyphs.

## From SLeeLa: seeing and acting

Besides `SLBallumHost`, two classes cover this layer:

| Class | Purpose |
|---|---|
| `SLBallumCharSource` | `loadPgm(path, cellW, cellH, charset, family, lightInk)`, `glyphCount()`, `clear()`, the charsets |
| `SLBallumInterpreter` | `observe(on)`, `setMode(0/1/2)`, `band(axis, low, high)`, `scores(...)`, `rule(match, point, value, rgb)`, the 23 contact points and their ratings, what the host saw (`elementCount`, `elementText`, `elementX/Y/Width/Height`, `elementKnown`), and counters |

SLeeLa configures before a session and reads while it runs. Alteration is by
**standing rules**: a rule matches a text exactly (or `"*"` for every text element
that has ink to alter) and names a contact point, a value and a colour; the native
host applies the rules to each interpreted frame with the Confidence score capped
to the point's rating. `text` and `crypto-grade` are not rules (one needs a string
and a glyph source, the other is a session setting). A missing atlas file returns
status 4 (display not found); the charsource is read by the session thread, so it
cannot be changed while a session runs.

### The first live run

Everything marked unconfirmed above is settled by one run on a machine with Xvfb,
libXtst, xterm and a terminal or gedit:

```sh
make test-x11          # captures and types into xterm, then runs it behind the relay
build/ballum-host --census --profile xterm                                # core or Xft, whichever xterm uses
build/ballum-host --census --atlas font.pgm --cell 8x16 --charset ascii -- /usr/bin/gedit
```

`make test-x11` asserts that the relay ran, bytes crossed both ways, the framer
followed the program, it drew and elements appeared, for a core-font xterm
(ImageText) and an Xft xterm (RENDER). What rests on assumptions (how many glyphs
were identified, the sample text) is printed, not asserted. `--census` prints,
while a session runs, the elements the host saw (kind, rectangle, ink and paper,
whether the characters are known, the text) and, at the end, the relay and tracker
counters. `?` after a colour means it was not in the requests. If the rectangles
land on the text in the picture, the glyph-origin and pen assumptions hold; if
they are shifted, that is the number to correct.

## Requirements and verification

Run time (Linux): an `Xvfb` binary and `libXtst.so.6` on the machine, plus the
program. Build time: libX11 (and Xrandr for the surface backend).

| Check | Command | Needs a display |
|---|---|---|
| layout, lifecycle, transitions, routing, remap, components, fail-closed paths | `make test` | no |
| process protection, including seccomp denial surviving `exec` | `make test` | no |
| interpreter: mode invariants, band, all 23 points, audit chain | `make test` | no |
| character source; X11 framer; drawing tracker; relay against a fake server | `make test` | no |
| capture and 1:1 input against a real X client | `make test-x11` | Xvfb, libXtst, xterm |
| an actual session on screen | `make host && build/ballum-host --profile xterm` | an X11 session |
| the relay against a real program | `make test-x11`, `build/ballum-host --census ...` | Xvfb, libXtst, xterm |

`make test` is portable and passes without any display libraries.
`make test-x11` and the on-screen session were **not run** when this was
written: the development sandbox had no X server and no package access. The
source compiles and its failure paths (missing Xvfb) were exercised; the capture
and input forwarding have not yet run against a live server. Run
`make test-x11` on a machine with Xvfb and xterm before relying on them.

## Files

| File | Role |
|---|---|
| `include/sleela_ballum/ballum_host.h`, `src/ballum_host.c` | compositor, layout, state machine, input router, remap, components |
| `include/sleela_ballum/ballum_transition.h`, `src/ballum_transition.c` | entry/exit transitions |
| `include/sleela_ballum/ballum_sandbox.h`, `src/ballum_sandbox.c` | process protection |
| `include/sleela_ballum/ballum_interp.h`, `src/ballum_interp.c` | interpreter: mode, filter band, 23 contact points, audit trail (links `crypto/`) |
| `include/sleela_ballum/ballum_charsource.h`, `src/ballum_charsource.c` | character source: charsets, pixel maps, PGM, recognising glyphs |
| `include/sleela_ballum/ballum_xwire.h`, `src/ballum_xwire.c` | X11 wire framer |
| `include/sleela_ballum/ballum_xdraw.h`, `src/ballum_xdraw.c` | what the program drew |
| `include/sleela_ballum/ballum_x11proxy.h`, `src/ballum_x11proxy.c` | the relay |
| `src/ballum_host_x11.c` | private-display source (Xvfb + capture + XTEST + relay) |
| `include/sleela_ballum/ballum_host_profiles.h`, `src/ballum_host_profiles.c` | starter profiles |
| `src/ballum_host_cli.c` | `ballum-host` |
| `impl/core/ballum_vm_bridge.c`, `lib/ballum/SLBallumHost.sleela`, `lib/ballum/host-demo.sleela` | the SLeeLa surface |
| `include/sleela_ballum/ballum_header.h`, `src/ballum_header.c` | APP NAME and DATE TIME components, built-in font |
| `include/sleela_ballum/ballum_lms.h`, `src/ballum_lms.c` | LMS signatures for the audit head |
| `src/ballum_backend_wayland.c` | Wayland display backend (unverified) |
| `src/ballum_backend_x11.c` (extended) | event pump, full key events, wheel, input grab, focus |

Backend additions in `ballum_manager.h`: `ballum_key_event_t`,
`poll_key_event`, `set_input_grab`, and wheel buttons 4–7. All are additive;
existing backends leave the new slots NULL and the manager reports
`UNSUPPORTED`.
