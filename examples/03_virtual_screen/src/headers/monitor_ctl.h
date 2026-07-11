/**********************************************************************
 * @file    monitor_ctl.h
 * @brief   Spawn and remove Hyprland outputs via hyprctl.
 * @copyright 2026 XR_Playground. Educational sample code.
 *
 * Wraps `hyprctl output create/remove` so the sample can conjure a
 * genuine extra desktop (its own workspaces, not a mirror) on demand and
 * tear it down again. Hyprland only; other compositors are a no-op that
 * reports failure.
 *
 * Coding standard: Barr-C:2018.
 **********************************************************************/

#ifndef MONITOR_CTL_H
#define MONITOR_CTL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**********************************************************************
 * @brief  Create a headless Hyprland output and return its name.
 *
 * Diffs the monitor list before and after creation to discover the new
 * connector name (e.g. "HEADLESS-2").
 *
 * @param[out] p_name  Buffer receiving the new output name.
 * @param[in]  size    Size of p_name in bytes.
 *
 * @return  0 on success, -1 on failure.
 **********************************************************************/
int monitor_ctl_create_headless(char * p_name, size_t size);

/**********************************************************************
 * @brief  Remove a previously created Hyprland output.
 *
 * @param[in]  p_name  Output name from monitor_ctl_create_headless().
 *
 * @return  0 on success, -1 on failure.
 **********************************************************************/
int monitor_ctl_remove(const char * p_name);

/**********************************************************************
 * @brief  Warp the pointer to an output and focus it.
 *
 * Moves the Hyprland cursor to the centre of the named output and makes
 * it the focused monitor, so keyboard and mouse land on the desktop
 * shown by the selected virtual screen.
 *
 * @param[in]  p_name  Output name, e.g. "eDP-1" or "HEADLESS-2".
 *
 * @return  0 on success, -1 on failure.
 **********************************************************************/
int monitor_ctl_focus_output(const char * p_name);

#ifdef __cplusplus
}
#endif

#endif /* MONITOR_CTL_H */
