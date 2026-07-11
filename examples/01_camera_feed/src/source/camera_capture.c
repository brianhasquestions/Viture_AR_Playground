/**********************************************************************
 * @file    camera_capture.c
 * @brief   MJPEG frame capture from the VITURE Luma Ultra camera.
 * @copyright 2026 XR_Playground. Educational test project.
 *
 * Implementation notes:
 *  - The SDK invokes on_camera_frame() from a dedicated camera thread.
 *    Shared counters are guarded by a POSIX mutex.
 *  - The capture context is heap allocated; the callback receives it
 *    through the SDK's user_data pointer, so no globals are needed.
 *  - Single point of exit via goto cleanup throughout, Yoda notation
 *    for comparisons, and all locals initialised to their null value.
 **********************************************************************/

#include "camera_capture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* VITURE SDK public headers (C linkage). */
#include "viture_camera_provider.h"
#include "viture_result.h"

/* Longest path we build for a per-frame output file. */
#define OUTPUT_PATH_MAX     (1024U)

/* The camera is not immediately re-claimable after a previous session */
/* releases it: the USB device needs a moment to settle before it will */
/* negotiate a stream format again. Back-to-back runs would otherwise  */
/* fail intermittently, so the start is retried with a short backoff.  */
#define START_MAX_ATTEMPTS  (5)
#define START_RETRY_DELAY_NS (500000000L)   /* 500 ms */

/**********************************************************************
 * @brief  Persist a single MJPEG frame to disk as a .jpg file.
 *
 * @param[in]  p_ctx    Capture context (for the output directory).
 * @param[in]  p_frame  Frame delivered by the SDK.
 *
 * @return  0 on success, -1 on failure.
 **********************************************************************/
static int save_frame_to_disk(const capture_ctx_t * p_ctx,
                             const XRCameraFrame * p_frame)
{
    int      result   = -1;
    char *   p_path    = NULL;
    FILE *   p_file    = NULL;
    size_t   written   = 0;

    if ((NULL == p_ctx) || (NULL == p_frame))
    {
        goto cleanup;
    }

    p_path = (char *)calloc(OUTPUT_PATH_MAX, sizeof(char));
    if (NULL == p_path)
    {
        goto cleanup;
    }

    (void)snprintf(p_path, OUTPUT_PATH_MAX, "%s/frame_%06u.jpg",
                   p_ctx->p_output_dir, p_frame->sequence);

    p_file = fopen(p_path, "wb");
    if (NULL == p_file)
    {
        goto cleanup;
    }

    written = fwrite(p_frame->data, sizeof(uint8_t),
                     (size_t)p_frame->size, p_file);
    if (written != (size_t)p_frame->size)
    {
        goto cleanup;
    }

    result = 0;

cleanup:
    if (NULL != p_file)
    {
        (void)fclose(p_file);
    }
    if (NULL != p_path)
    {
        free(p_path);
    }
    return result;
}

/**********************************************************************
 * @brief  SDK frame callback. Runs on the SDK's camera thread.
 *
 * Counts every frame under lock and writes the first
 * max_frames_to_save frames to disk. The frame buffer is only valid
 * for the duration of this call, so bytes must be copied out here.
 *
 * @param[in]  p_frame     Frame data (valid only during this call).
 * @param[in]  p_user_data Capture context pointer (capture_ctx_t *).
 **********************************************************************/
static void on_camera_frame(const XRCameraFrame * p_frame,
                           void * p_user_data)
{
    capture_ctx_t * p_ctx        = NULL;
    int             should_save  = 0;

    p_ctx = (capture_ctx_t *)p_user_data;

    if ((NULL == p_ctx) || (NULL == p_frame))
    {
        goto cleanup;
    }
    if ((XR_CAMERA_FORMAT_MJPEG != p_frame->format) ||
        (NULL == p_frame->data) || (0U == p_frame->size))
    {
        goto cleanup;
    }

    (void)pthread_mutex_lock(&p_ctx->lock);

    p_ctx->frames_received += 1U;
    p_ctx->bytes_received  += (uint64_t)p_frame->size;

    if ((0 == p_ctx->stream_mode) &&
        (p_ctx->frames_saved < p_ctx->max_frames_to_save))
    {
        should_save = 1;
    }

    (void)pthread_mutex_unlock(&p_ctx->lock);

    /* Live-stream mode: emit the raw MJPEG bytes on stdout. Back-to- */
    /* back JPEGs form a valid MJPEG stream for ffplay/mpv.           */
    if (0 != p_ctx->stream_mode)
    {
        (void)fwrite(p_frame->data, sizeof(uint8_t),
                     (size_t)p_frame->size, stdout);
        (void)fflush(stdout);
        goto cleanup;
    }

    /* File I/O is performed outside the lock to keep the critical     */
    /* section short. Only the first frames are written to disk.       */
    if (1 == should_save)
    {
        if (0 == save_frame_to_disk(p_ctx, p_frame))
        {
            (void)pthread_mutex_lock(&p_ctx->lock);
            p_ctx->frames_saved += 1U;
            (void)pthread_mutex_unlock(&p_ctx->lock);

            (void)printf("[camera] saved frame seq=%u  %ux%u  %u bytes\n",
                         p_frame->sequence, p_frame->width,
                         p_frame->height, p_frame->size);
            (void)fflush(stdout);
        }
    }

cleanup:
    return;
}

/**********************************************************************
 * @brief  Allocate and initialise a capture context on the heap.
 **********************************************************************/
capture_ctx_t * camera_capture_create(const char * p_output_dir,
                                      uint32_t max_frames_to_save)
{
    capture_ctx_t * p_ctx      = NULL;
    capture_ctx_t * p_result   = NULL;
    int             mutex_ok   = -1;
    size_t          dir_len    = 0;

    if (NULL == p_output_dir)
    {
        goto cleanup;
    }

    p_ctx = (capture_ctx_t *)calloc(1U, sizeof(capture_ctx_t));
    if (NULL == p_ctx)
    {
        goto cleanup;
    }

    /* calloc already zeroed every field; set the non-zero ones. */
    p_ctx->max_frames_to_save = max_frames_to_save;
    p_ctx->p_camera_handle    = NULL;

    dir_len = strlen(p_output_dir) + 1U;
    p_ctx->p_output_dir = (char *)calloc(dir_len, sizeof(char));
    if (NULL == p_ctx->p_output_dir)
    {
        goto cleanup;
    }
    (void)memcpy(p_ctx->p_output_dir, p_output_dir, dir_len);

    mutex_ok = pthread_mutex_init(&p_ctx->lock, NULL);
    if (0 != mutex_ok)
    {
        goto cleanup;
    }

    /* Success: hand ownership to the caller and stop cleanup. */
    p_result = p_ctx;
    p_ctx    = NULL;

cleanup:
    if (NULL != p_ctx)
    {
        /* Partial construction failed; unwind what we built. */
        if (NULL != p_ctx->p_output_dir)
        {
            free(p_ctx->p_output_dir);
        }
        free(p_ctx);
    }
    return p_result;
}

/**********************************************************************
 * @brief  Enable live-stream mode.
 **********************************************************************/
void camera_capture_set_stream_mode(capture_ctx_t * p_ctx, int enabled)
{
    if (NULL == p_ctx)
    {
        goto cleanup;
    }

    (void)pthread_mutex_lock(&p_ctx->lock);
    p_ctx->stream_mode = enabled;
    (void)pthread_mutex_unlock(&p_ctx->lock);

cleanup:
    return;
}

/**********************************************************************
 * @brief  Open the glasses camera and begin streaming frames.
 **********************************************************************/
int camera_capture_start(capture_ctx_t * p_ctx, int glasses_pid)
{
    int             result   = -1;
    int             cam_vid  = 0;
    int             cam_pid  = 0;
    int             attempt  = 0;
    int             started  = VITURE_GLASSES_ERROR_UNKNOWN;
    void *          p_handle = NULL;
    struct timespec backoff  = { 0 };

    if (NULL == p_ctx)
    {
        goto cleanup;
    }

    cam_vid = xr_camera_provider_get_camera_vid(glasses_pid);
    cam_pid = xr_camera_provider_get_camera_pid(glasses_pid);
    if (0 == cam_pid)
    {
        (void)fprintf(stderr,
                      "[camera] glasses PID 0x%04X has no camera\n",
                      (unsigned int)glasses_pid);
        goto cleanup;
    }

    /* Status goes to stderr: in stream mode stdout carries MJPEG.  */
    (void)fprintf(stderr, "[camera] camera VID=0x%04X PID=0x%04X\n",
                  (unsigned int)cam_vid, (unsigned int)cam_pid);

    backoff.tv_sec  = 0;
    backoff.tv_nsec = START_RETRY_DELAY_NS;

    for (attempt = 1; attempt <= START_MAX_ATTEMPTS; attempt++)
    {
        p_handle = xr_camera_provider_create(cam_vid, cam_pid);
        if (NULL == p_handle)
        {
            (void)fprintf(stderr,
                          "[camera] provider create failed\n");
            goto cleanup;
        }

        /* Context is passed as user_data so the callback avoids     */
        /* globals entirely.                                          */
        started = xr_camera_provider_start(p_handle, on_camera_frame,
                                           (void *)p_ctx);
        if (VITURE_GLASSES_SUCCESS == started)
        {
            /* Streaming. Hand the handle to the context and stop.   */
            p_ctx->p_camera_handle = p_handle;
            p_handle               = NULL;
            result                 = 0;
            goto cleanup;
        }

        /* Failed: drop this handle before retrying so the device is */
        /* fully released.                                           */
        xr_camera_provider_destroy(p_handle);
        p_handle = NULL;

        if (attempt < START_MAX_ATTEMPTS)
        {
            (void)fprintf(stderr,
                          "[camera] start failed (%d), retrying "
                          "(%d/%d)...\n",
                          started, attempt, START_MAX_ATTEMPTS);
            (void)nanosleep(&backoff, NULL);
        }
    }

    (void)fprintf(stderr,
                  "[camera] provider start failed after %d attempts "
                  "(%d)\n", START_MAX_ATTEMPTS, started);

cleanup:
    return result;
}

/**********************************************************************
 * @brief  Stop streaming and release the SDK camera provider.
 **********************************************************************/
void camera_capture_stop(capture_ctx_t * p_ctx)
{
    if (NULL == p_ctx)
    {
        goto cleanup;
    }
    if (NULL == p_ctx->p_camera_handle)
    {
        goto cleanup;
    }

    (void)xr_camera_provider_stop(p_ctx->p_camera_handle);
    xr_camera_provider_destroy(p_ctx->p_camera_handle);
    p_ctx->p_camera_handle = NULL;

cleanup:
    return;
}

/**********************************************************************
 * @brief  Read the current frame count under lock.
 **********************************************************************/
uint32_t camera_capture_frame_count(capture_ctx_t * p_ctx)
{
    uint32_t count = 0U;

    if (NULL == p_ctx)
    {
        goto cleanup;
    }

    (void)pthread_mutex_lock(&p_ctx->lock);
    count = p_ctx->frames_received;
    (void)pthread_mutex_unlock(&p_ctx->lock);

cleanup:
    return count;
}

/**********************************************************************
 * @brief  Destroy a capture context and free all owned memory.
 **********************************************************************/
void camera_capture_destroy(capture_ctx_t * p_ctx)
{
    if (NULL == p_ctx)
    {
        goto cleanup;
    }

    camera_capture_stop(p_ctx);
    (void)pthread_mutex_destroy(&p_ctx->lock);

    if (NULL != p_ctx->p_output_dir)
    {
        free(p_ctx->p_output_dir);
        p_ctx->p_output_dir = NULL;
    }

    free(p_ctx);

cleanup:
    return;
}
