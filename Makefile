# Fifth Wheel ("Juggernaut 3D for DOS-GL"), on the dgk kit (~/dgk). RetroGPU (DOS-GL, the HAL and the
# harness; RETROGPU) and dgk are the checkouts beside this one, on main as they are: no pins (the owner's
# decision, 2026-11-02).
#
#   make dos            build/dos/FWHEEL.EXE (DJGPP, DOS-GL, its SDL3)
#   make linux          build/linux/fwheel (SDL3, desktop OpenGL; built in the dev container)
#   make headless       build/headless/fwheel-hl (OSMesa, virtual clock; dev container)
#   make loopa [CARD=g450] [ARGS="-frames 60"]   run FWHEEL.EXE in 86Box: out/loopa-CARD/
#   make shots [CARD=g450]   the same frames from DOS (Loop A) and OSMesa, compared (tools/shots.sh)
#   make suite [CARD=g450] [CHECKS="sb16 sbpro"]   Loop A suites (tools/suite.sh)
#   make tests-host     unit tests on the host (governor, presets, settings, saves, the shop) and glcheck
#   make release        dist/fwheel-ID.zip: the game, CWSDPMI and the text files (tools/release.sh)
#   make winvm [CARD=g450]   dist/fwheel-CARD-vm.zip: an 86Box machine with the release on it
#   make jobsweep       the job autopilot on every depot pair and bay, headless (tools/jobsweep.sh)
#   make check-deps     RetroGPU's and dgk's checkouts are there (every build checks)
#   make regress [CARD=g450]   the quick regression set for a new RetroGPU (DOS-GL, the HAL) or dgk
include config.mk
-include config.local.mk
include $(DGK)/dgk.mk

MGAHAL   := $(RETROGPU)
DOSGL    := $(RETROGPU)/gl
DEV      := $(MGAHAL)/tools/dev
BUILD_ID := $(shell git describe --always --dirty 2>/dev/null || echo unknown)
Q ?= @

KIT_SRCS  := $(DGK_SRCS)
GAME_SRCS := $(wildcard game/src/*.c)
GEN_SRCS  := build/gen/font_gen.c
HDRS      := $(DGK_HDRS) $(wildcard game/src/*.h)
WARN      := -std=gnu99 -Wall -Wextra -Werror
# Every target compiles its GL against DOS-GL's own <GL/gl.h>: the subset is enforced.
COMMON    := $(WARN) -O2 -ffp-contract=off $(DGK_CFLAGS)

.PHONY: all dos linux headless loopa shots suite tests-host glcheck release winvm jobsweep check-deps regress deps clean help
all: dos

check-deps:
	@for d in "RETROGPU $(RETROGPU)/hal/include/mga/hal.h" "RETROGPU $(DOSGL)/include/GL/gl.h" "DGK $(DGK)/dgk.mk"; do \
	  [ -f "$${d#* }" ] || { echo "check-deps: no $${d%% *} checkout ($${d#* } is missing)"; exit 1; }; done

# The quick regression set for a new RetroGPU or dgk: unit tests, every job headless,
# DOS against Mesa, and the suite's sound, exit, crash, screen-size and
# agreement checks on one card. The full `make suite` on g200, g400 and
# g450 before a release.
regress: tests-host jobsweep shots
	$(Q)MGAHAL=$(MGAHAL) DEV=$(DEV) tools/suite.sh $(CARD) sb16 exit crash modes auto

build/gen/font_gen.c: data/font5x7.txt $(DGK_FONTBAKE)
	@mkdir -p $(dir $@)
	$(Q)python3 $(DGK_FONTBAKE) $< $@ fw_font

# ---- Data: packs made by host tools (the same files on every target) ------
TOOL_CFLAGS := $(WARN) -O2 -ffp-contract=off $(DGK_CFLAGS) -I$(DGK)/tools -Igame/src
build/tools/fwyard: tools/fwyard.c game/src/mesh.c $(DGK_PAKW) $(DGK)/src/base.c $(HDRS) $(DGK)/tools/pakw.h
	@mkdir -p $(dir $@)
	$(Q)$(HOST_CC) $(TOOL_CFLAGS) -o $@ tools/fwyard.c game/src/mesh.c $(DGK_PAKW) $(DGK)/src/base.c -lm
build/data/YARD.PAK: build/tools/fwyard
	@mkdir -p $(dir $@)
	$(Q)build/tools/fwyard $@
build/tools/fwgen: tools/fwgen.c game/src/mesh.c $(DGK_PAKW) $(DGK)/src/base.c game/src/wgen.h $(HDRS) $(DGK)/tools/pakw.h
	@mkdir -p $(dir $@)
	$(Q)$(HOST_CC) $(TOOL_CFLAGS) -o $@ tools/fwgen.c game/src/mesh.c $(DGK_PAKW) $(DGK)/src/base.c -lm
WORLD_SEED ?= 1
build/data/WORLD.PAK: build/tools/fwgen
	@mkdir -p $(dir $@)
	$(Q)build/tools/fwgen $(WORLD_SEED) $@
data: build/data/YARD.PAK build/data/WORLD.PAK build/data/budget.cfg
build/data/budget.cfg: data/budget.cfg
	@mkdir -p $(dir $@)
	$(Q)cp $< $@
# The generator is deterministic: seed 1 must give data/golden/world.sha's
# hash when built at -O0 and at -O2. Change the golden file only on purpose.
data-check:
	$(Q)mkdir -p build/check
	$(Q)$(HOST_CC) $(filter-out -O2,$(TOOL_CFLAGS)) -O0 -o build/check/fwgen-O0 tools/fwgen.c game/src/mesh.c \
	  $(DGK_PAKW) $(DGK)/src/base.c -lm
	$(Q)$(HOST_CC) $(TOOL_CFLAGS) -o build/check/fwgen-O2 tools/fwgen.c game/src/mesh.c $(DGK_PAKW) $(DGK)/src/base.c -lm
	$(Q)for o in O0 O2; do build/check/fwgen-$$o 1 build/check/w-$$o.pak | sed -n 's/.*hash //p' > build/check/$$o.sha; done
	$(Q)cmp -s build/check/O0.sha build/check/O2.sha || { echo "data-check: -O0 and -O2 differ"; exit 1; }
	$(Q)cmp -s build/check/O2.sha data/golden/world.sha \
	  || { echo "data-check: world hash $$(cat build/check/O2.sha), golden $$(cat data/golden/world.sha)"; exit 1; }
	$(Q)echo "data-check: world $$(cat data/golden/world.sha) at -O0 and -O2"
.PHONY: data data-check

# ---- DOS -------------------------------------------------------------------
DJCC := env LD_LIBRARY_PATH=$(DJGPP_PREFIX)/hostlib $(DJGPP_PREFIX)/bin/i586-pc-msdosdjgpp-gcc
DOS_CFLAGS := $(COMMON) -march=i586 -fexcess-precision=standard -I$(DOSGL)/include \
              -I$(DOSGL)/build/sdl/dos/include -I$(MGAHAL)/tests/shim -I$(MGAHAL)/hal/include -DHX_BUILD_ID='"$(BUILD_ID)"'
DOS_LIBS := $(DOSGL)/build/sdl/dos/lib/libSDL3.a $(DOSGL)/build/lib/libGL.a -lm
build/dos/FWHEEL.EXE: build/data/WORLD.PAK $(KIT_SRCS) $(DGK_SDL) $(GAME_SRCS) $(GEN_SRCS) $(HDRS) $(MGAHAL)/tests/shim/hx.c \
                      $(DOSGL)/build/sdl/dos/lib/libSDL3.a $(DOSGL)/build/lib/libGL.a
	@mkdir -p $(dir $@)
	$(Q)echo "  DJLD    $@"
	$(Q)$(DJCC) $(DOS_CFLAGS) -o $@ $(KIT_SRCS) $(DGK_SDL) $(GAME_SRCS) $(GEN_SRCS) \
	  $(MGAHAL)/tests/shim/hx.c $(DOS_LIBS)
	@if [ -e "$(@:.EXE=.exe)" ] && ! [ "$(@:.EXE=.exe)" -ef "$@" ]; then rm -f "$(@:.EXE=.exe)"; fi
dos: check-deps build/dos/FWHEEL.EXE build/data/WORLD.PAK build/data/YARD.PAK build/data/budget.cfg

# ---- Linux and headless: built in the dev container ------------------------
# The container (RetroGPU's tools/dev) sees RetroGPU, this repository and
# (MGA_EXTRA_DIR) dgk; DOS-GL's headers and the host SDL3 are staged into
# build/deps all the same, outside it, so the headless build's paths stay here.
ifdef IN_DEV
build/deps/stamp: ;                     # staged outside the container
else
build/deps/stamp: $(DOSGL)/include/GL/gl.h $(DOSGL)/build/sdl/host/lib/libSDL3.a
	$(Q)rm -rf build/deps && mkdir -p build/deps/include build/deps/lib
	$(Q)cp -r $(DOSGL)/include/GL build/deps/include/
	$(Q)cp -r $(DOSGL)/build/sdl/host/include/SDL3 build/deps/include/
	$(Q)cp $(DOSGL)/build/sdl/host/lib/libSDL3.a build/deps/lib/
	$(Q)touch $@
endif
deps: build/deps/stamp

HOST_CFLAGS := $(COMMON) -g -Ibuild/deps/include
build/linux/fwheel: build/data/WORLD.PAK $(KIT_SRCS) $(DGK_SDL) $(GAME_SRCS) $(GEN_SRCS) $(HDRS) build/deps/stamp
	@mkdir -p $(dir $@)
	$(Q)echo "  CC      $@"
	$(Q)$(HOST_CC) $(HOST_CFLAGS) -o $@ $(KIT_SRCS) $(DGK_SDL) $(GAME_SRCS) $(GEN_SRCS) \
	  build/deps/lib/libSDL3.a -lGL -lm -ldl -lpthread
build/headless/fwheel-hl: build/data/WORLD.PAK $(KIT_SRCS) $(DGK_HEADLESS) $(GAME_SRCS) $(GEN_SRCS) $(HDRS) build/deps/stamp
	@mkdir -p $(dir $@)
	$(Q)echo "  CC      $@"
	$(Q)$(HOST_CC) $(HOST_CFLAGS) -o $@ $(KIT_SRCS) $(DGK_HEADLESS) $(GAME_SRCS) $(GEN_SRCS) \
	  -lOSMesa -lm
linux: check-deps build/deps/stamp $(GEN_SRCS) build/data/WORLD.PAK
	$(Q)MGA_EXTRA_DIR=$(DGK) $(DEV) $(MAKE) -s IN_DEV=1 DGK=$(DGK) build/linux/fwheel
headless: check-deps build/deps/stamp $(GEN_SRCS) build/data/WORLD.PAK
	$(Q)MGA_EXTRA_DIR=$(DGK) $(DEV) $(MAKE) -s IN_DEV=1 DGK=$(DGK) build/headless/fwheel-hl

# ---- Loop A ----------------------------------------------------------------
CARD ?= g450
ARGS ?= -test -fixed -autopilot -laps 1 -hash
LOOPA_TIMEOUT ?= 600
loopa: dos
	$(Q)$(MGAHAL)/tools/dev python3 $(MGAHAL)/tools/loopa/run.py --name fwheel --card $(CARD) \
	  --exe build/dos/FWHEEL.EXE --file build/data/WORLD.PAK --args="$(ARGS)" --out $(CURDIR)/out/loopa-$(CARD) --sound sb16 \
	  --pre "SET BLASTER=A220 I5 D1 H5 T6" --idle 90 --timeout $(LOOPA_TIMEOUT); cat out/loopa-$(CARD)/status

shots: dos headless
	$(Q)MGAHAL=$(MGAHAL) DEV=$(DEV) sh tools/shots.sh $(CARD)

suite: dos
	$(Q)MGAHAL=$(MGAHAL) DEV=$(DEV) tools/suite.sh $(CARD) $(CHECKS)

# Unit tests that need no screen: the host's compiler, the kit's and the game's pure parts.
build/tests/test_host: game/tests/test_host.c game/src/career.c game/src/detail.c $(DGK)/src/cfg.c $(DGK)/src/save.c \
                       $(DGK)/src/base.c $(DGK)/src/log.c $(HDRS)
	@mkdir -p $(dir $@)
	$(Q)echo "  CC      $@"
	$(Q)$(CC) $(TOOL_CFLAGS) -o $@ game/tests/test_host.c game/src/career.c game/src/detail.c $(DGK)/src/cfg.c \
	  $(DGK)/src/save.c $(DGK)/src/base.c $(DGK)/src/log.c -lm
tests-host: build/tests/test_host glcheck
	$(Q)mkdir -p out && build/tests/test_host

# Every GL function the game and the kit call is one DOS-GL implements.
glcheck:
	$(Q)python3 $(DGK)/tools/glcheck.py $(DOSGL) $(wildcard game/src/*.c) $(wildcard $(DGK)/src/*.c)

# The DOS release: dist/fwheel-ID.zip (tools/release.sh).
CWSDPMI_ZIP ?= $(HOME)/.cache/mga-glide/dl/csdpmi7b.zip
# A ready-to-boot 86Box machine with the release installed (tools/winvm.sh).
winvm:
	$(Q)MGAHAL=$(MGAHAL) tools/winvm.sh $(CARD)

release: dos tests-host
	$(Q)DOSGL=$(DOSGL) RETROGPU=$(RETROGPU) DJGPP_PREFIX=$(DJGPP_PREFIX) CWSDPMI_ZIP=$(CWSDPMI_ZIP) sh tools/release.sh

jobsweep: headless
	$(Q)$(DEV) sh tools/jobsweep.sh $(or $(PAR),8)

clean:
	rm -rf build out dist

help:
	@sed -n '4,15p' Makefile
