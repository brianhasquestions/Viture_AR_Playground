# =====================================================================
# 03_virtual_screen - pin your desktop to a spot in the room.
#
#   make            build bin/virtual_screen
#   make run        fullscreen on the glasses
#   make windowed   desktop-window preview
#   make clean
#
# Captures a Wayland output via wlr-screencopy and draws it on a
# world-locked quad, anchored by the Carina 6DoF pose.
# =====================================================================

EXAMPLE := virtual_screen

# --- Wayland protocol code generation --------------------------------
# The wlr-screencopy XML is vendored under protocols/ so the sample does
# not depend on a system wlr-protocols package. wayland-scanner turns it
# into a client header + glue code at build time.
SCANNER    := wayland-scanner
PROTO_XML  := protocols/wlr-screencopy-unstable-v1.xml
PROTO_NAME := wlr-screencopy-unstable-v1
PROTO_H    := build/$(PROTO_NAME)-client-protocol.h
PROTO_C    := build/$(PROTO_NAME)-protocol.c
PROTO_O    := build/$(PROTO_NAME)-protocol.o

EXTRA_OBJS := $(PROTO_O)

EXTRA_CFLAGS := $(shell pkg-config --cflags sdl2 wayland-client) -D_GNU_SOURCE
EXTRA_LDLIBS := $(shell pkg-config --libs sdl2 wayland-client) -lGL -lm

include ../../common.mk

.PHONY: windowed

# Generated sources must exist before anything that includes them.
$(OBJS): | $(PROTO_H)

$(PROTO_H): $(PROTO_XML) | dirs
	$(SCANNER) client-header $< $@

$(PROTO_C): $(PROTO_XML) | dirs
	$(SCANNER) private-code $< $@

# Generated code is not ours; compile it without -Werror/-Wpedantic.
$(PROTO_O): $(PROTO_C)
	$(CC) -std=c11 -D_GNU_SOURCE -I build -c $< -o $@

windowed: all
	@$(RUN_ENV) ./$(TARGET) --windowed
