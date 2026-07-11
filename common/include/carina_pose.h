/**********************************************************************
 * @file    carina_pose.h
 * @brief   6DoF head tracking for VITURE Carina devices (Luma Ultra).
 * @copyright 2026 XR_Playground. Educational sample code.
 *
 * The Luma Ultra is a Carina device (XR_DEVICE_TYPE_VITURE_CARINA). It
 * runs a VIO (visual-inertial odometry) engine that fuses its stereo
 * cameras with the IMU to produce a full 6DoF pose. That pose is read
 * through xr_device_provider_get_gl_pose_carina().
 *
 * Do NOT use the raw IMU functions in viture_device.h
 * (xr_device_provider_open_imu and friends) on a Carina device: the SDK
 * documents them as "no effect for carina device". They exist for the
 * Gen1/Gen2 glasses only.
 *
 * Pose convention (from the SDK): OpenGL coordinates,
 *   x -> right, y -> up, z -> backward,
 * expressed as [px, py, pz, qw, qx, qy, qz] (Twb, world-from-body).
 * Pitch and roll are gravity-anchored by the VIO and cannot drift.
 *
 * Coding standard: Barr-C:2018.
 **********************************************************************/

#ifndef CARINA_POSE_H
#define CARINA_POSE_H

#include <stdint.h>

/* Pose stability, as reported by the SDK. */
#define CARINA_POSE_STABLE      (0)
#define CARINA_POSE_UNSTABLE    (1)

/**********************************************************************
 * @brief  Tracking session state (heap allocated).
 **********************************************************************/
typedef struct
{
    void * p_handle;    /* XRDeviceProviderHandle.                    */
    int    is_6dof;     /* 1: 6DoF tracking, 0: 3DoF.                 */
    int    started;     /* 1 once xr_device_provider_start succeeded. */
} carina_pose_ctx_t;

#ifdef __cplusplus
extern "C"
{
#endif

/**********************************************************************
 * @brief  Allocate a tracking context on the heap.
 *
 * @return  New context, or NULL on allocation failure. Release with
 *          carina_pose_destroy().
 **********************************************************************/
carina_pose_ctx_t * carina_pose_create(void);

/**********************************************************************
 * @brief  Bring up the Carina tracking pipeline.
 *
 * Runs the SDK lifecycle in the order the SDK requires:
 *   create -> set_dof_type -> initialize -> register callbacks -> start
 * The DoF type MUST be set before initialize, and the Carina callbacks
 * MUST be registered before start (the VIO engine captures the callback
 * pointers at start time).
 *
 * Verifies that the device really is a Carina and fails otherwise.
 *
 * @param[in]  p_ctx        Context from carina_pose_create().
 * @param[in]  glasses_pid  Glasses product ID (from device_scan).
 * @param[in]  is_6dof      1 for 6DoF, 0 for 3DoF.
 *
 * @return  0 on success, negative on failure.
 **********************************************************************/
int carina_pose_start(carina_pose_ctx_t * p_ctx, int glasses_pid,
                      int is_6dof);

/**********************************************************************
 * @brief  Read the current head pose.
 *
 * Queries the SDK directly, so call this once per rendered frame to get
 * the freshest possible pose. Supplying a predict_ns matching your
 * display latency reduces perceived lag in an AR overlay.
 *
 * @param[in]  p_ctx        Started tracking context.
 * @param[out] p_position   Receives [px, py, pz] (metres). 3 floats.
 * @param[out] p_quaternion Receives [qw, qx, qy, qz]. 4 floats.
 * @param[out] p_status     Receives CARINA_POSE_STABLE / _UNSTABLE.
 *                          May be NULL.
 * @param[in]  predict_ns   Forward-prediction time in nanoseconds; 0
 *                          for the current pose.
 *
 * @return  0 on success, negative on failure.
 **********************************************************************/
int carina_pose_get(carina_pose_ctx_t * p_ctx, float * p_position,
                    float * p_quaternion, int * p_status,
                    double predict_ns);

/**********************************************************************
 * @brief  Recentre tracking on the wearer's current pose.
 *
 * Moves the tracking origin to where the user is looking now, so the
 * overlay re-anchors in front of them. Position and yaw are reset;
 * pitch and roll stay gravity-anchored (the VIO owns them).
 *
 * @param[in]  p_ctx   Started tracking context.
 *
 * @return  0 on success, negative on failure.
 **********************************************************************/
int carina_pose_recenter(carina_pose_ctx_t * p_ctx);

/**********************************************************************
 * @brief  Stop tracking and release the SDK device provider.
 *
 * @param[in]  p_ctx   Context to stop. Safe if never started.
 **********************************************************************/
void carina_pose_stop(carina_pose_ctx_t * p_ctx);

/**********************************************************************
 * @brief  Destroy a tracking context and free it. Safe to pass NULL.
 **********************************************************************/
void carina_pose_destroy(carina_pose_ctx_t * p_ctx);

#ifdef __cplusplus
}
#endif

#endif /* CARINA_POSE_H */
