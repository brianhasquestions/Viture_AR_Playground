/**********************************************************************
 * @file    main.c
 * @brief   Optical see-through AR overlay for VITURE Luma Ultra.
 * @copyright 2026 XR_Playground. Educational sample code.
 *
 * Renders world-locked geometry on the glasses display, anchored in the
 * room by the Carina 6DoF head pose. Look around and the grid, cube and
 * axis gizmo stay put in space while the real world shows through.
 *
 * The whole idea in one line: the scene never moves, only the view
 * matrix does. Each frame we ask the device where the head is, invert
 * that into a view matrix, and redraw static world-space geometry.
 *
 * Flow:
 *   1. Find the glasses on USB (sysfs).
 *   2. Start Carina 6DoF tracking.
 *   3. Open a fullscreen GL window on the glasses' display.
 *   4. Per frame: read pose -> view matrix -> draw -> swap.
 *
 * Controls:  R = recentre    Esc / Q = quit
 *
 * Coding standard: Barr-C:2018.
 **********************************************************************/

#include "ar_render.h"
#include "carina_pose.h"
#include "device_scan.h"
#include "xr_args.h"
#include "xr_math.h"

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* VITURE SDK public headers (C linkage). */
#include "viture_glasses_provider.h"

/* Vertical field of view of the rendered frustum, in degrees.
 *
 * IMPORTANT: this is an approximation, not a calibrated value. It is
 * derived from the Luma Ultra's ~52 degree diagonal FOV at 16:9, which
 * works out near 27 degrees vertically. Getting it exactly right needs
 * the panel's true optical parameters. If the overlay appears to swim
 * or scale wrongly relative to the real world as you move, this is the
 * first number to tune: pass --fov <degrees>.
 */
#define DEFAULT_FOV_Y_DEGREES   (27.0F)

/* Clip planes, in metres. */
#define NEAR_PLANE_METRES       (0.05F)
#define FAR_PLANE_METRES        (50.0F)

/* Forward-prediction for the pose, in nanoseconds. Rendering and scan-
 * out take time, so asking for the pose slightly in the future reduces
 * the lag between head motion and what the wearer sees. ~11ms is one
 * frame at the panel's 90Hz.
 */
#define POSE_PREDICT_NS         (11000000.0)

#define DEGREES_TO_RADIANS      (3.14159265358979F / 180.0F)

/* How often to print pose to the terminal (frames). */
#define POSE_LOG_INTERVAL       (90U)

/* Process-wide stop flag, set from the SIGINT handler. */
static volatile sig_atomic_t g_stop_requested = 0;

/**********************************************************************
 * @brief  SIGINT handler: request a graceful shutdown.
 **********************************************************************/
static void handle_sigint(int signal_number)
{
    (void)signal_number;
    g_stop_requested = 1;
}

/**********************************************************************
 * @brief  Print usage.
 **********************************************************************/
static void print_usage(void)
{
    (void)fprintf(stderr,
        "Usage: ar_overlay [options]\n"
        "  --windowed     Preview in a desktop window instead of\n"
        "                 fullscreen on the glasses.\n"
        "  --display <n>  Force SDL display index n.\n"
        "  --fov <deg>    Vertical field of view (default %.1f).\n"
        "  --3dof         Orientation-only tracking (default 6DoF).\n"
        "  --help         Show this message.\n"
        "\nControls:  R = recentre    Esc/Q = quit\n",
        (double)DEFAULT_FOV_Y_DEGREES);
}

/**********************************************************************
 * @brief  Program entry point.
 **********************************************************************/
int main(int argc, char * argv[])
{
    int               exit_code     = EXIT_FAILURE;
    int               glasses_pid   = DEVICE_SCAN_NO_PID;
    int               display_index = AR_DISPLAY_NOT_FOUND;
    int               windowed      = 0;
    int               is_6dof       = 1;
    int               quit          = 0;
    int               recenter      = 0;
    int               pose_status   = CARINA_POSE_UNSTABLE;
    int               tracking_locked = 0;
    int               arg_index     = 0;
    uint32_t          frame_count   = 0U;
    float             fov_degrees   = DEFAULT_FOV_Y_DEGREES;
    float             position[3]   = { 0.0F };
    float             quaternion[4] = { 1.0F, 0.0F, 0.0F, 0.0F };
    float             view[MAT4_ELEMENTS]       = { 0.0F };
    float             projection[MAT4_ELEMENTS] = { 0.0F };
    carina_pose_ctx_t * p_pose   = NULL;
    ar_render_ctx_t *   p_render = NULL;

    /* --- Parse arguments ----------------------------------------- */
    for (arg_index = 1; arg_index < argc; arg_index++)
    {
        if (0 == strcmp(argv[arg_index], "--windowed"))
        {
            windowed = 1;
        }
        else if (0 == strcmp(argv[arg_index], "--3dof"))
        {
            is_6dof = 0;
        }
        else if ((0 == strcmp(argv[arg_index], "--display")) &&
                 ((arg_index + 1) < argc))
        {
            arg_index++;
            if (0 != xr_args_parse_int(argv[arg_index], &display_index))
            {
                (void)fprintf(stderr, "Invalid --display '%s'\n",
                              argv[arg_index]);
                goto cleanup;
            }
        }
        else if ((0 == strcmp(argv[arg_index], "--fov")) &&
                 ((arg_index + 1) < argc))
        {
            arg_index++;
            if (0 != xr_args_parse_float(argv[arg_index], &fov_degrees))
            {
                (void)fprintf(stderr, "Invalid --fov '%s'\n",
                              argv[arg_index]);
                goto cleanup;
            }
        }
        else if (0 == strcmp(argv[arg_index], "--help"))
        {
            print_usage();
            exit_code = EXIT_SUCCESS;
            goto cleanup;
        }
        else
        {
            (void)fprintf(stderr, "Unknown option: %s\n",
                          argv[arg_index]);
            print_usage();
            goto cleanup;
        }
    }

    (void)fprintf(stderr, "VITURE AR overlay (optical see-through)\n");
    (void)fprintf(stderr, "---------------------------------------\n");

    (void)signal(SIGINT, handle_sigint);
    xr_device_provider_set_log_level(LOG_LEVEL_ERROR);

    /* --- Step 1: find the glasses on USB -------------------------- */
    if (0 != device_scan_find_glasses(
                 &xr_device_provider_is_product_id_valid, &glasses_pid))
    {
        (void)fprintf(stderr,
                      "[device] no VITURE glasses found on USB\n");
        goto cleanup;
    }

    /* --- Step 2: start Carina head tracking ----------------------- */
    p_pose = carina_pose_create();
    if (NULL == p_pose)
    {
        goto cleanup;
    }
    if (0 != carina_pose_start(p_pose, glasses_pid, is_6dof))
    {
        goto cleanup;
    }

    /* --- Step 3: open a window on the glasses' display ------------ */
    if (AR_DISPLAY_NOT_FOUND == display_index)
    {
        display_index = ar_render_find_glasses_display();
    }
    if (AR_DISPLAY_NOT_FOUND == display_index)
    {
        if (0 == windowed)
        {
            (void)fprintf(stderr,
                "[render] no VITURE display found. Is the video cable\n"
                "         connected and the display enabled? Use\n"
                "         --display <n>, or --windowed to preview.\n");
            goto cleanup;
        }
        display_index = 0;
    }

    p_render = ar_render_create(display_index, windowed);
    if (NULL == p_render)
    {
        goto cleanup;
    }

    (void)fprintf(stderr,
                  "[ar] running. Waiting for VIO to converge - look\n"
                  "     around a lit area with some visual detail.\n");
    (void)fprintf(stderr,
                  "[ar] R = recentre, Esc/Q = quit\n");

    /* --- Step 4: render loop -------------------------------------- */
    while ((0 == quit) && (0 == g_stop_requested))
    {
        recenter = 0;
        ar_render_poll_events(p_render, &quit, &recenter);

        if (1 == recenter)
        {
            if (0 == carina_pose_recenter(p_pose))
            {
                (void)fprintf(stderr, "[ar] recentred\n");
            }
        }

        /* Ask the device where the head is, slightly into the future. */
        if (0 == carina_pose_get(p_pose, position, quaternion,
                                 &pose_status, POSE_PREDICT_NS))
        {
            /* The VIO needs a second or two of visual features and
             * parallax before it converges, and it reports pose_status
             * = UNSTABLE until then. Feeding that unconverged pose into
             * the view matrix makes the geometry fly around the room,
             * so hold the scene back until the first stable sample.
             *
             * On that first lock, recentre: the tracking origin snaps
             * to wherever the wearer is looking, so the scene appears
             * in front of them rather than wherever the device happened
             * to initialise. */
            if ((0 == tracking_locked) &&
                (CARINA_POSE_STABLE == pose_status))
            {
                (void)carina_pose_recenter(p_pose);
                tracking_locked = 1;
                (void)fprintf(stderr,
                              "[ar] tracking locked; scene anchored\n");

                /* Re-read after recentring so the very first drawn
                 * frame already uses the new origin. */
                (void)carina_pose_get(p_pose, position, quaternion,
                                      &pose_status, POSE_PREDICT_NS);
            }

            /* The scene is static in world space; inverting the head  */
            /* pose into a view matrix is what pins it to the room.    */
            mat4_view_from_pose(view, position, quaternion);
        }

        /* Rebuilt every frame because the drawable size is not final
         * until the compositor has applied fullscreen, and a windowed
         * preview can be resized at any time. */
        mat4_perspective(projection, fov_degrees * DEGREES_TO_RADIANS,
                         ar_render_aspect(p_render), NEAR_PLANE_METRES,
                         FAR_PLANE_METRES);

        /* Once locked we keep drawing even through a transient
         * unstable blip, so the overlay does not flicker. */
        ar_render_frame(p_render, view, projection, tracking_locked);

        frame_count++;
        if (0U == (frame_count % POSE_LOG_INTERVAL))
        {
            (void)fprintf(stderr,
                "[pose] %s  pos [% .3f % .3f % .3f]  quat "
                "[% .3f % .3f % .3f % .3f]\n",
                (CARINA_POSE_STABLE == pose_status) ? "stable  "
                                                    : "unstable",
                (double)position[0], (double)position[1],
                (double)position[2], (double)quaternion[0],
                (double)quaternion[1], (double)quaternion[2],
                (double)quaternion[3]);
        }
    }

    (void)fprintf(stderr, "[ar] rendered %u frames\n",
                  (unsigned int)frame_count);
    exit_code = EXIT_SUCCESS;

cleanup:
    /* Single teardown path for every exit above. */
    if (NULL != p_render)
    {
        ar_render_destroy(p_render);
        p_render = NULL;
    }
    if (NULL != p_pose)
    {
        carina_pose_destroy(p_pose);
        p_pose = NULL;
    }
    return exit_code;
}
