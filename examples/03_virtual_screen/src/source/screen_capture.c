/**********************************************************************
 * @file    screen_capture.c
 * @brief   Continuous capture of a Wayland output via wlr-screencopy.
 * @copyright 2026 XR_Playground. Educational sample code.
 *
 * Implementation notes:
 *  - Flow per frame:
 *      zwlr_screencopy_manager_v1.capture_output()
 *        -> frame.buffer(format, w, h, stride)   (we allocate shm)
 *        -> frame.buffer_done()                  (we call frame.copy)
 *        -> frame.flags() / frame.ready()        (pixels are valid)
 *    A new capture is requested as soon as the previous one is ready,
 *    which keeps frames flowing without ever blocking the renderer.
 *  - The shm buffer is reallocated only when the output geometry
 *    changes, so the steady state performs no allocation at all.
 *  - Outputs are matched by the wl_output "name" event, which requires
 *    wl_output version 4.
 *
 * Coding standard: Barr-C:2018.
 **********************************************************************/

#include "screen_capture.h"

#include "wlr-screencopy-unstable-v1-client-protocol.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

/* wl_output version 4 is the first that reports the connector name
 * ("eDP-1", "DP-1"), which is how we identify the right display. */
#define WL_OUTPUT_BIND_VERSION      (4)

/* Substring identifying the glasses, so we never capture them and
 * create a feedback loop. */
#define GLASSES_OUTPUT_TAG          "VITURE"

/* Maximum outputs we track. */
#define MAX_OUTPUTS                 (8)

/**********************************************************************
 * @brief  One Wayland output (a physical display).
 **********************************************************************/
typedef struct
{
    struct wl_output * p_output;
    char *             p_name;        /* Heap copy, e.g. "eDP-1". */
    char *             p_description; /* Heap copy; may contain "VITURE". */
} output_info_t;

/**********************************************************************
 * @brief  Capture session state.
 **********************************************************************/
struct screen_capture_ctx
{
    struct wl_display *  p_display;
    struct wl_registry * p_registry;
    struct wl_shm *      p_shm;
    struct zwlr_screencopy_manager_v1 * p_screencopy;

    output_info_t        outputs[MAX_OUTPUTS];
    int                  output_count;
    struct wl_output *   p_target;      /* Output being captured. */
    char *               p_target_name; /* Heap copy of its name.  */

    /* Frame currently in flight. */
    struct zwlr_screencopy_frame_v1 * p_frame;
    int                  capture_pending;
    int                  frame_ready;    /* Set by the ready event. */
    int                  frame_failed;

    /* Shared-memory buffer backing the capture. */
    struct wl_buffer *   p_buffer;
    void *               p_data;      /* mmap'd pixels. */
    size_t               data_size;
    uint32_t             width;
    uint32_t             height;
    uint32_t             stride;
    uint32_t             shm_format;
    screen_pixel_format_t format;
    int                  y_invert;
    int                  have_frame;  /* At least one frame captured. */
};

/**********************************************************************
 * @brief  Map a Wayland shm format to our pixel layout.
 **********************************************************************/
static screen_pixel_format_t map_format(uint32_t shm_format)
{
    screen_pixel_format_t format = SCREEN_PIXEL_UNKNOWN;

    /* Wayland names these by 32-bit word layout; in little-endian
     * memory XRGB8888 lands as bytes B,G,R,X. */
    if ((WL_SHM_FORMAT_XRGB8888 == shm_format) ||
        (WL_SHM_FORMAT_ARGB8888 == shm_format))
    {
        format = SCREEN_PIXEL_BGRA;
    }
    else if ((WL_SHM_FORMAT_XBGR8888 == shm_format) ||
             (WL_SHM_FORMAT_ABGR8888 == shm_format))
    {
        format = SCREEN_PIXEL_RGBA;
    }
    else
    {
        format = SCREEN_PIXEL_UNKNOWN;
    }

    return format;
}

/**********************************************************************
 * @brief  Create an anonymous shared-memory file of the given size.
 *
 * @return  File descriptor, or -1 on failure.
 **********************************************************************/
static int create_shm_file(size_t size)
{
    int fd     = -1;
    int result = -1;

    fd = memfd_create("xr_screen_capture", MFD_CLOEXEC);
    if (0 > fd)
    {
        goto cleanup;
    }

    if (0 != ftruncate(fd, (off_t)size))
    {
        (void)close(fd);
        fd = -1;
        goto cleanup;
    }

    result = fd;

cleanup:
    return result;
}

/**********************************************************************
 * @brief  Release the current shm buffer and its mapping.
 **********************************************************************/
static void release_buffer(screen_capture_ctx_t * p_ctx)
{
    if (NULL == p_ctx)
    {
        goto cleanup;
    }

    if (NULL != p_ctx->p_buffer)
    {
        wl_buffer_destroy(p_ctx->p_buffer);
        p_ctx->p_buffer = NULL;
    }
    if (NULL != p_ctx->p_data)
    {
        (void)munmap(p_ctx->p_data, p_ctx->data_size);
        p_ctx->p_data = NULL;
    }
    p_ctx->data_size = 0;

cleanup:
    return;
}

/**********************************************************************
 * @brief  (Re)allocate the shm buffer to match the frame geometry.
 *
 * Only reallocates when the geometry actually changes, so the steady
 * state performs no allocation.
 *
 * @return  0 on success, -1 on failure.
 **********************************************************************/
static int ensure_buffer(screen_capture_ctx_t * p_ctx, uint32_t width,
                         uint32_t height, uint32_t stride,
                         uint32_t shm_format)
{
    int                 result  = -1;
    int                 fd      = -1;
    size_t              size    = 0;
    struct wl_shm_pool * p_pool = NULL;

    if (NULL == p_ctx)
    {
        goto cleanup;
    }

    size = (size_t)stride * (size_t)height;
    if (0U == size)
    {
        goto cleanup;
    }

    /* wl_shm_create_pool and wl_shm_pool_create_buffer take int32_t. The
     * geometry below arrives from the compositor, so nothing here has
     * bounded it yet: a stride*height above INT32_MAX would wrap to a
     * negative pool size on the cast. Reject it rather than narrow it. */
    if ((size > (size_t)INT32_MAX) ||
        (width > (uint32_t)INT32_MAX) ||
        (height > (uint32_t)INT32_MAX) ||
        (stride > (uint32_t)INT32_MAX))
    {
        (void)fprintf(stderr,
                      "[capture] refusing implausible geometry "
                      "%ux%u stride %u\n", width, height, stride);
        goto cleanup;
    }

    /* Same geometry as last time: reuse the existing buffer. */
    if ((NULL != p_ctx->p_buffer) && (width == p_ctx->width) &&
        (height == p_ctx->height) && (stride == p_ctx->stride) &&
        (shm_format == p_ctx->shm_format))
    {
        result = 0;
        goto cleanup;
    }

    release_buffer(p_ctx);

    fd = create_shm_file(size);
    if (0 > fd)
    {
        (void)fprintf(stderr, "[capture] shm alloc failed: %s\n",
                      strerror(errno));
        goto cleanup;
    }

    p_ctx->p_data = mmap(NULL, size, PROT_READ | PROT_WRITE,
                         MAP_SHARED, fd, 0);
    if (MAP_FAILED == p_ctx->p_data)
    {
        p_ctx->p_data = NULL;
        goto cleanup;
    }
    p_ctx->data_size = size;

    p_pool = wl_shm_create_pool(p_ctx->p_shm, fd, (int32_t)size);
    if (NULL == p_pool)
    {
        goto cleanup;
    }

    p_ctx->p_buffer = wl_shm_pool_create_buffer(p_pool, 0,
                                                (int32_t)width,
                                                (int32_t)height,
                                                (int32_t)stride,
                                                shm_format);
    wl_shm_pool_destroy(p_pool);

    if (NULL == p_ctx->p_buffer)
    {
        goto cleanup;
    }

    p_ctx->width      = width;
    p_ctx->height     = height;
    p_ctx->stride     = stride;
    p_ctx->shm_format = shm_format;
    p_ctx->format     = map_format(shm_format);

    result = 0;

cleanup:
    if (0 <= fd)
    {
        /* The pool holds its own reference to the fd. */
        (void)close(fd);
    }
    return result;
}

/* --- zwlr_screencopy_frame_v1 listener ----------------------------- */

static void on_frame_buffer(void * p_user,
                            struct zwlr_screencopy_frame_v1 * p_frame,
                            uint32_t shm_format, uint32_t width,
                            uint32_t height, uint32_t stride)
{
    screen_capture_ctx_t * p_ctx = (screen_capture_ctx_t *)p_user;

    if (NULL == p_ctx)
    {
        goto cleanup;
    }

    if (SCREEN_PIXEL_UNKNOWN == map_format(shm_format))
    {
        /* Some other format was offered; wait for one we understand. */
        goto cleanup;
    }

    if (0 != ensure_buffer(p_ctx, width, height, stride, shm_format))
    {
        p_ctx->frame_failed = 1;
        goto cleanup;
    }

    /* Version 3 sends buffer_done; older versions expect copy() here.
     * We request version 3, so the copy is issued from buffer_done. */
    (void)p_frame;

cleanup:
    return;
}

static void on_frame_buffer_done(void * p_user,
                                 struct zwlr_screencopy_frame_v1 * p_frame)
{
    screen_capture_ctx_t * p_ctx = (screen_capture_ctx_t *)p_user;

    if ((NULL == p_ctx) || (NULL == p_ctx->p_buffer))
    {
        goto cleanup;
    }

    zwlr_screencopy_frame_v1_copy(p_frame, p_ctx->p_buffer);

cleanup:
    return;
}

static void on_frame_flags(void * p_user,
                           struct zwlr_screencopy_frame_v1 * p_frame,
                           uint32_t flags)
{
    screen_capture_ctx_t * p_ctx = (screen_capture_ctx_t *)p_user;

    (void)p_frame;

    if (NULL == p_ctx)
    {
        goto cleanup;
    }

    /* The compositor may hand us the image bottom-up. */
    p_ctx->y_invert =
        (0U != (flags & ZWLR_SCREENCOPY_FRAME_V1_FLAGS_Y_INVERT)) ? 1 : 0;

cleanup:
    return;
}

static void on_frame_ready(void * p_user,
                           struct zwlr_screencopy_frame_v1 * p_frame,
                           uint32_t sec_hi, uint32_t sec_lo,
                           uint32_t nsec)
{
    screen_capture_ctx_t * p_ctx = (screen_capture_ctx_t *)p_user;

    (void)p_frame;
    (void)sec_hi;
    (void)sec_lo;
    (void)nsec;

    if (NULL == p_ctx)
    {
        goto cleanup;
    }

    p_ctx->frame_ready = 1;
    p_ctx->have_frame  = 1;

cleanup:
    return;
}

static void on_frame_failed(void * p_user,
                            struct zwlr_screencopy_frame_v1 * p_frame)
{
    screen_capture_ctx_t * p_ctx = (screen_capture_ctx_t *)p_user;

    (void)p_frame;

    if (NULL == p_ctx)
    {
        goto cleanup;
    }

    p_ctx->frame_failed = 1;

cleanup:
    return;
}

static void on_frame_damage(void * p_user,
                            struct zwlr_screencopy_frame_v1 * p_frame,
                            uint32_t x, uint32_t y, uint32_t width,
                            uint32_t height)
{
    /* We always re-upload the whole texture, so damage is ignored. */
    (void)p_user;
    (void)p_frame;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
}

static void on_frame_linux_dmabuf(void * p_user,
                                  struct zwlr_screencopy_frame_v1 * p_frame,
                                  uint32_t format, uint32_t width,
                                  uint32_t height)
{
    /* This sample takes the shm path only. */
    (void)p_user;
    (void)p_frame;
    (void)format;
    (void)width;
    (void)height;
}

static const struct zwlr_screencopy_frame_v1_listener g_frame_listener =
{
    .buffer       = on_frame_buffer,
    .flags        = on_frame_flags,
    .ready        = on_frame_ready,
    .failed       = on_frame_failed,
    .damage       = on_frame_damage,
    .linux_dmabuf = on_frame_linux_dmabuf,
    .buffer_done  = on_frame_buffer_done
};

/* --- wl_output listener (we only care about the name) -------------- */

static void on_output_geometry(void * p_user, struct wl_output * p_output,
                               int32_t x, int32_t y, int32_t phys_w,
                               int32_t phys_h, int32_t subpixel,
                               const char * p_make, const char * p_model,
                               int32_t transform)
{
    (void)p_user; (void)p_output; (void)x; (void)y; (void)phys_w;
    (void)phys_h; (void)subpixel; (void)p_make; (void)p_model;
    (void)transform;
}

static void on_output_mode(void * p_user, struct wl_output * p_output,
                           uint32_t flags, int32_t width, int32_t height,
                           int32_t refresh)
{
    (void)p_user; (void)p_output; (void)flags; (void)width;
    (void)height; (void)refresh;
}

static void on_output_done(void * p_user, struct wl_output * p_output)
{
    (void)p_user; (void)p_output;
}

static void on_output_scale(void * p_user, struct wl_output * p_output,
                            int32_t factor)
{
    (void)p_user; (void)p_output; (void)factor;
}

static void on_output_name(void * p_user, struct wl_output * p_output,
                           const char * p_name)
{
    screen_capture_ctx_t * p_ctx = (screen_capture_ctx_t *)p_user;
    int                    i     = 0;

    if ((NULL == p_ctx) || (NULL == p_name))
    {
        goto cleanup;
    }

    for (i = 0; i < p_ctx->output_count; i++)
    {
        if (p_ctx->outputs[i].p_output == p_output)
        {
            char * p_copy = strdup(p_name);

            /* On failure keep the old name rather than dropping to NULL:
             * every consumer treats a NULL name as "output has no name
             * yet" and skips the output entirely. */
            if (NULL != p_copy)
            {
                free(p_ctx->outputs[i].p_name);
                p_ctx->outputs[i].p_name = p_copy;
            }
            break;
        }
    }

cleanup:
    return;
}

static void on_output_description(void * p_user,
                                  struct wl_output * p_output,
                                  const char * p_description)
{
    screen_capture_ctx_t * p_ctx = (screen_capture_ctx_t *)p_user;
    int                    i     = 0;

    if ((NULL == p_ctx) || (NULL == p_description))
    {
        goto cleanup;
    }

    for (i = 0; i < p_ctx->output_count; i++)
    {
        if (p_ctx->outputs[i].p_output == p_output)
        {
            char * p_copy = strdup(p_description);

            if (NULL != p_copy)
            {
                free(p_ctx->outputs[i].p_description);
                p_ctx->outputs[i].p_description = p_copy;
            }
            break;
        }
    }

cleanup:
    return;
}

static const struct wl_output_listener g_output_listener =
{
    .geometry    = on_output_geometry,
    .mode        = on_output_mode,
    .done        = on_output_done,
    .scale       = on_output_scale,
    .name        = on_output_name,
    .description = on_output_description
};

/* --- registry ------------------------------------------------------ */

static void on_registry_global(void * p_user,
                               struct wl_registry * p_registry,
                               uint32_t name, const char * p_interface,
                               uint32_t version)
{
    screen_capture_ctx_t * p_ctx = (screen_capture_ctx_t *)p_user;

    if ((NULL == p_ctx) || (NULL == p_interface))
    {
        goto cleanup;
    }

    if (0 == strcmp(p_interface, wl_shm_interface.name))
    {
        p_ctx->p_shm = (struct wl_shm *)wl_registry_bind(
            p_registry, name, &wl_shm_interface, 1U);
    }
    else if (0 == strcmp(p_interface,
                         zwlr_screencopy_manager_v1_interface.name))
    {
        uint32_t bind_version = (version < 3U) ? version : 3U;

        p_ctx->p_screencopy =
            (struct zwlr_screencopy_manager_v1 *)wl_registry_bind(
                p_registry, name,
                &zwlr_screencopy_manager_v1_interface, bind_version);
    }
    else if (0 == strcmp(p_interface, wl_output_interface.name))
    {
        if (p_ctx->output_count < MAX_OUTPUTS)
        {
            uint32_t bind_version =
                (version < (uint32_t)WL_OUTPUT_BIND_VERSION)
                    ? version : (uint32_t)WL_OUTPUT_BIND_VERSION;
            struct wl_output * p_output =
                (struct wl_output *)wl_registry_bind(
                    p_registry, name, &wl_output_interface,
                    bind_version);

            if (NULL != p_output)
            {
                p_ctx->outputs[p_ctx->output_count].p_output = p_output;
                p_ctx->output_count++;
                (void)wl_output_add_listener(p_output,
                                             &g_output_listener, p_ctx);
            }
        }
    }
    else
    {
        /* Interface not of interest. */
    }

cleanup:
    return;
}

static void on_registry_global_remove(void * p_user,
                                      struct wl_registry * p_registry,
                                      uint32_t name)
{
    (void)p_user;
    (void)p_registry;
    (void)name;
}

static const struct wl_registry_listener g_registry_listener =
{
    .global        = on_registry_global,
    .global_remove = on_registry_global_remove
};

/**********************************************************************
 * @brief  Ask the compositor for the next frame of the target output.
 **********************************************************************/
static void request_frame(screen_capture_ctx_t * p_ctx)
{
    if ((NULL == p_ctx) || (0 != p_ctx->capture_pending))
    {
        goto cleanup;
    }

    p_ctx->frame_ready  = 0;
    p_ctx->frame_failed = 0;

    /* overlay_cursor = 1 so the pointer shows up on the virtual screen. */
    p_ctx->p_frame = zwlr_screencopy_manager_v1_capture_output(
        p_ctx->p_screencopy, 1, p_ctx->p_target);

    if (NULL == p_ctx->p_frame)
    {
        goto cleanup;
    }

    (void)zwlr_screencopy_frame_v1_add_listener(p_ctx->p_frame,
                                                &g_frame_listener,
                                                p_ctx);
    p_ctx->capture_pending = 1;

cleanup:
    return;
}

/**********************************************************************
 * @brief  Connect to the compositor and begin capturing an output.
 **********************************************************************/
screen_capture_ctx_t * screen_capture_create(const char * p_output_name)
{
    screen_capture_ctx_t * p_ctx    = NULL;
    screen_capture_ctx_t * p_result = NULL;
    int                    i        = 0;

    p_ctx = (screen_capture_ctx_t *)calloc(
                1U, sizeof(screen_capture_ctx_t));
    if (NULL == p_ctx)
    {
        goto cleanup;
    }

    p_ctx->p_display = wl_display_connect(NULL);
    if (NULL == p_ctx->p_display)
    {
        (void)fprintf(stderr,
                      "[capture] cannot connect to a Wayland display\n");
        goto cleanup;
    }

    p_ctx->p_registry = wl_display_get_registry(p_ctx->p_display);
    (void)wl_registry_add_listener(p_ctx->p_registry,
                                   &g_registry_listener, p_ctx);

    /* First roundtrip binds the globals; the second delivers the
     * wl_output name/description events for the outputs we just bound. */
    (void)wl_display_roundtrip(p_ctx->p_display);
    (void)wl_display_roundtrip(p_ctx->p_display);

    if (NULL == p_ctx->p_shm)
    {
        (void)fprintf(stderr, "[capture] compositor has no wl_shm\n");
        goto cleanup;
    }
    if (NULL == p_ctx->p_screencopy)
    {
        (void)fprintf(stderr,
            "[capture] compositor does not support wlr-screencopy.\n"
            "          Needs a wlroots-based compositor (Hyprland,\n"
            "          Sway, river, ...).\n");
        goto cleanup;
    }

    /* Pick the output to capture. */
    for (i = 0; i < p_ctx->output_count; i++)
    {
        const char * p_name = p_ctx->outputs[i].p_name;
        const char * p_desc = p_ctx->outputs[i].p_description;

        if (NULL == p_name)
        {
            continue;
        }

        (void)fprintf(stderr, "[capture] output: %s%s\n", p_name,
                      ((NULL != p_desc) &&
                       (NULL != strstr(p_desc, GLASSES_OUTPUT_TAG)))
                          ? "  (glasses - skipped)" : "");

        if (NULL != p_output_name)
        {
            if (0 == strcmp(p_name, p_output_name))
            {
                /* Claim the target only if its name copy succeeded.
                 * Setting p_target with a NULL name would slip past the
                 * "no capturable output" check below and hand callers a
                 * target they cannot name. */
                char * p_copy = strdup(p_name);

                if (NULL == p_copy)
                {
                    goto cleanup;
                }
                free(p_ctx->p_target_name);
                p_ctx->p_target      = p_ctx->outputs[i].p_output;
                p_ctx->p_target_name = p_copy;
            }
        }
        else
        {
            /* Auto-pick: anything that is not the glasses, so we never
             * capture the display we are rendering onto. */
            int is_glasses = ((NULL != p_desc) &&
                              (NULL != strstr(p_desc,
                                              GLASSES_OUTPUT_TAG)));

            if ((0 == is_glasses) && (NULL == p_ctx->p_target))
            {
                char * p_copy = strdup(p_name);

                if (NULL == p_copy)
                {
                    goto cleanup;
                }
                free(p_ctx->p_target_name);
                p_ctx->p_target      = p_ctx->outputs[i].p_output;
                p_ctx->p_target_name = p_copy;
            }
        }
    }

    if (NULL == p_ctx->p_target)
    {
        (void)fprintf(stderr,
                      "[capture] no capturable output found%s%s\n",
                      (NULL != p_output_name) ? " named " : "",
                      (NULL != p_output_name) ? p_output_name : "");
        goto cleanup;
    }

    (void)fprintf(stderr, "[capture] capturing %s\n",
                  p_ctx->p_target_name);

    request_frame(p_ctx);
    (void)wl_display_roundtrip(p_ctx->p_display);

    p_result = p_ctx;
    p_ctx    = NULL;

cleanup:
    if (NULL != p_ctx)
    {
        screen_capture_destroy(p_ctx);
    }
    return p_result;
}

/**********************************************************************
 * @brief  Pump the Wayland event queue and keep frames flowing.
 **********************************************************************/
int screen_capture_poll(screen_capture_ctx_t * p_ctx)
{
    int result    = 0;
    int new_frame = 0;

    if (NULL == p_ctx)
    {
        result = -1;
        goto cleanup;
    }

    /* Non-blocking: flush our requests, then drain whatever has
     * arrived. The renderer must never stall waiting on the
     * compositor. */
    (void)wl_display_flush(p_ctx->p_display);

    if (0 > wl_display_dispatch_pending(p_ctx->p_display))
    {
        result = -1;
        goto cleanup;
    }

    /* Read anything queued on the socket without blocking. */
    if (0 == wl_display_prepare_read(p_ctx->p_display))
    {
        (void)wl_display_read_events(p_ctx->p_display);
        (void)wl_display_dispatch_pending(p_ctx->p_display);
    }
    else
    {
        (void)wl_display_dispatch_pending(p_ctx->p_display);
    }

    if ((0 != p_ctx->frame_ready) || (0 != p_ctx->frame_failed))
    {
        if (0 != p_ctx->frame_ready)
        {
            new_frame = 1;
        }

        /* This capture is done with; release it and start the next. */
        if (NULL != p_ctx->p_frame)
        {
            zwlr_screencopy_frame_v1_destroy(p_ctx->p_frame);
            p_ctx->p_frame = NULL;
        }
        p_ctx->capture_pending = 0;
        p_ctx->frame_ready     = 0;
        p_ctx->frame_failed    = 0;

        request_frame(p_ctx);
    }

    result = new_frame;

cleanup:
    return result;
}

/**********************************************************************
 * @brief  Access the most recently captured frame.
 **********************************************************************/
const uint8_t * screen_capture_pixels(screen_capture_ctx_t * p_ctx,
                                      uint32_t * p_width,
                                      uint32_t * p_height,
                                      uint32_t * p_stride,
                                      screen_pixel_format_t * p_format,
                                      int * p_y_invert)
{
    const uint8_t * p_pixels = NULL;

    if (NULL == p_ctx)
    {
        goto cleanup;
    }
    if ((0 == p_ctx->have_frame) || (NULL == p_ctx->p_data))
    {
        goto cleanup;
    }

    if (NULL != p_width)
    {
        *p_width = p_ctx->width;
    }
    if (NULL != p_height)
    {
        *p_height = p_ctx->height;
    }
    if (NULL != p_stride)
    {
        *p_stride = p_ctx->stride;
    }
    if (NULL != p_format)
    {
        *p_format = p_ctx->format;
    }
    if (NULL != p_y_invert)
    {
        *p_y_invert = p_ctx->y_invert;
    }

    p_pixels = (const uint8_t *)p_ctx->p_data;

cleanup:
    return p_pixels;
}

/**********************************************************************
 * @brief  Name of the output being captured.
 **********************************************************************/
const char * screen_capture_output_name(screen_capture_ctx_t * p_ctx)
{
    const char * p_name = NULL;

    if (NULL == p_ctx)
    {
        goto cleanup;
    }
    p_name = p_ctx->p_target_name;

cleanup:
    return p_name;
}

/**********************************************************************
 * @brief  Tear down the capture session and free all resources.
 **********************************************************************/
void screen_capture_destroy(screen_capture_ctx_t * p_ctx)
{
    int i = 0;

    if (NULL == p_ctx)
    {
        goto cleanup;
    }

    if (NULL != p_ctx->p_frame)
    {
        zwlr_screencopy_frame_v1_destroy(p_ctx->p_frame);
        p_ctx->p_frame = NULL;
    }

    release_buffer(p_ctx);

    for (i = 0; i < p_ctx->output_count; i++)
    {
        if (NULL != p_ctx->outputs[i].p_name)
        {
            free(p_ctx->outputs[i].p_name);
        }
        if (NULL != p_ctx->outputs[i].p_description)
        {
            free(p_ctx->outputs[i].p_description);
        }
        if (NULL != p_ctx->outputs[i].p_output)
        {
            wl_output_destroy(p_ctx->outputs[i].p_output);
        }
    }

    if (NULL != p_ctx->p_target_name)
    {
        free(p_ctx->p_target_name);
    }
    if (NULL != p_ctx->p_screencopy)
    {
        zwlr_screencopy_manager_v1_destroy(p_ctx->p_screencopy);
    }
    if (NULL != p_ctx->p_shm)
    {
        wl_shm_destroy(p_ctx->p_shm);
    }
    if (NULL != p_ctx->p_registry)
    {
        wl_registry_destroy(p_ctx->p_registry);
    }
    if (NULL != p_ctx->p_display)
    {
        wl_display_disconnect(p_ctx->p_display);
    }

    free(p_ctx);

cleanup:
    return;
}
