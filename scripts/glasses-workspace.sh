#!/usr/bin/env bash
# =====================================================================
# glasses-workspace.sh - world-locked desktop in the glasses.
#
# Meant to run when you switch to glasses-only mode (laptop panel off).
#
# The problem it solves: with the laptop disabled, Hyprland moves your
# windows onto the glasses output - which is exactly where the overlay
# draws, so the overlay would cover them, and there would be nothing
# left to capture. Capturing the glasses itself is a feedback loop.
#
# So we give the desktop somewhere else to live:
#
#   1. Create a headless output (a real Hyprland monitor, off-screen).
#   2. Move every workspace that has windows onto it.
#   3. Pin the overlay to the glasses output, and capture the headless.
#
# Net effect: the laptop panel is off, the glasses show only the
# overlay, and the overlay shows your real desktop - world-locked in
# space. Quitting the overlay (Ctrl+Alt+Shift+Q) moves the workspaces
# back and removes the headless output.
#
# Usage:  glasses-workspace.sh [extra virtual_screen args...]
# =====================================================================
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
EXAMPLE_DIR="${ROOT_DIR}/examples/03_virtual_screen"
BIN="${EXAMPLE_DIR}/bin/virtual_screen"

case "$(uname -m)" in
    aarch64) SDK_LIB="${ROOT_DIR}/sdk/viture_arm64/aarch64" ;;
    *)       SDK_LIB="${ROOT_DIR}/sdk/viture_x86_64/x86_64" ;;
esac

for tool in hyprctl jq; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "glasses-workspace: '$tool' is required." >&2; exit 1; }
done

if pgrep -x virtual_screen >/dev/null 2>&1; then
    echo "glasses-workspace: already running." >&2
    exit 0
fi

if [[ ! -x "${BIN}" ]]; then
    echo "glasses-workspace: building..." >&2
    make -C "${EXAMPLE_DIR}" >/dev/null || exit 1
fi

# --- Identify the glasses output (the VITURE one) --------------------
GLASSES=$(hyprctl monitors all -j | jq -r \
    '.[] | select((.description // "") | test("VITURE"; "i")) | .name' \
    | head -1)
if [[ -z ${GLASSES} ]]; then
    echo "glasses-workspace: no VITURE display found." >&2
    exit 1
fi

# --- 1. Create the headless output the desktop will live on ----------
BEFORE=$(hyprctl monitors all -j | jq -r '.[].name' | sort)
hyprctl output create headless >/dev/null
sleep 0.4
AFTER=$(hyprctl monitors all -j | jq -r '.[].name' | sort)
HL=$(comm -13 <(echo "${BEFORE}") <(echo "${AFTER}") | head -1)

if [[ -z ${HL} ]]; then
    echo "glasses-workspace: could not create a headless output." >&2
    exit 1
fi

# Give it a normal desktop resolution.
hyprctl keyword monitor "${HL},1920x1080@60,auto,1" >/dev/null
sleep 0.3

# --- 2. Move every populated workspace onto the headless -------------
# This is the key step: it is what puts your actual windows on the
# monitor the overlay is going to capture.
mapfile -t WORKSPACES < <(hyprctl workspaces -j \
    | jq -r --arg hl "${HL}" \
        '.[] | select(.windows > 0) | select(.monitor != $hl) | .id')

for ws in "${WORKSPACES[@]}"; do
    hyprctl dispatch moveworkspacetomonitor "${ws} ${HL}" >/dev/null
done

# --- 3. Pin the overlay to the glasses, so it never lands on the -----
#        headless output it is capturing (that would be a feedback loop).
hyprctl keyword windowrulev2 \
    "monitor ${GLASSES}, class:^(virtual_screen)$" >/dev/null
hyprctl dispatch focusmonitor "${GLASSES}" >/dev/null

LOG="${TMPDIR:-/tmp}/glasses-workspace.log"
echo "glasses-workspace: desktop -> ${HL}, overlay -> ${GLASSES}"
echo "glasses-workspace: log ${LOG}"

LD_LIBRARY_PATH="${SDK_LIB}:${LD_LIBRARY_PATH:-}" \
    setsid "${BIN}" --output "${HL}" "$@" >"${LOG}" 2>&1 < /dev/null &
APP_PID=$!

# --- 4. When the overlay exits, put the desktop back -----------------
(
    while kill -0 "${APP_PID}" 2>/dev/null; do sleep 2; done

    # Somewhere real to put the workspaces back: any enabled, non-headless
    # output (the laptop once display-both/display-laptop re-enables it,
    # otherwise the glasses).
    HOME_MON=$(hyprctl monitors -j | jq -r --arg hl "${HL}" \
        '.[] | select(.name != $hl) | .name' | head -1)

    if [[ -n ${HOME_MON} ]]; then
        for ws in $(hyprctl workspaces -j | jq -r --arg hl "${HL}" \
                    '.[] | select(.monitor == $hl) | .id'); do
            hyprctl dispatch moveworkspacetomonitor \
                "${ws} ${HOME_MON}" >/dev/null
        done
    fi

    hyprctl output remove "${HL}" >/dev/null 2>&1
) >/dev/null 2>&1 &

disown 2>/dev/null || true
