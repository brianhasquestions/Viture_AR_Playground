/*
Summary: Everything one run of the program carries around: parsed
         options, the derived identity, the open devices, and the small
         amount of state the live loops need. Passed as a single pointer
         so no function needs more than three parameters.
*/

#ifndef SESSION_H
#define SESSION_H

#include "display.h"
#include "glasses_link.h"
#include "keyring.h"
#include "phash.h"
#include "scan.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/*
Summary: Command-line options.
Inputs:  None.
Outputs: None.
*/
typedef struct
{
    const char * p_cmd;
    const char * p_vault;
    const char * p_captures;
    const char * p_image;
    const char * p_hash;
    const char * p_message;
    const char * p_recipient;
    const char * p_pin;
    const char * p_maxdist;
    int          max_dist;
    int          want_display;
    int          windowed;
    int          no_preview;
} options_t;

/*
Summary: Run state.
Inputs:  None.
Outputs: None.
*/
typedef struct
{
    options_t          opts;
    uint8_t            sn_hash[GLASSES_LINK_ID_BYTES];
    keyring_identity_t identity;
    uint8_t            recipient_pub[KEYRING_KEY_BYTES];
    uint8_t            last_shown[PHASH_BYTES];
    int                have_shown;
    int                frames_seen;
    int                sealed;
    int                offline;
    long               hold_until_ms;
    glasses_link_t *   p_link;
    display_ctx_t *    p_display;
    const scan_t *     p_scan;
} session_t;

#ifdef __cplusplus
}
#endif

#endif
