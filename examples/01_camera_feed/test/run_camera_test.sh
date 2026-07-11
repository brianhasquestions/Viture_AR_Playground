#!/usr/bin/env bash
# =====================================================================
# run_camera_test.sh - smoke test for the VITURE camera-feed harness.
#
# Verifies that:
#   1. The binary was built.
#   2. It runs and exits successfully (frames were received).
#   3. At least one .jpg frame was written to bin/captures.
#
# Exits 0 on pass, non-zero on failure.
# =====================================================================
set -u

# Resolve project root relative to this script.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${ROOT_DIR}"

BIN="bin/camera_feed"
CAPTURE_DIR="bin/captures"

echo "== VITURE camera-feed smoke test =="

if [[ ! -x "${BIN}" ]]; then
    echo "FAIL: ${BIN} not built (run 'make' first)"
    exit 1
fi

# Clear any prior captures so the count reflects this run.
rm -f "${CAPTURE_DIR}"/frame_*.jpg 2>/dev/null || true

echo "-- running harness --"
"./${BIN}"
RC=$?

echo "-- checking results --"
if [[ ${RC} -ne 0 ]]; then
    echo "FAIL: harness exited with code ${RC} (no frames captured?)"
    exit 1
fi

FRAME_COUNT=$(find "${CAPTURE_DIR}" -maxdepth 1 -name 'frame_*.jpg' 2>/dev/null | wc -l)
if [[ "${FRAME_COUNT}" -lt 1 ]]; then
    echo "FAIL: no frames written to ${CAPTURE_DIR}"
    exit 1
fi

echo "PASS: harness exited 0 and wrote ${FRAME_COUNT} frame(s) to ${CAPTURE_DIR}"
exit 0
