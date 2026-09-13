# =====================================================================
# 04_glyph_reader - recognise a hand-drawn glyph and reveal a message
# that is bound to a specific pair of glasses.
#
#   make            build bin/glyph_reader and bin/glyphtool
#   make run        run the reader (live camera)
#   make glyphtool  build only the authoring tool
#   make clean
#
# The reader (bin/glyph_reader) links the SDK and the shared common/
# modules. The authoring tool (bin/glyphtool) is pure logic - glyph,
# detect, sha256, binding - and links no SDK, so it builds and runs
# anywhere.
# =====================================================================

EXAMPLE := glyph_reader

include ../../common.mk

# --- Authoring tool (second binary) ----------------------------------
# Reuses the object files common.mk already builds for the reader, minus
# the reader's own main. tools/glyphtool.c provides its own main().
GLYPHTOOL_OBJS := $(OBJ_DIR)/glyph.o $(OBJ_DIR)/detect.o \
                  $(OBJ_DIR)/common_sha256.o $(OBJ_DIR)/binding.o
GLYPHTOOL_BIN  := $(BIN_DIR)/glyphtool

all: $(GLYPHTOOL_BIN)

.PHONY: glyphtool
glyphtool: $(GLYPHTOOL_BIN)

$(GLYPHTOOL_BIN): tools/glyphtool.c $(GLYPHTOOL_OBJS) | dirs
	$(CC) $(CFLAGS) $^ -lm -o $@
	@echo "Built $@"
