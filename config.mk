# Build configuration; override in config.local.mk (not committed).
# RetroGPU: DOS-GL (gl/), the HAL and the harness, in one checkout.
RETROGPU     ?= $(HOME)/RetroGPU
DGK          ?= $(HOME)/dgk
DJGPP_PREFIX ?= $(HOME)/.local/opt/djgpp-gcc1220
HOST_CC      ?= gcc
