# =====================================================================
# 01_camera_feed - pull the live camera feed from VITURE glasses.
#
#   make          build bin/camera_feed
#   make run      capture 10 frames to bin/captures/
#   make view     live preview window (needs ffplay)
#   make test     smoke test
#   make clean    remove build artifacts
# =====================================================================

EXAMPLE := camera_feed

include ../../common.mk

.PHONY: view test

# Capture stills into bin/captures/.
run: | captures
captures:
	@mkdir -p $(BIN_DIR)/captures

# Live preview. The harness writes raw MJPEG to stdout with --stream;
# ffplay decodes it. Low-latency flags keep the view responsive.
view: all
	@command -v ffplay >/dev/null || { echo "ffplay not found (install ffmpeg)"; exit 1; }
	@echo "Opening live camera view - press Q to close"
	@$(RUN_ENV) ./$(TARGET) --stream \
	    | ffplay -hide_banner -loglevel error \
	             -f mjpeg -fflags nobuffer -flags low_delay \
	             -window_title "VITURE Luma Ultra - camera feed" -i -

test: all | captures
	@$(RUN_ENV) ./test/run_camera_test.sh
