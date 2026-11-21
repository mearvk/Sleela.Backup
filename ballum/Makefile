# SLeeLa Ballum(TM) -- pixel-level display control. Standalone build of the
# portable manager plus the per-OS backend and the effects/image helpers, into
# one static library, plus the tests, plus install/uninstall and per-OS
# dependency provisioning. Exactly one OS backend is compiled in; the OS is
# auto-detected but can be overridden with PLATFORM=linux|macos|windows.
#
# The OS-centric libraries Ballum links against are provided by the build:
#   - Linux   : X11 + Xrandr, plus Xext when BALLUM_X11_SHM=1 (reported by `deps`)
#   - macOS   : CoreGraphics/ApplicationServices/Foundation frameworks (ship
#               with the Xcode command-line tools; `deps` verifies them)
#   - Windows : gdi32/user32 import libraries (ship with the Windows SDK /
#               MinGW; `deps` verifies them)
# `make all` runs `check-deps` first, so a missing OS library is reported as a
# clear, actionable message instead of a raw compiler/linker error.

CC  ?= cc
CXX ?= c++
AR  ?= ar
PKG_CONFIG ?= pkg-config
CFLAGS   ?= -std=c11   -O2 -Wall -Wextra -Wpedantic -Iinclude
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -Iinclude
OBJCXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Iinclude

BUILD  := build
ifeq ($(OS),Windows_NT)
  EXE := .exe
else
  EXE :=
endif
PREFIX ?= /usr/local
DESTDIR ?=
LIBDIR     ?= $(PREFIX)/lib
INCLUDEDIR ?= $(PREFIX)/include
INSTALL ?= install

# `deps` install behaviour. By default `deps` only REPORTS and, where a package
# manager is available, uses its download-only / non-privileged path. Set
# BALLUM_DEPS_INSTALL=1 to actually install OS packages (which may use sudo on
# Linux). This keeps the default build side-effect-free and never silently
# elevates privileges.
BALLUM_DEPS_INSTALL ?= 0

# ---- OS detection (override with PLATFORM=...) ---------------------------
UNAME_S := $(shell uname -s 2>/dev/null)
ifeq ($(OS),Windows_NT)
  PLATFORM ?= windows
else ifeq ($(UNAME_S),Darwin)
  PLATFORM ?= macos
else
  PLATFORM ?= linux
endif

# ---- per-OS backend source, define, and system libs ----------------------
ifeq ($(PLATFORM),windows)
  BACKEND_SRC    := src/ballum_backend_windows.c
  BACKEND_DEFINE := -DBALLUM_BACKEND_WINDOWS
  BACKEND_LIBS   := -lgdi32 -luser32
  BACKEND_IS_CPP := 0
else ifeq ($(PLATFORM),wayland)
  # Native Wayland display backend (wl_shm + xdg-shell). UNVERIFIED on a real
  # compositor. Program hosting still uses the private Xvfb source, so Xlib is linked
  # for that and the host source is built with the X11 define.
  BACKEND_SRC    := src/ballum_backend_wayland.c
  BACKEND_DEFINE := -DBALLUM_BACKEND_WAYLAND -I$(BUILD)
  BACKEND_LIBS   := $(shell pkg-config --libs wayland-client xkbcommon 2>/dev/null) -lX11 -lm -ldl -lpthread
  BACKEND_IS_CPP := 0
  HOST_X11_DEFINE := -DBALLUM_BACKEND_X11
  WL_XML := $(shell pkg-config --variable=pkgdatadir wayland-protocols 2>/dev/null)/stable/xdg-shell/xdg-shell.xml
else ifeq ($(PLATFORM),macos)
  BACKEND_SRC    := src/ballum_backend_macos.mm
  BACKEND_DEFINE := -DBALLUM_BACKEND_MACOS
  BACKEND_LIBS   := -framework CoreGraphics -framework ApplicationServices -framework Foundation -framework Cocoa
  BACKEND_IS_CPP := 1
else
  BACKEND_SRC    := src/ballum_backend_x11.c
  # MIT-SHM (fast fullscreen present) is on by default; set BALLUM_X11_SHM=0 to
  # drop it (then the plain-XImage present path is used and -lXext is not needed).
  BALLUM_X11_SHM ?= 1
  ifeq ($(BALLUM_X11_SHM),1)
    BACKEND_DEFINE := -DBALLUM_BACKEND_X11 -DBALLUM_X11_USE_SHM
    BACKEND_LIBS   := -lX11 -lXrandr -lXext -lm -ldl -lpthread
  else
    BACKEND_DEFINE := -DBALLUM_BACKEND_X11
    BACKEND_LIBS   := -lX11 -lXrandr -lm -ldl -lpthread
  endif
  BACKEND_IS_CPP := 0
endif

NANOJPEG_DIR := ../user-interface/src
# The interpreter's audit trail uses the repository SHA-256 / HMAC-SHA-256.
CRYPTO_DIR := ../crypto

PORTABLE_C := src/ballum_manager.c src/ballum_backend_select.c src/ballum_backend_unavailable.c

# ---- default: verify OS deps, then build the real OS backend library ------
all: check-deps $(BUILD)/libsleela_ballum.a $(BUILD)/sleela_ballum.pc
	@echo "ballum: built $(BUILD)/libsleela_ballum.a + sleela_ballum.pc for PLATFORM=$(PLATFORM) (links: $(BACKEND_LIBS))"

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/ballum_manager.o: src/ballum_manager.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ballum_backend_select.o: src/ballum_backend_select.c | $(BUILD)
	$(CC) $(CFLAGS) $(BACKEND_DEFINE) -c $< -o $@

$(BUILD)/ballum_backend_unavailable.o: src/ballum_backend_unavailable.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ballum_effects.o: src/ballum_effects.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ballum_image.o: src/ballum_image.c | $(BUILD)
	$(CC) $(CFLAGS) -I$(NANOJPEG_DIR) -c $< -o $@

# Program host: transitions, process protection, compositor, starter profiles,
# and the private-display source (the source needs the backend define so it is
# real on X11 and a fail-closed stub everywhere else).
$(BUILD)/ballum_transition.o: src/ballum_transition.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ballum_sandbox.o: src/ballum_sandbox.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ballum_host.o: src/ballum_host.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

# The hook into the X11 stack: wire framer, drawing tracker, relay, and the
# character source they recognise glyphs with.
$(BUILD)/ballum_charsource.o: src/ballum_charsource.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ballum_xwire.o: src/ballum_xwire.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ballum_xdraw.o: src/ballum_xdraw.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ballum_x11proxy.o: src/ballum_x11proxy.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ballum_interp.o: src/ballum_interp.c | $(BUILD)
	$(CC) $(CFLAGS) -I$(CRYPTO_DIR)/include -c $< -o $@

$(BUILD)/slc_sha2.o: $(CRYPTO_DIR)/src/sha2.c | $(BUILD)
	$(CC) $(CFLAGS) -I$(CRYPTO_DIR)/include -c $< -o $@

$(BUILD)/slc_mac_kdf.o: $(CRYPTO_DIR)/src/mac_kdf.c | $(BUILD)
	$(CC) $(CFLAGS) -I$(CRYPTO_DIR)/include -c $< -o $@

$(BUILD)/ballum_host_profiles.o: src/ballum_host_profiles.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ballum_header.o: src/ballum_header.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ballum_lms.o: src/ballum_lms.c | $(BUILD)
	$(CC) $(CFLAGS) -I$(CRYPTO_DIR)/include -c $< -o $@

HOST_X11_DEFINE ?= $(BACKEND_DEFINE)
$(BUILD)/ballum_host_x11.o: src/ballum_host_x11.c | $(BUILD)
	$(CC) $(CFLAGS) $(HOST_X11_DEFINE) -c $< -o $@

ifeq ($(PLATFORM),wayland)
# xdg-shell glue generated from the stable protocol XML by wayland-scanner.
$(BUILD)/xdg-shell-client-protocol.h: | $(BUILD)
	wayland-scanner client-header $(WL_XML) $@
$(BUILD)/xdg-shell-protocol.c: | $(BUILD)
	wayland-scanner private-code $(WL_XML) $@
$(BUILD)/xdg-shell-protocol.o: $(BUILD)/xdg-shell-protocol.c $(BUILD)/xdg-shell-client-protocol.h
	$(CC) $(CFLAGS) -I$(BUILD) -c $< -o $@
$(BUILD)/ballum_backend_os.o: $(BUILD)/xdg-shell-client-protocol.h
LIB_EXTRA := $(BUILD)/xdg-shell-protocol.o
endif

ifeq ($(BACKEND_IS_CPP),1)
$(BUILD)/ballum_backend_os.o: $(BACKEND_SRC) | $(BUILD)
	$(CXX) $(OBJCXXFLAGS) $(BACKEND_DEFINE) -c $< -o $@
else
$(BUILD)/ballum_backend_os.o: $(BACKEND_SRC) | $(BUILD)
	$(CC) $(CFLAGS) $(BACKEND_DEFINE) -c $< -o $@
endif

LIB_OBJS := $(BUILD)/ballum_manager.o $(BUILD)/ballum_backend_select.o \
            $(BUILD)/ballum_backend_unavailable.o $(BUILD)/ballum_backend_os.o \
            $(BUILD)/ballum_effects.o $(BUILD)/ballum_image.o \
            $(BUILD)/ballum_transition.o $(BUILD)/ballum_sandbox.o $(BUILD)/ballum_host.o \
            $(BUILD)/ballum_host_profiles.o $(BUILD)/ballum_host_x11.o \
            $(BUILD)/ballum_interp.o $(BUILD)/slc_sha2.o $(BUILD)/slc_mac_kdf.o \
            $(BUILD)/ballum_charsource.o $(BUILD)/ballum_xwire.o $(BUILD)/ballum_xdraw.o \
            $(BUILD)/ballum_x11proxy.o $(BUILD)/ballum_header.o $(BUILD)/ballum_lms.o $(LIB_EXTRA)

$(BUILD)/libsleela_ballum.a: $(LIB_OBJS)
	$(AR) rcs $@ $(LIB_OBJS)

# ---- pkg-config manifest -------------------------------------------------
# Records how a downstream build links against Ballum, including the OS-centric
# display libraries this PLATFORM needs. Because libsleela_ballum is a STATIC
# archive, those system libs go in Libs.private (emitted by `pkg-config
# --static`), so a consumer does:
#   cc app.c $(pkg-config --cflags --libs --static sleela_ballum) -o app
# The private libs are generated from BACKEND_LIBS, so the .pc never drifts from
# what the build actually links. It is regenerated every build (FORCE) so a
# change of PLATFORM in an existing build/ can never leave a stale manifest.
.PHONY: FORCE
$(BUILD)/sleela_ballum.pc: FORCE | $(BUILD)
	@printf '%s\n' \
	  'prefix=$(PREFIX)' \
	  'exec_prefix=$${prefix}' \
	  'libdir=$(LIBDIR)' \
	  'includedir=$(INCLUDEDIR)' \
	  '' \
	  'Name: libsleela_ballum' \
	  'Description: SLeeLa Ballum(TM) pixel-level display control (PLATFORM=$(PLATFORM))' \
	  'Version: 1.0.0' \
	  'Cflags: -I$${includedir}' \
	  'Libs: -L$${libdir} -lsleela_ballum' \
	  'Libs.private: $(BACKEND_LIBS)' \
	  > $@

pc: $(BUILD)/sleela_ballum.pc
	@echo "ballum: wrote $(BUILD)/sleela_ballum.pc (Libs.private: $(BACKEND_LIBS))"

# ---- demo: a REAL linked executable --------------------------------------
# Unlike the static archive (which `ar` only bundles), this target performs an
# actual link of libsleela_ballum.a against the per-OS display libraries, so a
# missing/!working OS link surfaces here at build time. Needs the OS deps, so it
# depends on check-deps like `all`.
ifeq ($(BACKEND_IS_CPP),1)
LINKER := $(CXX)
else
LINKER := $(CC)
endif
demo: check-deps $(BUILD)/libsleela_ballum.a
	$(LINKER) $(CFLAGS) src/ballum_demo.c $(BUILD)/libsleela_ballum.a $(BACKEND_LIBS) -o $(BUILD)/ballum-demo$(EXE)
	@echo "ballum: linked $(BUILD)/ballum-demo$(EXE) against libsleela_ballum.a $(BACKEND_LIBS)"

# ---- ballum-host: put a program inside the Ballum environment ---------------
# Real executable. At run time it needs Xvfb and libXtst.so.6 plus the program
# to host (see HOST.md); without them it fails closed and says what is missing.
host: check-deps $(BUILD)/libsleela_ballum.a
	$(LINKER) $(CFLAGS) src/ballum_host_cli.c $(BUILD)/libsleela_ballum.a $(BACKEND_LIBS) -o $(BUILD)/ballum-host$(EXE)
	@echo "ballum: linked $(BUILD)/ballum-host$(EXE)  (run: $(BUILD)/ballum-host --list)"

# Real-environment test of the private-display source: launches Xvfb + xterm
# (or $$BALLUM_TEST_X11_PROGRAM), captures it and drives it with keys. Reports
# SKIPPED, leaving nothing behind, when Xvfb / libXtst / the program are absent.
test-x11: check-deps $(BUILD)/libsleela_ballum.a
	$(CC) $(CFLAGS) tests/test_ballum_host_x11.c $(BUILD)/libsleela_ballum.a $(BACKEND_LIBS) -o $(BUILD)/ballum-host-x11-test$(EXE)
	./$(BUILD)/ballum-host-x11-test$(EXE)

# ---- per-OS dependency provisioning --------------------------------------
# `check-deps` verifies the OS-centric libraries this PLATFORM needs. On Linux
# it checks X11 + Xrandr, plus Xext when BALLUM_X11_SHM=1, via pkg-config;
# `deps` installs them with the detected package manager. macOS/Windows system
# frameworks/import libs ship with the platform SDK, so there is nothing to
# download -- `check-deps` only confirms the toolchain can see them.
.PHONY: check-deps deps
check-deps:
	@echo "ballum deps: PLATFORM=$(PLATFORM)"
	@command -v "$(CC)"  >/dev/null 2>&1 && echo "  CC  $$($(CC) --version 2>/dev/null | head -n1)"  || { echo "  MISSING: $(CC)"; exit 1; }
ifeq ($(PLATFORM),wayland)
	@missing=0; \
	for p in wayland-client wayland-protocols xkbcommon x11; do \
	  if command -v "$(PKG_CONFIG)" >/dev/null 2>&1 && $(PKG_CONFIG) --exists $$p; then \
	    echo "  AVAILABLE: $$p"; else echo "  MISSING: $$p development files"; missing=1; fi; \
	done; \
	command -v wayland-scanner >/dev/null 2>&1 || { echo "  MISSING: wayland-scanner"; missing=1; }; \
	if [ $$missing -ne 0 ]; then echo "  -> apt install libwayland-dev wayland-protocols libxkbcommon-dev libx11-dev"; exit 1; fi
else ifeq ($(PLATFORM),linux)
	@missing=0; \
	pkgs="x11 xrandr"; \
	if [ "$(BALLUM_X11_SHM)" = "1" ]; then pkgs="$pkgs xext"; fi; \
	for p in $pkgs; do \
	  if command -v "$(PKG_CONFIG)" >/dev/null 2>&1 && $(PKG_CONFIG) --exists $p; then \
	    echo "  AVAILABLE: $p ($($(PKG_CONFIG) --modversion $p))"; \
	  else echo "  MISSING: $p development files"; missing=1; fi; \
	done; \
	if [ $missing -ne 0 ]; then \
	  echo "  -> run 'make deps' (reports how to install) or 'make deps BALLUM_DEPS_INSTALL=1' to install."; \
	  echo "     Ballum still builds its portable 'test' target without these."; \
	  exit 1; \
	fi
else ifeq ($(PLATFORM),macos)
	@echo "  macOS frameworks (CoreGraphics/ApplicationServices/Foundation) ship with the"
	@echo "  Xcode command-line tools. Verifying the toolchain can link them..."
	@mkdir -p "$(BUILD)"
	@printf 'int main(void){return 0;}\n' > "$(BUILD)/.depcheck.m"
	@$(CC) "$(BUILD)/.depcheck.m" $(BACKEND_LIBS) -o "$(BUILD)/.depcheck" 2>/dev/null \
	  && echo "  AVAILABLE: CoreGraphics/ApplicationServices/Foundation" \
	  || { echo "  MISSING: Apple frameworks -- install the Xcode command-line tools: xcode-select --install"; exit 1; }
	@rm -f $(BUILD)/.depcheck $(BUILD)/.depcheck.m
else
	@echo "  Checking Windows system import libraries (gdi32/user32)..."
	@mkdir -p "$(BUILD)"
	@printf 'int main(void){return 0;}\n' > "$(BUILD)/.depcheck.c"
	@$(CC) "$(BUILD)/.depcheck.c" -o "$(BUILD)/.depcheck$(EXE)" $(BACKEND_LIBS) >/dev/null 2>&1 \
	  && echo "  AVAILABLE: gdi32/user32" \
	  || { rm -f "$(BUILD)/.depcheck.c" "$(BUILD)/.depcheck$(EXE)"; echo "  MISSING: MinGW Windows SDK import libraries (gdi32/user32); use the MSYS2 UCRT64 toolchain." >&2; exit 1; }
	@rm -f "$(BUILD)/.depcheck.c" "$(BUILD)/.depcheck$(EXE)"
endif
	@echo "ballum deps: OK for PLATFORM=$(PLATFORM)"

deps:
ifeq ($(PLATFORM),linux)
	@echo "ballum deps (linux): X11 + Xrandr development files are required; Xext is required when BALLUM_X11_SHM=1."
	@if command -v apt-get >/dev/null 2>&1; then \
	   pkgs="libx11-dev libxrandr-dev libxext-dev"; mgr="apt-get install -y $$pkgs"; \
	 elif command -v dnf >/dev/null 2>&1; then \
	   pkgs="libX11-devel libXrandr-devel libXext-devel"; mgr="dnf install -y $$pkgs"; \
	 elif command -v pacman >/dev/null 2>&1; then \
	   pkgs="libx11 libxrandr libxext"; mgr="pacman -S --noconfirm $$pkgs"; \
	 elif command -v zypper >/dev/null 2>&1; then \
	   pkgs="libX11-devel libXrandr-devel libXext-devel"; mgr="zypper install -y $$pkgs"; \
	 else \
	   echo "  No supported package manager found (apt/dnf/pacman/zypper)."; \
	   echo "  Install the X11 and Xrandr development packages (and Xext when BALLUM_X11_SHM=1) for your distro, then re-run 'make all'."; \
	   exit 1; \
	 fi; \
	 if [ "$(BALLUM_DEPS_INSTALL)" = "1" ]; then \
	   echo "  Installing: $$pkgs"; \
	   if [ "$$(id -u)" = "0" ]; then sh -c "$$mgr"; else sudo $$mgr; fi; \
	 else \
	   echo "  To install, run:  sudo $$mgr"; \
	   echo "  or non-interactively:  make deps BALLUM_DEPS_INSTALL=1"; \
	 fi
else ifeq ($(PLATFORM),macos)
	@echo "ballum deps (macos): the required frameworks ship with the Xcode command-line tools."
	@xcode-select -p >/dev/null 2>&1 && echo "  Xcode command-line tools: present" \
	  || { echo "  Installing the Xcode command-line tools..."; [ "$(BALLUM_DEPS_INSTALL)" = "1" ] && xcode-select --install || echo "  Run: xcode-select --install"; }
else
	@echo "ballum deps (windows): gdi32/user32 ship with the Windows SDK / MinGW; nothing to download."
endif

# ---- test: portable, links the unavailable backend (no display libs) -----
# Built without any BACKEND_DEFINE so ballum_backend_select resolves to the
# unavailable fallback; the test supplies its own mock backend. Needs no OS
# display libraries, so it runs anywhere without `deps`.
test: | $(BUILD)
	$(CC) $(CFLAGS) \
	  src/ballum_manager.c src/ballum_backend_select.c src/ballum_backend_unavailable.c \
	  tests/test_ballum_manager.c -o $(BUILD)/ballum-manager-test$(EXE)
	./$(BUILD)/ballum-manager-test$(EXE)
	$(CC) $(CFLAGS) -I$(NANOJPEG_DIR) \
	  src/ballum_effects.c src/ballum_image.c $(NANOJPEG_DIR)/nanojpeg.c \
	  tests/test_ballum_effects.c -o $(BUILD)/ballum-effects-test$(EXE) -lm
	./$(BUILD)/ballum-effects-test$(EXE)
	$(CC) $(CFLAGS) \
	  src/ballum_transition.c src/ballum_effects.c src/ballum_manager.c \
	  src/ballum_backend_select.c src/ballum_backend_unavailable.c \
	  tests/test_ballum_transition.c -o $(BUILD)/ballum-transition-test$(EXE) -lm
	./$(BUILD)/ballum-transition-test$(EXE)
	$(CC) $(CFLAGS) \
	  -I$(CRYPTO_DIR)/include \
	  src/ballum_host.c src/ballum_interp.c src/ballum_transition.c src/ballum_effects.c src/ballum_manager.c \
	  src/ballum_backend_select.c src/ballum_backend_unavailable.c \
	  $(CRYPTO_DIR)/src/sha2.c $(CRYPTO_DIR)/src/mac_kdf.c \
	  tests/test_ballum_host.c -o $(BUILD)/ballum-host-test$(EXE) -lm
	./$(BUILD)/ballum-host-test$(EXE)
	$(CC) $(CFLAGS) \
	  -I$(CRYPTO_DIR)/include \
	  src/ballum_interp.c src/ballum_manager.c \
	  src/ballum_backend_select.c src/ballum_backend_unavailable.c \
	  $(CRYPTO_DIR)/src/sha2.c $(CRYPTO_DIR)/src/mac_kdf.c \
	  tests/test_ballum_interp.c -o $(BUILD)/ballum-interp-test$(EXE)
	./$(BUILD)/ballum-interp-test$(EXE)
	$(CC) $(CFLAGS) -I$(CRYPTO_DIR)/include \
	  src/ballum_header.c src/ballum_host.c src/ballum_interp.c src/ballum_transition.c src/ballum_effects.c \
	  src/ballum_manager.c src/ballum_backend_select.c src/ballum_backend_unavailable.c \
	  $(CRYPTO_DIR)/src/sha2.c $(CRYPTO_DIR)/src/mac_kdf.c \
	  tests/test_ballum_header.c -o $(BUILD)/ballum-header-test$(EXE) -lm
	./$(BUILD)/ballum-header-test$(EXE)
	$(CC) $(CFLAGS) -I$(CRYPTO_DIR)/include \
	  src/ballum_lms.c $(CRYPTO_DIR)/src/sha2.c tests/test_ballum_lms.c -o $(BUILD)/ballum-lms-test$(EXE)
	./$(BUILD)/ballum-lms-test$(EXE)
	$(CC) $(CFLAGS) -I$(CRYPTO_DIR)/include \
	  src/ballum_charsource.c src/ballum_interp.c src/ballum_manager.c \
	  src/ballum_backend_select.c src/ballum_backend_unavailable.c \
	  $(CRYPTO_DIR)/src/sha2.c $(CRYPTO_DIR)/src/mac_kdf.c \
	  tests/test_ballum_charsource.c -o $(BUILD)/ballum-charsource-test$(EXE)
	./$(BUILD)/ballum-charsource-test$(EXE)
	$(CC) $(CFLAGS) src/ballum_xwire.c tests/test_ballum_xwire.c -o $(BUILD)/ballum-xwire-test$(EXE)
	./$(BUILD)/ballum-xwire-test$(EXE)
ifneq ($(PLATFORM),windows)
	$(CC) $(CFLAGS) \
	  src/ballum_sandbox.c src/ballum_manager.c \
	  src/ballum_backend_select.c src/ballum_backend_unavailable.c \
	  tests/test_ballum_sandbox.c -o $(BUILD)/ballum-sandbox-test$(EXE)
	cd $(BUILD) && ./ballum-sandbox-test$(EXE)
	$(CC) $(CFLAGS) -I$(CRYPTO_DIR)/include \
	  src/ballum_xdraw.c src/ballum_charsource.c src/ballum_interp.c src/ballum_manager.c \
	  src/ballum_backend_select.c src/ballum_backend_unavailable.c \
	  $(CRYPTO_DIR)/src/sha2.c $(CRYPTO_DIR)/src/mac_kdf.c \
	  tests/test_ballum_xdraw.c -o $(BUILD)/ballum-xdraw-test$(EXE) -lpthread
	./$(BUILD)/ballum-xdraw-test$(EXE)
	$(CC) $(CFLAGS) -I$(CRYPTO_DIR)/include \
	  src/ballum_x11proxy.c src/ballum_xdraw.c src/ballum_xwire.c src/ballum_charsource.c src/ballum_interp.c \
	  src/ballum_manager.c src/ballum_backend_select.c src/ballum_backend_unavailable.c \
	  $(CRYPTO_DIR)/src/sha2.c $(CRYPTO_DIR)/src/mac_kdf.c \
	  tests/test_ballum_x11proxy.c -o $(BUILD)/ballum-proxy-test$(EXE) -lpthread
	./$(BUILD)/ballum-proxy-test$(EXE)
endif
	@echo "ballum: all tests passed"

# ---- install / uninstall --------------------------------------------------
# Install the static library and the public headers under $(DESTDIR)$(PREFIX).
install: all
	$(INSTALL) -d "$(DESTDIR)$(LIBDIR)"
	$(INSTALL) -m 0644 $(BUILD)/libsleela_ballum.a "$(DESTDIR)$(LIBDIR)/libsleela_ballum.a"
	$(INSTALL) -d "$(DESTDIR)$(INCLUDEDIR)/sleela_ballum"
	$(INSTALL) -m 0644 include/sleela_ballum/ballum_manager.h "$(DESTDIR)$(INCLUDEDIR)/sleela_ballum/ballum_manager.h"
	$(INSTALL) -m 0644 include/sleela_ballum/ballum_effects.h "$(DESTDIR)$(INCLUDEDIR)/sleela_ballum/ballum_effects.h"
	$(INSTALL) -m 0644 include/sleela_ballum/ballum_image.h   "$(DESTDIR)$(INCLUDEDIR)/sleela_ballum/ballum_image.h"
	$(INSTALL) -m 0644 include/sleela_ballum/ballum_transition.h "$(DESTDIR)$(INCLUDEDIR)/sleela_ballum/ballum_transition.h"
	$(INSTALL) -m 0644 include/sleela_ballum/ballum_sandbox.h "$(DESTDIR)$(INCLUDEDIR)/sleela_ballum/ballum_sandbox.h"
	$(INSTALL) -m 0644 include/sleela_ballum/ballum_host.h    "$(DESTDIR)$(INCLUDEDIR)/sleela_ballum/ballum_host.h"
	$(INSTALL) -m 0644 include/sleela_ballum/ballum_interp.h  "$(DESTDIR)$(INCLUDEDIR)/sleela_ballum/ballum_interp.h"
	$(INSTALL) -m 0644 include/sleela_ballum/ballum_charsource.h "$(DESTDIR)$(INCLUDEDIR)/sleela_ballum/ballum_charsource.h"
	$(INSTALL) -m 0644 include/sleela_ballum/ballum_xwire.h   "$(DESTDIR)$(INCLUDEDIR)/sleela_ballum/ballum_xwire.h"
	$(INSTALL) -m 0644 include/sleela_ballum/ballum_xdraw.h   "$(DESTDIR)$(INCLUDEDIR)/sleela_ballum/ballum_xdraw.h"
	$(INSTALL) -m 0644 include/sleela_ballum/ballum_x11proxy.h "$(DESTDIR)$(INCLUDEDIR)/sleela_ballum/ballum_x11proxy.h"
	$(INSTALL) -m 0644 include/sleela_ballum/ballum_host_profiles.h "$(DESTDIR)$(INCLUDEDIR)/sleela_ballum/ballum_host_profiles.h"
	$(INSTALL) -m 0644 include/sleela_ballum/ballum_header.h "$(DESTDIR)$(INCLUDEDIR)/sleela_ballum/ballum_header.h"
	$(INSTALL) -m 0644 include/sleela_ballum/ballum_lms.h "$(DESTDIR)$(INCLUDEDIR)/sleela_ballum/ballum_lms.h"
	$(INSTALL) -d "$(DESTDIR)$(LIBDIR)/pkgconfig"
	$(INSTALL) -m 0644 $(BUILD)/sleela_ballum.pc "$(DESTDIR)$(LIBDIR)/pkgconfig/sleela_ballum.pc"
	@echo "ballum: installed libsleela_ballum.a, headers and sleela_ballum.pc to $(DESTDIR)$(PREFIX)"
	@echo "ballum: downstream builds: cc app.c \$$(pkg-config --cflags --libs --static sleela_ballum) -o app"

uninstall:
	rm -f "$(DESTDIR)$(LIBDIR)/libsleela_ballum.a"
	rm -f "$(DESTDIR)$(INCLUDEDIR)/sleela_ballum/ballum_manager.h" \
	      "$(DESTDIR)$(INCLUDEDIR)/sleela_ballum/ballum_effects.h" \
	      "$(DESTDIR)$(INCLUDEDIR)/sleela_ballum/ballum_image.h" \
	      "$(DESTDIR)$(INCLUDEDIR)/sleela_ballum/ballum_transition.h" \
	      "$(DESTDIR)$(INCLUDEDIR)/sleela_ballum/ballum_sandbox.h" \
	      "$(DESTDIR)$(INCLUDEDIR)/sleela_ballum/ballum_host.h" \
	      "$(DESTDIR)$(INCLUDEDIR)/sleela_ballum/ballum_interp.h" \
	      "$(DESTDIR)$(INCLUDEDIR)/sleela_ballum/ballum_charsource.h" \
	      "$(DESTDIR)$(INCLUDEDIR)/sleela_ballum/ballum_xwire.h" \
	      "$(DESTDIR)$(INCLUDEDIR)/sleela_ballum/ballum_xdraw.h" \
	      "$(DESTDIR)$(INCLUDEDIR)/sleela_ballum/ballum_x11proxy.h" \
	      "$(DESTDIR)$(INCLUDEDIR)/sleela_ballum/ballum_host_profiles.h" \
	      "$(DESTDIR)$(INCLUDEDIR)/sleela_ballum/ballum_header.h" \
	      "$(DESTDIR)$(INCLUDEDIR)/sleela_ballum/ballum_lms.h"
	rm -f "$(DESTDIR)$(LIBDIR)/pkgconfig/sleela_ballum.pc"
	-rmdir "$(DESTDIR)$(INCLUDEDIR)/sleela_ballum" 2>/dev/null || true
	@echo "ballum: uninstalled from $(DESTDIR)$(PREFIX)"

clean:
	rm -rf $(BUILD)

help:
	@echo "SLeeLa Ballum build (PLATFORM=$(PLATFORM); override PLATFORM=linux|macos|windows)"
	@echo "  make all         Verify OS deps, then build build/libsleela_ballum.a for this OS"
	@echo "  make check-deps  Report whether this OS's display libraries are available"
	@echo "  make deps        Show how to install the OS libraries (BALLUM_DEPS_INSTALL=1 to install)"
	@echo "  make demo        Link a real build/ballum-demo$(EXE) executable against the lib + this OS's libs"
	@echo "  make sanitize    Run the portable suite under ASan+UBSan"
	@echo "  make fuzz-smoke  Pseudo-random smoke run of the X wire framer (make fuzz: libFuzzer)"
	@echo "  make pc          Generate the pkg-config manifest (build/sleela_ballum.pc)"
	@echo "  make test        Build and run the portable tests (no display libraries required)"
	@echo "  make host        Link build/ballum-host: run a program inside the Ballum environment"
	@echo "  make test-x11    Real-environment test of the private-display program source"
	@echo "  make install     Install the library + headers + sleela_ballum.pc to \$$(PREFIX) [default $(PREFIX)]"
	@echo "  make uninstall   Remove the installed library + headers"
	@echo "  make clean       Remove the build/ directory"

# Exercise the Unix-domain X11 relay, including native peer-credential checks.
.PHONY: test-x11proxy
test-x11proxy: | $(BUILD)
	$(CC) $(CFLAGS) src/ballum_xwire.c src/ballum_xdraw.c src/ballum_x11proxy.c tests/test_ballum_x11proxy.c -pthread -lm -o $(BUILD)/ballum-x11proxy-test$(EXE)
	./$(BUILD)/ballum-x11proxy-test$(EXE)

.PHONY: all test test-x11 host clean install uninstall help check-deps deps demo pc test-x11proxy

# ---- Sanitizers and fuzzing -------------------------------------------------
# `make sanitize` re-runs the whole portable suite under ASan + UBSan in a
# separate build dir. Needs a compiler with the sanitizer runtimes installed.
sanitize:
	$(MAKE) test BUILD=build-san CC="$(CC)" \
	  CFLAGS="-std=c11 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=undefined -Wall -Wextra -Wpedantic -Iinclude"

# libFuzzer needs clang. `fuzz` builds the harness; run build/ballum-xwire-fuzz.
fuzz: | $(BUILD)
	clang -std=c11 -O1 -g -fsanitize=fuzzer,address,undefined -Iinclude \
	  src/ballum_xwire.c tests/fuzz_xwire.c -o $(BUILD)/ballum-xwire-fuzz

# Fixed pseudo-random smoke run: no libFuzzer runtime required.
fuzz-smoke: | $(BUILD)
	$(CC) $(CFLAGS) -DBALLUM_FUZZ_STANDALONE src/ballum_xwire.c tests/fuzz_xwire.c -o $(BUILD)/ballum-xwire-fuzz-smoke$(EXE)
	./$(BUILD)/ballum-xwire-fuzz-smoke$(EXE)

.PHONY: sanitize fuzz fuzz-smoke
