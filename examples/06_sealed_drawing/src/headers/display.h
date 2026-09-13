/*
Summary: Put a title and a word-wrapped message on the glasses screen.
         A fullscreen SDL2 window is opened on the VITURE display (the
         glasses enumerate as an ordinary monitor) and the text image
         from hud_render() is copied to it with the 2D renderer.
*/

#ifndef DISPLAY_H
#define DISPLAY_H

#include "bytes.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct display_ctx display_ctx_t;

/*
Summary: Open a fullscreen window on the glasses, or a desktop preview.
Inputs:  windowed - non-zero for a normal desktop window instead of
                    fullscreen on the VITURE display.
Outputs: Context, or NULL on failure. Release with display_destroy().
*/
display_ctx_t * display_create(int windowed);

/*
Summary: Replace what is shown with a title line and a wrapped body.
Inputs:  p_ctx   - display context.
         p_title - first line; NULL for none.
         p_body  - word-wrapped text; NULL to show only the title.
Outputs: None.
*/
void display_show_message(display_ctx_t * p_ctx, const char * p_title,
                          const char * p_body);

/*
Summary: Replace what is shown with a full RGBA image (a captured
         picture with its caption). Scaled to fit the screen.
Inputs:  p_ctx - display context.
         p_img - image to show; copied into a texture.
Outputs: None.
*/
void display_show_image(display_ctx_t * p_ctx, const rgba_image_t * p_img);

/*
Summary: Process window events and repaint.
Inputs:  p_ctx - display context.
Outputs: 1 if the user asked to quit (Esc or window close), else 0.
*/
int display_pump(display_ctx_t * p_ctx);

/*
Summary: Destroy the window and release resources. Safe on NULL.
Inputs:  p_ctx - display context.
Outputs: None.
*/
void display_destroy(display_ctx_t * p_ctx);

#ifdef __cplusplus
}
#endif

#endif
