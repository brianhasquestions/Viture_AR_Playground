#!/usr/bin/env bash
# =====================================================================
# install_udev_rules.sh - grant this user access to the VITURE camera.
#
# The VITURE camera USB node is owned by root:root with mode 0664, so a
# normal user cannot claim it and the SDK fails with "Access denied".
# This installs a udev rule that hands the device to the active user.
#
# Run with: sudo ./scripts/install_udev_rules.sh
# =====================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RULE_SRC="${SCRIPT_DIR}/70-viture.rules"
RULE_DST="/etc/udev/rules.d/70-viture.rules"

if [[ "${EUID}" -ne 0 ]]; then
    echo "Please run as root: sudo $0"
    exit 1
fi

install -m 0644 "${RULE_SRC}" "${RULE_DST}"
echo "Installed ${RULE_DST}"

udevadm control --reload-rules
udevadm trigger
echo "Reloaded udev rules."
echo "Now unplug and replug the VITURE glasses, then run: make run"
