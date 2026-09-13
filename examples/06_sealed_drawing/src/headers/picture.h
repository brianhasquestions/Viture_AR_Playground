/*
Summary: Keep and annotate the pictures the camera takes. A capture is
         saved as the original colour JPEG, and after the message is
         known the same frame is decoded, a caption band is drawn over
         its lower edge, and the composite is saved as a BMP and shown
         on the glasses.
*/

#ifndef PICTURE_H
#define PICTURE_H

#include "bytes.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define PICTURE_STAMP_CHARS     (16)
#define PICTURE_PATH_CHARS      (512)

/*
Summary: Where a capture and its overlay go.
Inputs:  p_dir - directory, created if missing.
         stamp - timestamp text shared by the JPEG and the BMP.
Outputs: None.
*/
typedef struct
{
    const char * p_dir;
    char         stamp[PICTURE_STAMP_CHARS];
} picture_slot_t;

/*
Summary: Start a new capture slot: make the directory and stamp it with
         the current local time (YYYYmmdd-HHMMSS).
Inputs:  p_dir  - capture directory.
         p_slot - receives the slot.
Outputs: 0 on success, -1 if the directory cannot be created.
*/
int picture_slot_begin(const char * p_dir, picture_slot_t * p_slot);

/*
Summary: Save the original JPEG frame into the slot.
Inputs:  p_slot - slot from picture_slot_begin().
         jpeg   - the frame.
         p_path - receives the written path (PICTURE_PATH_CHARS).
Outputs: 0 on success, -1 on I/O failure.
*/
int picture_save_jpeg(const picture_slot_t * p_slot, byte_span_t jpeg,
                      char * p_path);

/*
Summary: Decode the frame to colour and draw a caption band across its
         bottom edge.
Inputs:  jpeg      - the frame.
         p_caption - text to overlay; word-wrapped.
         p_out     - receives the composite RGBA image.
Outputs: 0 on success, -1 if the frame does not decode. Release
         p_out->p_rgba with free().
*/
int picture_compose(byte_span_t jpeg, const char * p_caption,
                    rgba_image_t * p_out);

/*
Summary: Quarter-resolution version of picture_compose() for the live
         view: decodes only the low frequencies of each block, so it
         runs several times faster and keeps the preview smooth.
Inputs:  jpeg      - the frame.
         p_caption - text to overlay.
         p_out     - receives the composite (width/4 x height/4).
Outputs: 0 on success, -1 if the frame does not decode.
*/
int picture_preview(byte_span_t jpeg, const char * p_caption,
                    rgba_image_t * p_out);

/*
Summary: Save a composite into the slot as a 24-bit BMP.
Inputs:  p_slot - slot from picture_slot_begin().
         p_img  - composite from picture_compose().
         p_path - receives the written path (PICTURE_PATH_CHARS).
Outputs: 0 on success, -1 on I/O failure.
*/
int picture_save_bmp(const picture_slot_t * p_slot,
                     const rgba_image_t * p_img, char * p_path);

#ifdef __cplusplus
}
#endif

#endif
