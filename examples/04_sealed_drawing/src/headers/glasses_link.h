/*
Summary: The USB link to the glasses: hardware identity plus the
         physical buttons. The SDK never exposes the raw serial, only
         its SHA-256, and it never exposes button presses directly: a
         press on the glasses shows up as a state-change callback
         (brightness, volume, electrochromic film). This module keeps
         the device provider open, reads the serial hash once, and turns
         a HOLD of the volume rocker into a single trigger that knows
         which way it was pushed. The firmware auto-repeats the volume
         report while the rocker is held, so a run of reports with no
         gap over 0.7 s that lasts at least 1.2 s is a hold, and whether
         the level climbs or falls says UP or DOWN. The volume is parked
         at a mid level when the link opens and after every trigger so
         both directions are always readable, and the original level is
         put back on close. A long press on the other rocker is the
         firmware's 2D/3D (or lens film) toggle; it is not a trigger, but
         the state is put back by glasses_link_restore().
*/

#ifndef GLASSES_LINK_H
#define GLASSES_LINK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define GLASSES_LINK_ID_BYTES       (32)
#define GLASSES_LINK_NONE           (0)
#define GLASSES_LINK_UP             (1)
#define GLASSES_LINK_DOWN           (2)

typedef struct glasses_link glasses_link_t;

/*
Summary: Open the device provider and read the hardware identity.
Inputs:  glasses_pid    - USB product ID located by device_scan.
         listen_buttons - non-zero to also start the provider and count
                          button presses; zero for identity only.
Outputs: Link, or NULL on any SDK failure. Release with
         glasses_link_close(). Only one link may listen for buttons at
         a time (the SDK callback carries no user pointer).
*/
glasses_link_t * glasses_link_open(int glasses_pid, int listen_buttons);

/*
Summary: The SHA-256 serial hash read when the link was opened.
Inputs:  p_link - open link.
Outputs: Pointer to GLASSES_LINK_ID_BYTES bytes owned by the link.
*/
const uint8_t * glasses_link_sn_hash(const glasses_link_t * p_link);

/*
Summary: Check whether a hold completed since the last call.
Inputs:  p_link - open link.
Outputs: GLASSES_LINK_UP or GLASSES_LINK_DOWN once per hold,
         GLASSES_LINK_NONE otherwise. Reading it clears it.
*/
int glasses_link_take_trigger(glasses_link_t * p_link);

/*
Summary: Undo whatever a hold toggled (display mode, lens film) and
         park the volume at the mid level again. Call from the main loop
         after a trigger, never from a callback.
Inputs:  p_link - open link.
Outputs: None.
*/
void glasses_link_restore(glasses_link_t * p_link);

/*
Summary: Stop, shut down and destroy the provider. Safe on NULL.
Inputs:  p_link - link to close.
Outputs: None.
*/
void glasses_link_close(glasses_link_t * p_link);

#ifdef __cplusplus
}
#endif

#endif
