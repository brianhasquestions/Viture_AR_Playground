# =====================================================================
# 05_drawing_vault - draw something, seal a message to it and your
# glasses, then reveal it later by looking at the drawing.
#
#   make            build bin/drawing_vault
#   make run ARGS="recall --display"      reveal on the glasses
#   make run ARGS="enroll --message ..."  store a message
#   make clean
#
# Frameless: the drawing is fingerprinted with a perceptual hash (no
# marker frame). The message is sealed under the glasses SN hash, so it
# only opens on the device it was enrolled on. Pure C except SDL2, which
# is used only to put the revealed message on the glasses screen.
# =====================================================================

EXAMPLE := drawing_vault

EXTRA_CFLAGS := $(shell pkg-config --cflags sdl2)
EXTRA_LDLIBS := $(shell pkg-config --libs sdl2)

include ../../common.mk
