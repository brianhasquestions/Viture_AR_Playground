/*
Summary: The visible "scanning" effect. When a capture is taken the
         object's outline is found with a Sobel edge detector inside
         the fingerprint area (the central 60% of the frame) and then
         traced onto the picture on the glasses: a bright scan line
         sweeps down the frame and every edge above it lights up. The
         same outline is kept, dimmer, on the final result picture so
         the wearer can see what was recognised.
*/

#ifndef SCAN_H
#define SCAN_H

#include "bytes.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define SCAN_DURATION_MS    (4000L)

/*
Summary: Precomputed scan state for one captured frame.
Inputs:  None; fill with scan_begin().
Outputs: base   - the decoded picture as RGBA.
         p_edge - width * height bytes, non-zero where an edge is.
         frame  - reusable output buffer for scan_render().
         box    - the scan area (x, y, side) in pixels.
*/
typedef struct
{
    rgba_image_t base;
    uint8_t *    p_edge;
    rgba_image_t frame;
    int          box[3];
} scan_t;

/*
Summary: Decode the frame, find the outline, and allocate the buffers.
Inputs:  jpeg   - the captured frame.
         p_scan - receives the state; release with scan_end().
Outputs: 0 on success, -1 if the frame does not decode or allocation
         fails.
*/
int scan_begin(byte_span_t jpeg, scan_t * p_scan);

/*
Summary: Render one animation frame into p_scan->frame.
Inputs:  p_scan   - state from scan_begin().
         progress - 0.0 (nothing traced) to 1.0 (outline complete).
Outputs: Pointer to p_scan->frame, valid until the next call or
         scan_end().
*/
const rgba_image_t * scan_render(scan_t * p_scan, double progress);

/*
Summary: Draw the finished outline (dim) and the scan box onto an
         image that shares the frame's dimensions, e.g. the result
         picture from picture_compose().
Inputs:  p_scan - state from scan_begin().
         p_img  - image to draw on; must match the frame size.
Outputs: None.
*/
void scan_mark_result(const scan_t * p_scan, rgba_image_t * p_img);

/*
Summary: Release everything in the state. Safe on a zeroed state.
Inputs:  p_scan - state to release.
Outputs: None.
*/
void scan_end(scan_t * p_scan);

#ifdef __cplusplus
}
#endif

#endif
