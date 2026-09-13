/*
Summary: Find the painted object in a frame. Painted or drawn things
         are busy with edges; skin, walls and desks are not. A Sobel
         edge map of the central region is thresholded and counted
         into a coarse grid of cells; the busiest cell, weighted toward
         the centre of view where the wearer is looking, seeds a cluster
         that grows across neighbouring cells that are at least 70% as
         busy, and a square is placed on the cluster's edge-weighted
         centroid with a side from its spread (far steadier than the
         cluster's extreme cells).
         Thin outlines (a hand, a cable) never reach that density, so
         they stay outside the box. The fingerprint and the
         scan animation both use this square, so the match no longer
         depends on where in the view the object is or how far away.
*/

#ifndef LOCATE_H
#define LOCATE_H

#include "bytes.h"

#define LOCATE_SEARCH_PERCENT   (70)

#ifdef __cplusplus
extern "C"
{
#endif

/*
Summary: An axis-aligned square region of an image.
Inputs:  None.
Outputs: x, y - top-left corner in pixels.
         side - width and height in pixels.
*/
typedef struct region
{
    int x;
    int y;
    int side;
} region_t;

struct rgb_image;

/*
Summary: Locate the busiest COLOURFUL object. Same as locate_object()
         but each grid cell's edge count is weighted by the cell's mean
         saturation, so monitor text, cables and wood grain lose to
         paint. The colour image may be any resolution (typically the
         quarter-size decode); it is sampled proportionally.
Inputs:  p_img    - grayscale image (full resolution).
         p_colour - RGB or RGBA image of the same scene.
         p_region - receives the square around the object.
Outputs: 0 on success, -1 on bad input or allocation failure.
*/
int locate_object_colour(const gray_image_t * p_img,
                         const struct rgb_image * p_colour,
                         region_t * p_region);

/*
Summary: Locate the busiest object in the central part of the image.
Inputs:  p_img    - grayscale image.
         p_region - receives the square around the object. Falls back
                    to the central 60% square if too few edges exist.
Outputs: 0 on success, -1 on bad input or allocation failure.
*/
int locate_object(const gray_image_t * p_img, region_t * p_region);

#ifdef __cplusplus
}
#endif

#endif
