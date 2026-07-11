/**********************************************************************
 * @file    carina_pose.c
 * @brief   6DoF head tracking for VITURE Carina devices (Luma Ultra).
 * @copyright 2026 XR_Playground. Educational sample code.
 *
 * Implementation notes:
 *  - The SDK lifecycle order is load-bearing and not obvious:
 *      set_dof_type_carina() must precede initialize(), and
 *      register_callbacks_carina() must precede start().
 *    Getting either wrong yields a handle that starts but never tracks.
 *  - The pose is queried on demand rather than cached from a poll
 *    thread, so the caller always renders the freshest sample.
 *  - Single point of exit via goto cleanup; Yoda comparisons; locals
 *    initialised to their type's null value.
 **********************************************************************/

#include "carina_pose.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* VITURE SDK public headers (C linkage). */
#include "viture_device_carina.h"
#include "viture_glasses_provider.h"
#include "viture_result.h"

/* Number of floats in a pose: [px, py, pz, qw, qx, qy, qz]. */
#define POSE_FLOAT_COUNT    (7)

/**********************************************************************
 * @brief  Allocate a tracking context on the heap.
 **********************************************************************/
carina_pose_ctx_t * carina_pose_create(void)
{
    carina_pose_ctx_t * p_ctx = NULL;

    p_ctx = (carina_pose_ctx_t *)calloc(1U,
                                        sizeof(carina_pose_ctx_t));
    if (NULL == p_ctx)
    {
        goto cleanup;
    }

    /* calloc zeroed everything; make the intent explicit. */
    p_ctx->p_handle = NULL;
    p_ctx->is_6dof  = 0;
    p_ctx->started  = 0;

cleanup:
    return p_ctx;
}

/**********************************************************************
 * @brief  Bring up the Carina tracking pipeline.
 **********************************************************************/
int carina_pose_start(carina_pose_ctx_t * p_ctx, int glasses_pid,
                      int is_6dof)
{
    int    result      = -1;
    int    rc          = VITURE_GLASSES_ERROR_UNKNOWN;
    int    device_type = -1;
    void * p_handle    = NULL;

    if (NULL == p_ctx)
    {
        goto cleanup;
    }

    p_handle = xr_device_provider_create(glasses_pid);
    if (NULL == p_handle)
    {
        (void)fprintf(stderr,
                      "[pose] device provider create failed\n");
        goto cleanup;
    }

    /* This module only knows how to drive Carina hardware. */
    device_type = xr_device_provider_get_device_type(p_handle);
    if (XR_DEVICE_TYPE_VITURE_CARINA != device_type)
    {
        (void)fprintf(stderr,
                      "[pose] device is not a Carina (type %d); 6DoF "
                      "tracking unavailable\n", device_type);
        goto cleanup;
    }

    /* Order matters: DoF type must be chosen BEFORE initialize(). */
    rc = xr_device_provider_set_dof_type_carina(p_handle,
                                                (0 != is_6dof) ? 1 : 0);
    if (VITURE_GLASSES_SUCCESS != rc)
    {
        (void)fprintf(stderr, "[pose] set_dof_type failed (%d)\n", rc);
        goto cleanup;
    }

    rc = xr_device_provider_initialize(p_handle, NULL, NULL);
    if (VITURE_GLASSES_SUCCESS != rc)
    {
        (void)fprintf(stderr, "[pose] initialize failed (%d)\n", rc);
        goto cleanup;
    }

    /* Order matters: the VIO engine captures the callback pointers at */
    /* start() time, so they must be registered first. We do not need  */
    /* the stereo frames ourselves, so every callback is NULL; the VIO */
    /* still consumes them internally to produce the pose.             */
    rc = xr_device_provider_register_callbacks_carina(p_handle, NULL,
                                                      NULL, NULL, NULL);
    if (VITURE_GLASSES_SUCCESS != rc)
    {
        (void)fprintf(stderr,
                      "[pose] register_callbacks failed (%d)\n", rc);
        goto cleanup;
    }

    rc = xr_device_provider_start(p_handle);
    if (VITURE_GLASSES_SUCCESS != rc)
    {
        (void)fprintf(stderr, "[pose] start failed (%d)\n", rc);
        (void)xr_device_provider_shutdown(p_handle);
        goto cleanup;
    }

    p_ctx->p_handle = p_handle;
    p_ctx->is_6dof  = is_6dof;
    p_ctx->started  = 1;
    p_handle        = NULL;   /* Ownership transferred to the context. */
    result          = 0;

    (void)fprintf(stderr, "[pose] Carina tracking started (%s)\n",
                  (0 != is_6dof) ? "6DoF" : "3DoF");

cleanup:
    if (NULL != p_handle)
    {
        /* Construction failed after create(): unwind the handle. */
        xr_device_provider_destroy(p_handle);
    }
    return result;
}

/**********************************************************************
 * @brief  Read the current head pose.
 **********************************************************************/
int carina_pose_get(carina_pose_ctx_t * p_ctx, float * p_position,
                    float * p_quaternion, int * p_status,
                    double predict_ns)
{
    int   result                   = -1;
    int   rc                       = VITURE_GLASSES_ERROR_UNKNOWN;
    int   status                   = CARINA_POSE_UNSTABLE;
    float pose[POSE_FLOAT_COUNT]   = { 0.0F };

    if ((NULL == p_ctx) || (0 == p_ctx->started))
    {
        goto cleanup;
    }
    if ((NULL == p_position) || (NULL == p_quaternion))
    {
        goto cleanup;
    }

    rc = xr_device_provider_get_gl_pose_carina(p_ctx->p_handle, pose,
                                               predict_ns, &status);
    if (VITURE_GLASSES_SUCCESS != rc)
    {
        goto cleanup;
    }

    /* SDK layout: [px, py, pz, qw, qx, qy, qz]. */
    p_position[0]   = pose[0];
    p_position[1]   = pose[1];
    p_position[2]   = pose[2];
    p_quaternion[0] = pose[3];   /* w */
    p_quaternion[1] = pose[4];   /* x */
    p_quaternion[2] = pose[5];   /* y */
    p_quaternion[3] = pose[6];   /* z */

    if (NULL != p_status)
    {
        *p_status = status;
    }

    result = 0;

cleanup:
    return result;
}

/**********************************************************************
 * @brief  Recentre tracking on the wearer's current pose.
 **********************************************************************/
int carina_pose_recenter(carina_pose_ctx_t * p_ctx)
{
    int   result                 = -1;
    int   rc                     = VITURE_GLASSES_ERROR_UNKNOWN;
    int   status                 = CARINA_POSE_UNSTABLE;
    float pose[POSE_FLOAT_COUNT] = { 0.0F };

    if ((NULL == p_ctx) || (0 == p_ctx->started))
    {
        goto cleanup;
    }

    /* Feeding the current pose back in as the new origin re-anchors   */
    /* the world in front of the wearer.                               */
    rc = xr_device_provider_get_gl_pose_carina(p_ctx->p_handle, pose,
                                               0.0, &status);
    if (VITURE_GLASSES_SUCCESS != rc)
    {
        goto cleanup;
    }

    rc = xr_device_provider_reset_origin_carina(p_ctx->p_handle, pose);
    if (VITURE_GLASSES_SUCCESS != rc)
    {
        goto cleanup;
    }

    result = 0;

cleanup:
    return result;
}

/**********************************************************************
 * @brief  Stop tracking and release the SDK device provider.
 **********************************************************************/
void carina_pose_stop(carina_pose_ctx_t * p_ctx)
{
    if (NULL == p_ctx)
    {
        goto cleanup;
    }
    if (NULL == p_ctx->p_handle)
    {
        goto cleanup;
    }

    if (1 == p_ctx->started)
    {
        (void)xr_device_provider_stop(p_ctx->p_handle);
        (void)xr_device_provider_shutdown(p_ctx->p_handle);
        p_ctx->started = 0;
    }

    xr_device_provider_destroy(p_ctx->p_handle);
    p_ctx->p_handle = NULL;

cleanup:
    return;
}

/**********************************************************************
 * @brief  Destroy a tracking context and free it.
 **********************************************************************/
void carina_pose_destroy(carina_pose_ctx_t * p_ctx)
{
    if (NULL == p_ctx)
    {
        goto cleanup;
    }

    carina_pose_stop(p_ctx);
    free(p_ctx);

cleanup:
    return;
}
