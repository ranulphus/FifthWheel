# Fifth Wheel ("Juggernaut 3D for DOS-GL") and the dgk kit.
#
#   make dos            build/dos/FWHEEL.EXE (DJGPP, DOS-GL, DOSGL's SDL3)
#   make linux          build/linux/fwheel (SDL3, desktop OpenGL; built in the dev container)
#   make headless       build/headless/fwheel-hl (OSMesa, virtual clock; dev container)
#   make loopa [CARD=g450] [ARGS="-frames 60"]   run FWHEEL.EXE in 86Box: out/loopa-CARD/
#   make shots [CARD=g450]   the same frames from DOS (Loop A) and OSMesa, compared (tools/shots.sh)
#   make suite [CARD=g450] [CHECKS="sb16 sbpro"]   Loop A suites (tools/suite.sh)
#   make jobsweep       the job autopilot on every depot pair and bay, headless (tools/jobsweep.sh)
#   make check-deps     DOSGL at or after deps.mk's pin
include config.mk
-include config.local.mk
include deps.mk

MGAHAL   := $(DOSGL)/third_party/mgahal
DEV      := $(MGAHAL)/tools/dev
BUILD_ID := $(shell git describe --always --dirty 2>/dev/null || echo unknown)
Q ?= @

KIT_SRCS  := kit/src/base.c kit/src/cfg.c kit/src/log.c kit/src/app.c kit/src/gfx.c kit/src/text.c kit/src/mix.c kit/src/test.c \
             kit/src/pak.c kit/src/replay.c kit/src/bench.c
GAME_SRCS := $(wildcard game/src/*.c)
GEN_SRCS  := build/gen/font_gen.c
HDRS      := $(wildcard kit/include/dgk/*.h kit/src/*.h game/src/*.h)
WARN      := -std=gnu99 -Wall -Wextra -Werror
# Every target compiles its GL against DOS-GL's own <GL/gl.h>: the subset is enforced.
COMMON    := $(WARN) -O2 -ffp-contract=off -Ikit/include -Ikit/src

.PHONY: all dos linux headless loopa shots suite jobsweep check-deps deps clean help
all: dos

check-deps:
	@git -C "$(DOSGL)" merge-base --is-ancestor "$(DOSGL_PIN)" HEAD 2>/dev/null \
	  || { echo "$(DOSGL) is not at or after $(DOSGL_PIN) (deps.mk)"; exit 1; }

build/gen/font_gen.c: data/font5x7.txt kit/tools/fontbake.py
	@mkdir -p $(dir $@)
	$(Q)python3 kit/tools/fontbake.py $< $@ fw_font

# ---- Data: packs made by host tools (the same files on every target) ------
TOOL_CFLAGS := $(WARN) -O2 -ffp-contract=off -Ikit/include -Ikit/tools -Igame/src
build/tools/fwyard: tools/fwyard.c game/src/mesh.c kit/tools/pakw.c kit/src/base.c $(HDRS) kit/tools/pakw.h
	@mkdir -p $(dir $@)
	$(Q)$(HOST_CC) $(TOOL_CFLAGS) -o $@ tools/fwyard.c game/src/mesh.c kit/tools/pakw.c kit/src/base.c -lm
build/data/YARD.PAK: build/tools/fwyard
	@mkdir -p $(dir $@)
	$(Q)build/tools/fwyard $@
build/tools/fwgen: tools/fwgen.c game/src/mesh.c kit/tools/pakw.c kit/src/base.c game/src/wgen.h $(HDRS) kit/tools/pakw.h
	@mkdir -p $(dir $@)
	$(Q)$(HOST_CC) $(TOOL_CFLAGS) -o $@ tools/fwgen.c game/src/mesh.c kit/tools/pakw.c kit/src/base.c -lm
WORLD_SEED ?= 1
build/data/WORLD.PAK: build/tools/fwgen
	@mkdir -p $(dir $@)
	$(Q)build/tools/fwgen $(WORLD_SEED) $@
data: build/data/YARD.PAK build/data/WORLD.PAK
# The generator is deterministic: seed 1 must give data/golden/world.sha's
# hash when built at -O0 and at -O2. Change the golden file only on purpose.
data-check:
	$(Q)mkdir -p build/check
	$(Q)$(HOST_CC) $(filter-out -O2,$(TOOL_CFLAGS)) -O0 -o build/check/fwgen-O0 tools/fwgen.c game/src/mesh.c \
	  kit/tools/pakw.c kit/src/base.c -lm
	$(Q)$(HOST_CC) $(TOOL_CFLAGS) -o build/check/fwgen-O2 tools/fwgen.c game/src/mesh.c kit/tools/pakw.c kit/src/base.c -lm
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
build/dos/FWHEEL.EXE: build/data/WORLD.PAK $(KIT_SRCS) kit/src/plat_sdl.c $(GAME_SRCS) $(GEN_SRCS) $(HDRS) $(MGAHAL)/tests/shim/hx.c \
                      $(DOSGL)/build/sdl/dos/lib/libSDL3.a $(DOSGL)/build/lib/libGL.a
	@mkdir -p $(dir $@)
	$(Q)echo "  DJLD    $@"
	$(Q)$(DJCC) $(DOS_CFLAGS) -o $@ $(KIT_SRCS) kit/src/plat_sdl.c $(GAME_SRCS) $(GEN_SRCS) \
	  $(MGAHAL)/tests/shim/hx.c $(DOS_LIBS)
	@if [ -e "$(@:.EXE=.exe)" ] && ! [ "$(@:.EXE=.exe)" -ef "$@" ]; then rm -f "$(@:.EXE=.exe)"; fi
dos: build/dos/FWHEEL.EXE build/data/WORLD.PAK build/data/YARD.PAK

# ---- Linux and headless: built in the dev container ------------------------
# The container sees this repository, not DOSGL: stage what it needs first.
ifdef IN_DEV
build/deps/stamp: ;                     # staged outside, where DOSGL is visible
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
build/linux/fwheel: build/data/WORLD.PAK $(KIT_SRCS) kit/src/plat_sdl.c $(GAME_SRCS) $(GEN_SRCS) $(HDRS) build/deps/stamp
	@mkdir -p $(dir $@)
	$(Q)echo "  CC      $@"
	$(Q)$(HOST_CC) $(HOST_CFLAGS) -o $@ $(KIT_SRCS) kit/src/plat_sdl.c $(GAME_SRCS) $(GEN_SRCS) \
	  build/deps/lib/libSDL3.a -lGL -lm -ldl -lpthread
build/headless/fwheel-hl: build/data/WORLD.PAK $(KIT_SRCS) kit/src/plat_headless.c $(GAME_SRCS) $(GEN_SRCS) $(HDRS) build/deps/stamp
	@mkdir -p $(dir $@)
	$(Q)echo "  CC      $@"
	$(Q)$(HOST_CC) $(HOST_CFLAGS) -o $@ $(KIT_SRCS) kit/src/plat_headless.c $(GAME_SRCS) $(GEN_SRCS) \
	  -lOSMesa -lm
linux: build/deps/stamp $(GEN_SRCS) build/data/WORLD.PAK
	$(Q)$(DEV) $(MAKE) -s IN_DEV=1 build/linux/fwheel
headless: build/deps/stamp $(GEN_SRCS) build/data/WORLD.PAK
	$(Q)$(DEV) $(MAKE) -s IN_DEV=1 build/headless/fwheel-hl

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

jobsweep: headless
	$(Q)$(DEV) sh tools/jobsweep.sh $(or $(PAR),8)

clean:
	rm -rf build out

help:
	@sed -n '3,11p' Makefile
