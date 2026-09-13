# =====================================================================
# 04_sealed_drawing - seal a message to a painted object and one specific
# pair of glasses, then reveal it by looking at the drawing.
#
#   make                                   build bin/sealed_drawing
#   make run ARGS="identity"               print this pair's public key
#   make run ARGS="seal --message ..."     seal a message
#   make run ARGS="reveal --display"       reveal on the glasses
#   make clean
#
# The crypto is real: X25519 + HKDF-SHA256 + AES-256-GCM from
# libcrypto (OpenSSL 3), with the X25519 identity derived from the
# glasses' hardware serial hash. SDL2 is used only to put the revealed
# message on the glasses screen.
# =====================================================================

EXAMPLE := sealed_drawing

EXTRA_CFLAGS := $(shell pkg-config --cflags sdl2 libcrypto)
EXTRA_LDLIBS := $(shell pkg-config --libs sdl2 libcrypto)

include ../../common.mk
