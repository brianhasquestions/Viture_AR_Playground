/*
Summary: Rasterise lines of ASCII text into an RGBA image for the
         glasses display. No font library: an embedded 8x8 bitmap font
         (the public-domain "font8x8_basic" by Daniel Hepper,
         https://github.com/dhepper/font8x8, ASCII 0x20-0x7F) is blitted
         into a heap buffer as white pixels on black. On the additive
         optical combiner black is transparent, so only the text glows.
*/

#ifndef HUD_H
#define HUD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/*
Summary: A block of text lines plus the integer pixel scale to draw at.
Inputs:  pp_lines   - array of NUL-terminated lines.
         line_count - number of entries in pp_lines.
         scale      - each font pixel becomes scale x scale pixels (>= 1).
Outputs: None.
*/
typedef struct
{
    const char * const * pp_lines;
    int                  line_count;
    int                  scale;
} hud_text_t;

#define HUD_WRAP_COLS       (40)
#define HUD_MAX_LINES       (24)

/*
Summary: Text word-wrapped into fixed-width lines, ready for hud_render.
Inputs:  None.
Outputs: lines      - storage for the wrapped text.
         pp_lines   - pointers into lines, for hud_text_t.
         line_count - number of lines used.
*/
typedef struct
{
    char         lines[HUD_MAX_LINES][HUD_WRAP_COLS + 1];
    const char * pp_lines[HUD_MAX_LINES];
    int          line_count;
} hud_wrapped_t;

/*
Summary: Word-wrap text to HUD_WRAP_COLS columns. Newlines in the text
         force a break; overlong words are cut at the column limit.
Inputs:  p_text - NUL-terminated text; NULL yields one empty line.
         p_out  - receives the wrapped lines.
Outputs: None.
*/
void hud_wrap_text(const char * p_text, hud_wrapped_t * p_out);

/*
Summary: Rasterise text into a newly allocated RGBA8 buffer.
Inputs:  p_text   - what to draw.
         p_width  - receives the image width in pixels.
         p_height - receives the image height in pixels.
Outputs: Heap buffer of width * height * 4 bytes, or NULL on failure.
         Caller frees with free().
*/
uint8_t * hud_render(const hud_text_t * p_text, uint32_t * p_width,
                     uint32_t * p_height);

#ifdef __cplusplus
}
#endif

#endif
