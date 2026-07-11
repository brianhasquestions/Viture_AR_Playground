/**********************************************************************
 * @file    camera_capture.h
 * @brief   Thin C wrapper around the VITURE camera provider that
 *          captures MJPEG frames from the Luma Ultra glasses camera.
 * @copyright 2026 XR_Playground. Educational test project.
 *
 * The VITURE camera provider delivers 1920x1080 MJPEG frames at 30 fps
 * on a dedicated SDK thread. This module registers a frame callback,
 * counts the frames as they arrive, and writes the first N frames to
 * disk as standalone .jpg files so that the feed can be verified.
 *
 * All shared state lives in a heap-allocated capture_ctx_t. A POSIX
 * mutex guards the fields written by the SDK callback thread and read
 * by the main thread.
 *
 * Coding standard: Barr-C:2018.
 **********************************************************************/

#ifndef CAMERA_CAPTURE_H
#define CAMERA_CAPTURE_H

#include <pthread.h>
#include <stdint.h>

/* Frames requested per callback configuration is fixed by the SDK at  */
/* 1920x1080 @ 30 fps, MJPEG. These are informational only.            */
#define CAMERA_FRAME_WIDTH      (1920U)
#define CAMERA_FRAME_HEIGHT     (1080U)

/**********************************************************************
 * @brief  Shared capture context (heap allocated).
 *
 * Fields marked "callback thread" are written only from the SDK frame
 * callback and must be accessed under lock. Fields marked "main
 * thread" are owned by the orchestrating thread.
 **********************************************************************/
typedef struct
{
    /* --- Guarded shared state (callback thread <-> main thread) --- */
    pthread_mutex_t lock;             /* Protects the counters below. */
    uint32_t        frames_received;  /* Total frames seen so far.    */
    uint32_t        frames_saved;     /* Frames written to disk.      */
    uint64_t        bytes_received;   /* Cumulative MJPEG byte count.  */

    /* --- Configuration (main thread, read-only in callback) ------- */
    uint32_t        max_frames_to_save; /* Cap on files written.      */
    char *          p_output_dir;       /* Heap copy of output path.  */
    int             stream_mode;        /* 1: pipe MJPEG to stdout.   */

    /* --- SDK handle (main thread) --------------------------------- */
    void *          p_camera_handle;    /* XRCameraProviderHandle.    */
} capture_ctx_t;

#ifdef __cplusplus
extern "C"
{
#endif

/**********************************************************************
 * @brief  Allocate and initialise a capture context on the heap.
 *
 * @param[in]  p_output_dir        Directory where .jpg frames are
 *                                 written. Copied internally.
 * @param[in]  max_frames_to_save  Maximum number of frames to persist.
 *
 * @return  Pointer to a new context, or NULL on allocation failure.
 *          Release with camera_capture_destroy().
 **********************************************************************/
capture_ctx_t * camera_capture_create(const char * p_output_dir,
                                      uint32_t max_frames_to_save);

/**********************************************************************
 * @brief  Enable live-stream mode.
 *
 * In stream mode each MJPEG frame is written verbatim to stdout as it
 * arrives, forming a continuous MJPEG stream that a player can consume:
 *
 *     ./bin/viture_camera_test --stream | ffplay -f mjpeg -i -
 *
 * Frames are no longer written to disk, and all human-readable logging
 * is redirected to stderr so it cannot corrupt the stream.
 *
 * @param[in]  p_ctx    Context from camera_capture_create().
 * @param[in]  enabled  Non-zero to stream to stdout.
 **********************************************************************/
void camera_capture_set_stream_mode(capture_ctx_t * p_ctx, int enabled);

/**********************************************************************
 * @brief  Open the glasses camera and begin streaming frames.
 *
 * Resolves the camera VID/PID from the glasses product ID, creates the
 * SDK camera provider, and starts delivery to the internal callback.
 *
 * @param[in]  p_ctx         Context from camera_capture_create().
 * @param[in]  glasses_pid   Glasses USB product ID from device_scan.
 *
 * @return  0 on success, negative on failure.
 **********************************************************************/
int camera_capture_start(capture_ctx_t * p_ctx, int glasses_pid);

/**********************************************************************
 * @brief  Stop streaming and release the SDK camera provider.
 *
 * Safe to call even if streaming never started.
 *
 * @param[in]  p_ctx   Context from camera_capture_create().
 **********************************************************************/
void camera_capture_stop(capture_ctx_t * p_ctx);

/**********************************************************************
 * @brief  Read the current frame count under lock.
 *
 * @param[in]  p_ctx   Context from camera_capture_create().
 *
 * @return  Number of frames received so far (0 if p_ctx is NULL).
 **********************************************************************/
uint32_t camera_capture_frame_count(capture_ctx_t * p_ctx);

/**********************************************************************
 * @brief  Destroy a capture context and free all owned memory.
 *
 * Calls camera_capture_stop() first. Safe to pass NULL.
 *
 * @param[in]  p_ctx   Context to free.
 **********************************************************************/
void camera_capture_destroy(capture_ctx_t * p_ctx);

#ifdef __cplusplus
}
#endif

#endif /* CAMERA_CAPTURE_H */
