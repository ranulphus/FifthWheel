# Build configuration; override in config.local.mk (not committed).
# RetroGPU: DOS-GL (gl/), the HAL and the harness, in one checkout.
RETROGPU     ?= $(HOME)/RetroGPU
DGK          ?= $(HOME)/dgk
# GLOS: its FM synthesiser (sdk/fm, General MIDI on the FM chip) and library, for dgk's music (dgk/midi.h).
GLOS         ?= $(HOME)/GLOS
DJGPP_PREFIX ?= $(HOME)/.local/opt/djgpp-gcc1220
HOST_CC      ?= gcc
