/*
Summary: What happens to a captured frame: seal a message to it, reveal
         the message sealed to it, or (watch mode) whichever the button
         asked for. Every action keeps the picture: the JPEG is saved,
         the outcome is drawn over a colour decode of it, and the
         composite is saved as a BMP and shown on the glasses.

         Encoding in watch mode is multi-shot: after the first frame,
         three more are taken over about two seconds while the wearer
         turns the object, and every view's fingerprint goes into the
         one record, so a later look from any of those angles matches.

         The message comes from --message, else from an on-glasses
         picker (short presses move, a hold confirms) fed by --messages
         or a built-in list, else from a terminal prompt.
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

#define ACTION_MAX_VIEWS    (4)

/*
Summary: Watch-mode capture. ACTION_MODE_DECODE looks the object up and
         shows the message, a LOCKED notice, or a NOT FOUND notice.
         ACTION_MODE_ENCODE collects views and seals the session's
         message to the object. Either way the scan animation plays.
Inputs:  p_s  - session (camera, display and link may be NULL offline).
         jpeg - the first frame.
         mode - ACTION_MODE_DECODE or ACTION_MODE_ENCODE.
Outputs: ACTION_OK, ACTION_LOCKED, ACTION_NO_MATCH, or ACTION_ERROR.
*/
int actions_capture(session_t * p_s, byte_span_t jpeg, int mode);

/*
Summary: One-shot seal of a single frame (the `seal` command).
Inputs:  p_s  - session.
         jpeg - the frame.
Outputs: ACTION_OK or ACTION_ERROR.
*/
int actions_seal_frame(session_t * p_s, byte_span_t jpeg);

/*
Summary: One-shot lookup of a single frame (the `reveal` command).
         Repeated calls for the same record are quiet.
Inputs:  p_s  - session.
         jpeg - the frame.
Outputs: ACTION_OK, ACTION_LOCKED, ACTION_NO_MATCH, or ACTION_ERROR.
*/
int actions_reveal_frame(session_t * p_s, byte_span_t jpeg);

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
