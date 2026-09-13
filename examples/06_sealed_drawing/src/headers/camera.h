/*
Summary: Live MJPEG frames from the glasses' onboard camera.
         The SDK delivers frames on its own thread; this module keeps
         only the newest one under a mutex and hands the caller a copy
         on request, so decoding never blocks the capture thread.
         The kernel uvcvideo driver is asked to release the camera first
         (see usb_detach.h); the SDK talks UVC over libusb itself.
*/

#ifndef CAMERA_H
#define CAMERA_H

#include "bytes.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct camera_ctx camera_ctx_t;

/*
Summary: Open and start the camera that belongs to the given glasses.
Inputs:  glasses_pid - USB product ID located by device_scan.
Outputs: Running capture context, or NULL if the model has no camera or
         the SDK could not start it. Release with camera_close().
*/
camera_ctx_t * camera_open(int glasses_pid);

/*
Summary: Copy the newest frame out, if one arrived since the last call.
Inputs:  p_ctx   - capture context.
         p_frame - heap buffer, grown with realloc as needed. Start with
                   all fields zero; free p_frame->p_data when done.
Outputs: 1 if p_frame now holds a new JPEG, 0 if nothing new arrived,
         -1 on allocation failure or bad argument.
*/
int camera_take_frame(camera_ctx_t * p_ctx, byte_buf_t * p_frame);

/*
Summary: Stop capture and free the context. Safe on NULL.
Inputs:  p_ctx - context from camera_open().
Outputs: None.
*/
void camera_close(camera_ctx_t * p_ctx);

#ifdef __cplusplus
}
#endif

#endif
