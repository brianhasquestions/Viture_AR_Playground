#!/usr/bin/env bash
# =====================================================================
# install-bashrc.sh - wire the glasses spatial overlay into your shell.
#
# Adds a small block to ~/.bashrc so that:
#   * switching to glasses-only mode (the `display-external` function, if
#     you have one) also launches the world-locked overlay, and
#   * a standalone `xr-glasses` command is available anywhere.
#
# Idempotent: does nothing if the hook is already present. Re-run safely.
# Undo with:  install-bashrc.sh --uninstall
# =====================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)"
LAUNCHER="${SCRIPT_DIR}/glasses-workspace.sh"
BASHRC="${HOME}/.bashrc"

BEGIN='# >>> XR_Playground glasses-workspace >>>'
END='# <<< XR_Playground glasses-workspace <<<'

# --- Uninstall -------------------------------------------------------
if [[ "${1:-}" == "--uninstall" ]]; then
    if [[ -f ${BASHRC} ]] && grep -qF "${BEGIN}" "${BASHRC}"; then
        # Delete the marker block (and a leading blank line if present).
        sed -i "/^${BEGIN}\$/,/^${END}\$/d" "${BASHRC}"
        echo "Removed the glasses-workspace hook from ${BASHRC}."
        echo "Open a new terminal (or 'source ~/.bashrc') to apply."
    else
        echo "No glasses-workspace hook found in ${BASHRC}; nothing to do."
    fi
    exit 0
fi

# --- Already installed? ----------------------------------------------
# Covers both this script's marker block and any earlier hand-added
# reference to the launcher, so we never wire it up twice.
if [[ -f ${BASHRC} ]] && grep -qF 'glasses-workspace.sh' "${BASHRC}"; then
    echo "Already installed: ~/.bashrc references the launcher."
    echo "Run 'display-external' (or 'xr-glasses') to start the overlay."
    exit 0
fi

# --- Append the hook -------------------------------------------------
{
    echo ""
    echo "${BEGIN}"
    echo "# Launch the spatial virtual-screen overlay for the VITURE"
    echo "# glasses. See ${SCRIPT_DIR}."
    echo "_xr_glasses_launcher=\"${LAUNCHER}\""
    echo '# Standalone command: start the overlay (creates a headless'
    echo '# output, moves your desktop onto it, captures it in the glasses).'
    echo 'xr-glasses() {'
    echo '    [ -x "$_xr_glasses_launcher" ] && "$_xr_glasses_launcher" "$@"'
    echo '}'
    echo '# If a display-external function exists (glasses-only switch),'
    echo '# wrap it so it also launches the overlay.'
    echo 'if declare -f display-external >/dev/null 2>&1 \'
    echo '   && ! declare -f _xr_orig_display_external >/dev/null 2>&1; then'
    echo '    eval "_xr_orig_display_external() $(declare -f display-external | tail -n +2)"'
    echo '    display-external() {'
    echo '        _xr_orig_display_external "$@" || return'
    echo '        xr-glasses'
    echo '    }'
    echo 'fi'
    echo "${END}"
} >> "${BASHRC}"

echo "Installed the glasses-workspace hook into ${BASHRC}."
echo "Open a new terminal (or 'source ~/.bashrc'), then:"
echo "  display-external   # glasses-only + overlay  (if you have it)"
echo "  xr-glasses         # just the overlay, any time"
