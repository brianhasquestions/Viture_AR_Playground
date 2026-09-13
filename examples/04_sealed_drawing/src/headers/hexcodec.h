/*
Summary: Lowercase hexadecimal encoding and decoding of byte ranges.
*/

#ifndef HEXCODEC_H
#define HEXCODEC_H

#include "bytes.h"

#ifdef __cplusplus
extern "C"
{
#endif

/*
Summary: Encode a byte range as a NUL-terminated lowercase hex string.
Inputs:  in      - bytes to encode.
         p_out   - receives the text.
         out_cap - capacity of p_out; must be at least (2 * in.len) + 1.
Outputs: 0 on success, -1 if p_out is too small or an argument is NULL.
*/
int hex_encode(byte_span_t in, char * p_out, size_t out_cap);

/*
Summary: Decode a NUL-terminated hex string into a byte buffer.
Inputs:  p_hex - even-length hex text, either case.
         p_out - buffer whose cap bounds the decode.
Outputs: 0 on success with p_out->len set, -1 on bad text, odd length,
         or insufficient capacity.
*/
int hex_decode(const char * p_hex, byte_buf_t * p_out);

#ifdef __cplusplus
}
#endif

#endif
