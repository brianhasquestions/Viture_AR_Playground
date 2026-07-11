/**********************************************************************
 * @file    main.c
 * @brief   VITURE Luma Ultra camera-feed test harness.
 * @copyright 2026 XR_Playground. Educational test project.
 *
 * Flow:
 *   1. Scan the POSIX sysfs USB tree for a connected VITURE device and
 *      validate its product ID against the SDK.
 *   2. Report the device's market name.
 *   3. Open the glasses camera and stream MJPEG frames, saving the
 *      first few to disk under bin/captures.
 *   4. Run until the frame target is met, a timeout elapses, or the
 *      user presses Ctrl-C, then tear everything down cleanly.
 *
 * Coding standard: Barr-C:2018. Single point of exit via goto cleanup,
 * Yoda comparisons, fixed-width types, heap-allocated resources.
 **********************************************************************/

#include "camera_capture.h"
#include "device_scan.h"
#include "usb_detach.h"

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* VITURE SDK public headers (C linkage). */
#include "viture_camera_provider.h"
#include "viture_glasses_provider.h"

/* Directory that receives the captured .jpg frames. */
#define CAPTURE_OUTPUT_DIR      "bin/captures"

/* Number of frames to persist to disk before we consider the test    */
/* a success (the stream keeps running until the timeout regardless).  */
#define TARGET_FRAMES_TO_SAVE   (10U)

/* Hard timeout so the harness never blocks forever. */
#define CAPTURE_TIMEOUT_SECONDS (15)

/* Poll period while waiting for frames, in nanoseconds (100 ms). */
#define POLL_PERIOD_NS          (100000000L)

/* Market-name buffer size mandated by the SDK example usage. */
#define MARKET_NAME_MAX         (64U)

/* Process-wide stop flag, set from the SIGINT handler. sig_atomic_t  */
/* is the only type the C standard permits a handler to touch safely. */
static volatile sig_atomic_t g_stop_requested = 0;

/**********************************************************************
 * @brief  SIGINT handler: request a graceful shutdown.
 *
 * @param[in]  signal_number  Unused signal identifier.
 **********************************************************************/
static void handle_sigint(int signal_number)
{
    (void)signal_number;
    g_stop_requested = 1;
}

/**********************************************************************
 * @brief  Look up and print the glasses market name.
 *
 * @param[in]  glasses_pid  Validated glasses product ID.
 **********************************************************************/
static void report_market_name(int glasses_pid)
{
    char * p_name = NULL;
    int    length = 0;
    int    rc     = VITURE_GLASSES_ERROR_UNKNOWN;

    p_name = (char *)calloc(MARKET_NAME_MAX, sizeof(char));
    if (NULL == p_name)
    {
        goto cleanup;
    }

    length = (int)MARKET_NAME_MAX;
    rc     = xr_device_provider_get_market_name(glasses_pid, p_name,
                                                &length);
    if (VITURE_GLASSES_SUCCESS == rc)
    {
        (void)fprintf(stderr, "[device] connected: %s (PID 0x%04X)\n",
                      p_name, (unsigned int)glasses_pid);
    }
    else
    {
        (void)fprintf(stderr, "[device] connected: PID 0x%04X\n",
                      (unsigned int)glasses_pid);
    }

cleanup:
    if (NULL != p_name)
    {
        free(p_name);
    }
}

/**********************************************************************
 * @brief  Sleep for one poll period using the POSIX clock.
 **********************************************************************/
static void poll_sleep(void)
{
    struct timespec request = { 0 };

    request.tv_sec  = 0;
    request.tv_nsec = POLL_PERIOD_NS;
    (void)nanosleep(&request, NULL);
}

/**********************************************************************
 * @brief  Program entry point.
 *
 * @return  EXIT_SUCCESS when at least one frame was captured,
 *          EXIT_FAILURE otherwise.
 **********************************************************************/
int main(int argc, char * argv[])
{
    int             exit_code    = EXIT_FAILURE;
    int             glasses_pid  = DEVICE_SCAN_NO_PID;
    int             scan_rc      = -1;
    int             start_rc     = -1;
    int             cam_vid      = 0;
    int             cam_pid      = 0;
    int             stream_mode  = 0;
    int             arg_index    = 0;
    uint32_t        final_count  = 0U;
    time_t          start_time   = (time_t)0;
    time_t          now          = (time_t)0;
    capture_ctx_t * p_ctx        = NULL;

    /* --stream pipes raw MJPEG to stdout for a live viewer, e.g.      */
    /*   ./bin/viture_camera_test --stream | ffplay -f mjpeg -i -      */
    for (arg_index = 1; arg_index < argc; arg_index++)
    {
        if (0 == strcmp(argv[arg_index], "--stream"))
        {
            stream_mode = 1;
        }
    }

    /* Banner goes to stderr so it can never corrupt a piped stream. */
    (void)fprintf(stderr, "VITURE Luma Ultra camera-feed test\n");
    (void)fprintf(stderr, "----------------------------------\n");

    /* Install the Ctrl-C handler so we always reach cleanup. */
    (void)signal(SIGINT, handle_sigint);

    /* Quiet the SDK's internal logging to keep our output readable.   */
    xr_device_provider_set_log_level(LOG_LEVEL_ERROR);

    /* Step 1: locate a connected, valid VITURE device. */
    scan_rc = device_scan_find_glasses(
                  &xr_device_provider_is_product_id_valid,
                  &glasses_pid);
    if (0 != scan_rc)
    {
        (void)fprintf(stderr,
                      "[device] no VITURE glasses found on USB\n");
        goto cleanup;
    }

    /* Step 2: report which model we are talking to. */
    report_market_name(glasses_pid);

    /* Step 3: release the camera from the kernel's uvcvideo driver.   */
    /* The SDK drives UVC directly over libusb, so the kernel must not */
    /* be holding the interfaces. A failure here is not fatal: the     */
    /* camera may already be free, so we still attempt to stream.      */
    cam_vid = xr_camera_provider_get_camera_vid(glasses_pid);
    cam_pid = xr_camera_provider_get_camera_pid(glasses_pid);
    if (0 != cam_pid)
    {
        (void)usb_detach_kernel_driver(cam_vid, cam_pid);
    }

    /* Step 4: build the capture context on the heap. */
    p_ctx = camera_capture_create(CAPTURE_OUTPUT_DIR,
                                  TARGET_FRAMES_TO_SAVE);
    if (NULL == p_ctx)
    {
        (void)fprintf(stderr,
                      "[camera] failed to allocate capture context\n");
        goto cleanup;
    }

    camera_capture_set_stream_mode(p_ctx, stream_mode);

    /* Step 5: start streaming. */
    start_rc = camera_capture_start(p_ctx, glasses_pid);
    if (0 != start_rc)
    {
        (void)fprintf(stderr, "[camera] failed to start stream\n");
        goto cleanup;
    }

    if (0 != stream_mode)
    {
        (void)fprintf(stderr,
                      "[camera] live MJPEG on stdout; Ctrl-C to stop\n");
    }
    else
    {
        (void)fprintf(stderr,
                      "[camera] streaming; saving up to %u frames to %s\n",
                      (unsigned int)TARGET_FRAMES_TO_SAVE,
                      CAPTURE_OUTPUT_DIR);
        (void)fprintf(stderr,
                      "[camera] press Ctrl-C to stop early\n");
    }

    /* Step 6: wait for frames, honouring the stop flag and timeout. */
    start_time = time(NULL);
    for (;;)
    {
        uint32_t saved = 0U;

        if (1 == g_stop_requested)
        {
            (void)fprintf(stderr, "\n[camera] stop requested\n");
            break;
        }

        /* Live view runs until the user stops it: no target, no
           timeout. */
        if (0 != stream_mode)
        {
            poll_sleep();
            continue;
        }

        (void)pthread_mutex_lock(&p_ctx->lock);
        saved = p_ctx->frames_saved;
        (void)pthread_mutex_unlock(&p_ctx->lock);

        if (saved >= TARGET_FRAMES_TO_SAVE)
        {
            (void)fprintf(stderr,
                          "[camera] target of %u frames reached\n",
                          (unsigned int)TARGET_FRAMES_TO_SAVE);
            break;
        }

        now = time(NULL);
        if ((now - start_time) >= (time_t)CAPTURE_TIMEOUT_SECONDS)
        {
            (void)fprintf(stderr, "[camera] timeout after %d seconds\n",
                          CAPTURE_TIMEOUT_SECONDS);
            break;
        }

        poll_sleep();
    }

    /* Step 7: summarise. */
    final_count = camera_capture_frame_count(p_ctx);
    (void)fprintf(stderr, "[camera] total frames received: %u\n",
                  (unsigned int)final_count);

    if (0U < final_count)
    {
        exit_code = EXIT_SUCCESS;
    }

cleanup:
    /* Single teardown path for every early exit above. */
    if (NULL != p_ctx)
    {
        camera_capture_stop(p_ctx);
        camera_capture_destroy(p_ctx);
        p_ctx = NULL;
    }
    return exit_code;
}
