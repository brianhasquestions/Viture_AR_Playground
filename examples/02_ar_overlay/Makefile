# =====================================================================
# 02_ar_overlay - optical see-through AR on the glasses display.
#
#   make            build bin/ar_overlay
#   make run        fullscreen on the glasses, world-locked overlay
#   make windowed   preview in a desktop window (no glasses needed)
#   make clean
# =====================================================================

EXAMPLE := ar_overlay

# SDL2 for the window/GL context; libGL for rendering; libm for the math.
EXTRA_CFLAGS := $(shell pkg-config --cflags sdl2)
EXTRA_LDLIBS := $(shell pkg-config --libs sdl2) -lGL -lm

include ../../common.mk

.PHONY: windowed

windowed: all
	@$(RUN_ENV) ./$(TARGET) --windowed
