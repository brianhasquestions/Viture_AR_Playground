#include "locate.h"

#include "colorsig.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SEARCH_PERCENT      (LOCATE_SEARCH_PERCENT)
#define CENTRE_BIAS_PERCENT (60)
#define FALLBACK_PERCENT    (60)
#define PERCENT             (100)
#define EDGE_KEEP_PERMILLE  (25)
#define PERMILLE            (1000)
#define HIST_BINS           (1024)
#define CELL_PX             (16)
#define GROW_PERCENT        (70)
#define PAD_PERCENT         (150)
#define MIN_SIDE_PX         (160)
#define MIN_EDGE_PIXELS     (400)
#define NEIGHBOUR_COUNT     (8)
#define SAT_FLOOR_PERCENT   (20)
#define SPREAD_FACTOR       (2.4)
#define SAT_SAMPLES         (4)

static int luma(const gray_image_t * p_img, int x, int y)
{
    int value = p_img->p_gray[((size_t)y * (size_t)p_img->width) +
                              (size_t)x];

    return value;
}

static int sobel(const gray_image_t * p_img, int x, int y)
{
    int gx = (luma(p_img, x + 1, y - 1) + (2 * luma(p_img, x + 1, y)) +
              luma(p_img, x + 1, y + 1)) -
             (luma(p_img, x - 1, y - 1) + (2 * luma(p_img, x - 1, y)) +
              luma(p_img, x - 1, y + 1));
    int gy = (luma(p_img, x - 1, y + 1) + (2 * luma(p_img, x, y + 1)) +
              luma(p_img, x + 1, y + 1)) -
             (luma(p_img, x - 1, y - 1) + (2 * luma(p_img, x, y - 1)) +
              luma(p_img, x + 1, y - 1));

    return abs(gx) + abs(gy);
}

static void central_square(const gray_image_t * p_img, int percent,
                           region_t * p_out)
{
    int side = (p_img->width < p_img->height) ? p_img->width
                                              : p_img->height;

    side        = (side * percent) / PERCENT;
    p_out->x    = (p_img->width - side) / 2;
    p_out->y    = (p_img->height - side) / 2;
    p_out->side = side;
}

static int threshold_for(const int * p_mag, size_t count)
{
    long   hist[HIST_BINS];
    long   keep = ((long)count * EDGE_KEEP_PERMILLE) / PERMILLE;
    long   seen = 0;
    size_t i    = 0;
    int    bin  = HIST_BINS - 1;

    memset(hist, 0, sizeof(hist));
    for (i = 0; i < count; i++)
    {
        int m = p_mag[i];

        hist[(m >= HIST_BINS) ? (HIST_BINS - 1) : m]++;
    }
    while ((bin > 0) && (seen < keep))
    {
        seen += hist[bin];
        bin--;
    }

    return bin;
}

typedef struct
{
    long * p_density;
    uint8_t * p_visited;
    int    cells;
    long   total;
    long   max_density;
} cell_grid_t;

typedef struct
{
    const rgb_image_t * p_colour;
    const region_t *    p_search;
    int                 gray_width;
    int                 gray_height;
} colour_lookup_t;

static int cell_saturation(const colour_lookup_t * p_lk, int cx, int cy)
{
    int  sum = 0;
    int  sx  = 0;
    int  sy  = 0;

    for (sy = 0; sy < SAT_SAMPLES; sy++)
    {
        for (sx = 0; sx < SAT_SAMPLES; sx++)
        {
            int gx = p_lk->p_search->x + (cx * CELL_PX) +
                     ((CELL_PX * ((2 * sx) + 1)) / (2 * SAT_SAMPLES));
            int gy = p_lk->p_search->y + (cy * CELL_PX) +
                     ((CELL_PX * ((2 * sy) + 1)) / (2 * SAT_SAMPLES));
            int px = (gx * p_lk->p_colour->width) / p_lk->gray_width;
            int py = (gy * p_lk->p_colour->height) / p_lk->gray_height;

            sum += colorsig_saturation(
                p_lk->p_colour->p_rgb +
                ((((size_t)py * (size_t)p_lk->p_colour->width) +
                  (size_t)px) * (size_t)p_lk->p_colour->pixel_bytes));
        }
    }

    return sum / (SAT_SAMPLES * SAT_SAMPLES);
}

static void weight_by_saturation(cell_grid_t * p_grid,
                                 const colour_lookup_t * p_lk)
{
    int cx = 0;
    int cy = 0;

    for (cy = 0; cy < p_grid->cells; cy++)
    {
        for (cx = 0; cx < p_grid->cells; cx++)
        {
            long sat    = cell_saturation(p_lk, cx, cy);
            long weight = SAT_FLOOR_PERCENT +
                          (((PERCENT - SAT_FLOOR_PERCENT) * sat) / PERCENT);
            int  i      = (cy * p_grid->cells) + cx;

            p_grid->p_density[i] = (p_grid->p_density[i] * weight) / PERCENT;
        }
    }
}

static void count_cells(const int * p_mag, const region_t * p_search,
                        cell_grid_t * p_grid)
{
    int side   = p_search->side;
    int thresh = threshold_for(p_mag, (size_t)side * (size_t)side);
    int x      = 0;
    int y      = 0;

    for (y = 0; y < (p_grid->cells * CELL_PX); y++)
    {
        for (x = 0; x < (p_grid->cells * CELL_PX); x++)
        {
            if (p_mag[((size_t)y * (size_t)side) + (size_t)x] > thresh)
            {
                p_grid->p_density[((y / CELL_PX) * p_grid->cells) +
                                  (x / CELL_PX)]++;
                p_grid->total++;
            }
        }
    }
}

static long centre_weighted(const cell_grid_t * p_grid, int cell)
{
    long half   = p_grid->cells / 2;
    long dx     = (cell % p_grid->cells) - half;
    long dy     = (cell / p_grid->cells) - half;
    long dist2  = (dx * dx) + (dy * dy);
    long max2   = (2 * half * half) + 1;
    long weight = PERCENT - ((CENTRE_BIAS_PERCENT * dist2) / max2);

    return (p_grid->p_density[cell] * weight) / PERCENT;
}

static int busiest_cell(cell_grid_t * p_grid)
{
    int  count = p_grid->cells * p_grid->cells;
    int  i     = 0;
    int  best  = 0;
    long score = 0;
    long top   = -1;

    for (i = 0; i < count; i++)
    {
        score = centre_weighted(p_grid, i);
        if (score > top)
        {
            top  = score;
            best = i;
        }
    }
    p_grid->max_density = p_grid->p_density[best];

    return best;
}

typedef struct
{
    double sum_w;
    double sum_x;
    double sum_y;
    double sum_xx;
    double sum_yy;
} moments_t;

static void add_moment(moments_t * p_m, const cell_grid_t * p_grid,
                       int cell)
{
    double w = (double)p_grid->p_density[cell];
    double x = (double)(cell % p_grid->cells) + 0.5;
    double y = (double)(cell / p_grid->cells) + 0.5;

    p_m->sum_w  += w;
    p_m->sum_x  += w * x;
    p_m->sum_y  += w * y;
    p_m->sum_xx += w * x * x;
    p_m->sum_yy += w * y * y;
}

static void grow_cluster(cell_grid_t * p_grid, int seed, int bounds[4])
{
    static const int STEP[NEIGHBOUR_COUNT][2] =
    {
        { -1, -1 }, { 0, -1 }, { 1, -1 }, { -1, 0 },
        { 1, 0 },   { -1, 1 }, { 0, 1 },  { 1, 1 },
    };
    long      floor_density = (p_grid->max_density * GROW_PERCENT) / PERCENT;
    int       count         = p_grid->cells * p_grid->cells;
    int *     p_queue       = NULL;
    int       head          = 0;
    int       tail          = 0;
    double    mean_x        = 0.0;
    double    mean_y        = 0.0;
    double    spread        = 0.0;
    moments_t m;

    memset(&m, 0, sizeof(m));
    bounds[0] = seed % p_grid->cells;
    bounds[1] = seed / p_grid->cells;
    bounds[2] = bounds[0] + 1;
    bounds[3] = bounds[1] + 1;
    p_queue = (int *)malloc((size_t)count * sizeof(int));
    if (NULL == p_queue)
    {
        goto cleanup;
    }
    p_queue[tail++]         = seed;
    p_grid->p_visited[seed] = 1U;
    while (head < tail)
    {
        int cell = p_queue[head++];
        int cx   = cell % p_grid->cells;
        int cy   = cell / p_grid->cells;
        int n    = 0;

        add_moment(&m, p_grid, cell);
        for (n = 0; n < NEIGHBOUR_COUNT; n++)
        {
            int nx   = cx + STEP[n][0];
            int ny   = cy + STEP[n][1];
            int next = (ny * p_grid->cells) + nx;

            if ((nx < 0) || (ny < 0) || (nx >= p_grid->cells) ||
                (ny >= p_grid->cells) || (0U != p_grid->p_visited[next]) ||
                (p_grid->p_density[next] < floor_density))
            {
                continue;
            }
            p_grid->p_visited[next] = 1U;
            p_queue[tail++]         = next;
        }
    }
    if (m.sum_w > 0.0)
    {
        mean_x = m.sum_x / m.sum_w;
        mean_y = m.sum_y / m.sum_w;
        spread = sqrt(((m.sum_xx / m.sum_w) - (mean_x * mean_x)) +
                      ((m.sum_yy / m.sum_w) - (mean_y * mean_y)));
        spread = (spread * SPREAD_FACTOR) + 1.0;
        bounds[0] = (int)floor(mean_x - spread);
        bounds[1] = (int)floor(mean_y - spread);
        bounds[2] = (int)ceil(mean_x + spread);
        bounds[3] = (int)ceil(mean_y + spread);
    }

cleanup:
    if (NULL != p_queue)
    {
        free(p_queue);
    }
}

static void square_and_pad(const gray_image_t * p_img, const int bounds[4],
                           region_t * p_out)
{
    int w    = bounds[2] - bounds[0];
    int h    = bounds[3] - bounds[1];
    int side = ((w > h) ? w : h);
    int cx   = (bounds[0] + bounds[2]) / 2;
    int cy   = (bounds[1] + bounds[3]) / 2;
    int max  = (p_img->width < p_img->height) ? p_img->width
                                              : p_img->height;

    side = (side * PAD_PERCENT) / PERCENT;
    if (side < MIN_SIDE_PX)
    {
        side = MIN_SIDE_PX;
    }
    if (side > max)
    {
        side = max;
    }
    p_out->side = side;
    p_out->x    = cx - (side / 2);
    p_out->y    = cy - (side / 2);
    if (p_out->x < 0)
    {
        p_out->x = 0;
    }
    if (p_out->y < 0)
    {
        p_out->y = 0;
    }
    if ((p_out->x + side) > p_img->width)
    {
        p_out->x = p_img->width - side;
    }
    if ((p_out->y + side) > p_img->height)
    {
        p_out->y = p_img->height - side;
    }
}

static int locate_impl(const gray_image_t * p_img,
                       const rgb_image_t * p_colour, region_t * p_region)
{
    int             result = -1;
    int             x      = 0;
    int             y      = 0;
    int             seed   = 0;
    int *           p_mag  = NULL;
    region_t        search;
    cell_grid_t     grid;
    colour_lookup_t lookup;
    int             cell_bounds[4];
    int             bounds[4];

    memset(&grid, 0, sizeof(grid));
    if ((NULL == p_img) || (NULL == p_img->p_gray) || (NULL == p_region) ||
        (p_img->width < MIN_SIDE_PX) || (p_img->height < MIN_SIDE_PX))
    {
        goto cleanup;
    }
    central_square(p_img, FALLBACK_PERCENT, p_region);
    central_square(p_img, SEARCH_PERCENT, &search);
    grid.cells     = search.side / CELL_PX;
    p_mag          = (int *)calloc((size_t)search.side * (size_t)search.side,
                                   sizeof(int));
    grid.p_density = (long *)calloc((size_t)grid.cells * (size_t)grid.cells,
                                    sizeof(long));
    grid.p_visited = (uint8_t *)calloc((size_t)grid.cells *
                                       (size_t)grid.cells, 1U);
    if ((NULL == p_mag) || (NULL == grid.p_density) ||
        (NULL == grid.p_visited))
    {
        goto cleanup;
    }
    for (y = 1; y < (search.side - 1); y++)
    {
        for (x = 1; x < (search.side - 1); x++)
        {
            p_mag[((size_t)y * (size_t)search.side) + (size_t)x] =
                sobel(p_img, search.x + x, search.y + y);
        }
    }
    count_cells(p_mag, &search, &grid);
    result = 0;
    if (grid.total < MIN_EDGE_PIXELS)
    {
        goto cleanup;
    }
    if ((NULL != p_colour) && (NULL != p_colour->p_rgb))
    {
        lookup.p_colour    = p_colour;
        lookup.p_search    = &search;
        lookup.gray_width  = p_img->width;
        lookup.gray_height = p_img->height;
        weight_by_saturation(&grid, &lookup);
    }
    seed = busiest_cell(&grid);
    grow_cluster(&grid, seed, cell_bounds);
    bounds[0] = search.x + (cell_bounds[0] * CELL_PX);
    bounds[1] = search.y + (cell_bounds[1] * CELL_PX);
    bounds[2] = search.x + (cell_bounds[2] * CELL_PX);
    bounds[3] = search.y + (cell_bounds[3] * CELL_PX);
    square_and_pad(p_img, bounds, p_region);

cleanup:
    if (NULL != p_mag)
    {
        free(p_mag);
    }
    if (NULL != grid.p_density)
    {
        free(grid.p_density);
    }
    if (NULL != grid.p_visited)
    {
        free(grid.p_visited);
    }

    return result;
}

int locate_object(const gray_image_t * p_img, region_t * p_region)
{
    int result = locate_impl(p_img, NULL, p_region);

    return result;
}

int locate_object_colour(const gray_image_t * p_img,
                         const struct rgb_image * p_colour,
                         region_t * p_region)
{
    int result = locate_impl(p_img, p_colour, p_region);

    return result;
}
