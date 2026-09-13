/*
Summary: What happens to a captured frame: seal a message to it, reveal
         the message sealed to it, or (watch mode) whichever applies.
         Every action keeps the picture: the JPEG is saved, the outcome
         is drawn over a colour decode of it, and the composite is saved
         as a BMP and shown on the glasses.
*/

#ifndef ACTIONS_H
#define ACTIONS_H

#include "bytes.h"
#include "session.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define ACTION_OK           (0)
#define ACTION_ERROR        (-1)
#define ACTION_NO_MATCH     (1)
#define ACTION_LOCKED       (2)

#define ACTION_MODE_DECODE  (1)
#define ACTION_MODE_ENCODE  (2)

/*
Summary: Seal the session's message (option or terminal prompt) to the
         frame's object fingerprint and the recipient key, then store
         it with the rotated/zoomed fingerprint variants.
Inputs:  p_s   - session.
         jpeg  - the frame.
         p_set - its fingerprint set from phash_set_from_jpeg().
Outputs: ACTION_OK or ACTION_ERROR.
*/
int actions_seal(session_t * p_s, byte_span_t jpeg,
                 const phash_set_t * p_set);

/*
Summary: Find the closest vault record and try to open it with the
         session identity. Repeated calls for the same record are quiet.
Inputs:  p_s   - session.
         jpeg  - the frame.
         p_set - its fingerprint set from phash_set_from_jpeg().
Outputs: ACTION_OK (opened), ACTION_LOCKED, ACTION_NO_MATCH, or
         ACTION_ERROR.
*/
int actions_reveal(session_t * p_s, byte_span_t jpeg,
                   const phash_set_t * p_set);

/*
Summary: Watch-mode capture. ACTION_MODE_DECODE looks the object up and
         shows the message, a LOCKED notice, or a NOT FOUND notice.
         ACTION_MODE_ENCODE seals the session's message to the object.
         Either way the scan animation plays first.
Inputs:  p_s  - session.
         jpeg - the frame.
         mode - ACTION_MODE_DECODE or ACTION_MODE_ENCODE.
Outputs: ACTION_OK, ACTION_LOCKED, ACTION_NO_MATCH, or ACTION_ERROR.
*/
int actions_capture(session_t * p_s, byte_span_t jpeg, int mode);

/*
Summary: Show a live frame with a hint caption on the glasses without
         saving anything.
Inputs:  p_s    - session (no-op without a display).
         jpeg   - the frame.
         p_hint - caption text.
Outputs: None.
*/
void actions_preview(session_t * p_s, byte_span_t jpeg, const char * p_hint);

#ifdef __cplusplus
}
#endif

#endif
