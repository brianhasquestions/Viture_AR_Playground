/**********************************************************************
 * @file    input_evdev.h
 * @brief   Global keyboard input via Linux evdev, focus-independent.
 * @copyright 2026 XR_Playground. Educational sample code.
 *
 * The overlay renders a fullscreen window on the glasses display, but
 * the wearer keeps typing in windows on the laptop panel. Under Wayland
 * keyboard events only reach the FOCUSED window, so the overlay's SDL
 * window never sees a keypress and its controls appear dead.
 *
 * This module reads the keyboard directly from /dev/input/event*, which
 * is delivered regardless of which window has focus. Reads are passive:
 * the compositor and the focused application still receive the same keys
 * normally. A modifier combo is required so ordinary typing is ignored.
 *
 * Access: the user must be able to read /dev/input/event* (typically
 * membership of the "input" group). No root required.
 *
 * Coding standard: Barr-C:2018.
 **********************************************************************/

#ifndef INPUT_EVDEV_H
#define INPUT_EVDEV_H

#include <stdint.h>

#include "screen_render.h"   /* screen_input_t, SCREEN_MOD_* */

/**********************************************************************
 * @brief  Reader state (heap allocated). Opaque.
 **********************************************************************/
typedef struct input_evdev input_evdev_t;

#ifdef __cplusplus
extern "C"
{
#endif

/**********************************************************************
 * @brief  Open all keyboard devices under /dev/input.
 *
 * @param[in]  mod_mask  Modifiers (OR of SCREEN_MOD_*) that must all be
 *                       held for a control key to count.
 *
 * @return  New reader, or NULL if no readable keyboard was found (e.g.
 *          insufficient permissions). The caller may then fall back to
 *          SDL's focus-bound input.
 **********************************************************************/
input_evdev_t * input_evdev_create(uint32_t mod_mask);

/**********************************************************************
 * @brief  Drain pending key events into an input tick.
 *
 * Non-blocking. Zeroes *p_input, then sets its fields from any control
 * keys pressed this tick with the required modifiers held. Modifier
 * state is tracked across calls.
 *
 * @param[in]  p_ctx    Reader.
 * @param[out] p_input  Filled with this tick's edges.
 **********************************************************************/
void input_evdev_poll(input_evdev_t * p_ctx, screen_input_t * p_input);

/**********************************************************************
 * @brief  Close all devices and free the reader. Safe to pass NULL.
 **********************************************************************/
void input_evdev_destroy(input_evdev_t * p_ctx);

#ifdef __cplusplus
}
#endif

#endif /* INPUT_EVDEV_H */
