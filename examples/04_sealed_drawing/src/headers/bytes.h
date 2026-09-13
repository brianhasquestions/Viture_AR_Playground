/*
Summary: Byte-range value types shared by every module in this example.
         Passing one of these instead of a pointer/length pair keeps
         function signatures at three parameters or fewer.
*/

#ifndef BYTES_H
#define BYTES_H

#include <stddef.h>
#include <stdint.h>

/*
Summary: Read-only view of len bytes owned by someone else.
Inputs:  p_data - first byte of the range.
         len    - number of bytes in the range.
Outputs: None.
*/
typedef struct
{
    const uint8_t * p_data;
    size_t          len;
} byte_span_t;

/*
Summary: Writable buffer with a capacity and a fill level.
Inputs:  p_data - storage for up to cap bytes.
         cap    - capacity of p_data in bytes.
         len    - number of bytes currently valid.
Outputs: None.
*/
typedef struct
{
    uint8_t * p_data;
    size_t    cap;
    size_t    len;
} byte_buf_t;

/*
Summary: Grayscale image view.
Inputs:  p_gray - width * height bytes, row-major.
         width  - columns.
         height - rows.
Outputs: None.
*/
typedef struct
{
    const uint8_t * p_gray;
    int             width;
    int             height;
} gray_image_t;

/*
Summary: An RGBA8 image on the heap.
Inputs:  p_rgba - width * height * 4 bytes, row-major.
         width  - columns.
         height - rows.
Outputs: None. Release p_rgba with free().
*/
typedef struct
{
    uint8_t * p_rgba;
    uint32_t  width;
    uint32_t  height;
} rgba_image_t;

#endif
