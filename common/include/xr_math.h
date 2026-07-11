/**********************************************************************
 * @file    xr_math.h
 * @brief   Minimal 4x4 matrix / quaternion math for AR rendering.
 * @copyright 2026 XR_Playground. Educational sample code.
 *
 * Just enough linear algebra to turn a Carina head pose into an OpenGL
 * view matrix. Matrices are 16 floats in COLUMN-MAJOR order, matching
 * what OpenGL expects from glUniformMatrix4fv with transpose = GL_FALSE.
 *
 * Coding standard: Barr-C:2018.
 **********************************************************************/

#ifndef XR_MATH_H
#define XR_MATH_H

/* A 4x4 matrix is 16 floats, column-major (OpenGL convention). */
#define MAT4_ELEMENTS   (16)

#ifdef __cplusplus
extern "C"
{
#endif

/**********************************************************************
 * @brief  Write the 4x4 identity matrix.
 *
 * @param[out] p_out  Destination, MAT4_ELEMENTS floats.
 **********************************************************************/
void mat4_identity(float * p_out);

/**********************************************************************
 * @brief  Build a right-handed perspective projection matrix.
 *
 * @param[out] p_out       Destination, MAT4_ELEMENTS floats.
 * @param[in]  fov_y_rad   Vertical field of view, in radians.
 * @param[in]  aspect      Viewport width / height.
 * @param[in]  near_plane  Near clip distance (> 0).
 * @param[in]  far_plane   Far clip distance (> near_plane).
 **********************************************************************/
void mat4_perspective(float * p_out, float fov_y_rad, float aspect,
                      float near_plane, float far_plane);

/**********************************************************************
 * @brief  Build a rotation matrix about the X axis.
 *
 * @param[out] p_out      Destination, MAT4_ELEMENTS floats.
 * @param[in]  angle_rad  Rotation angle in radians.
 **********************************************************************/
void mat4_rotate_x(float * p_out, float angle_rad);

/**********************************************************************
 * @brief  Build a rotation matrix about the Y axis.
 *
 * @param[out] p_out      Destination, MAT4_ELEMENTS floats.
 * @param[in]  angle_rad  Rotation angle in radians.
 **********************************************************************/
void mat4_rotate_y(float * p_out, float angle_rad);

/**********************************************************************
 * @brief  Multiply two matrices: out = a * b.
 *
 * p_out may alias neither p_a nor p_b.
 *
 * @param[out] p_out  Destination, MAT4_ELEMENTS floats.
 * @param[in]  p_a    Left operand.
 * @param[in]  p_b    Right operand.
 **********************************************************************/
void mat4_multiply(float * p_out, const float * p_a, const float * p_b);

/**********************************************************************
 * @brief  Build a translation matrix.
 *
 * @param[out] p_out  Destination, MAT4_ELEMENTS floats.
 * @param[in]  x      Translation along x.
 * @param[in]  y      Translation along y.
 * @param[in]  z      Translation along z.
 **********************************************************************/
void mat4_translation(float * p_out, float x, float y, float z);

/**********************************************************************
 * @brief  Build a scale matrix.
 *
 * @param[out] p_out  Destination, MAT4_ELEMENTS floats.
 * @param[in]  x      Scale along x.
 * @param[in]  y      Scale along y. Pass a negative value to flip the
 *                    geometry vertically (used when the compositor
 *                    delivers a bottom-up image).
 * @param[in]  z      Scale along z.
 **********************************************************************/
void mat4_scale(float * p_out, float x, float y, float z);

/**********************************************************************
 * @brief  Normalised linear interpolation between two quaternions.
 *
 * Used to low-pass filter a noisy head pose. Handles the double-cover
 * problem: q and -q describe the same rotation, so the source is negated
 * when the two lie on opposite hemispheres, without which the filter
 * would take the long way round and snap.
 *
 * nlerp rather than slerp: for the small angular steps between
 * consecutive frames the difference is imperceptible, and it cannot
 * divide by zero.
 *
 * @param[out] p_out  Result [qw, qx, qy, qz], normalised.
 * @param[in]  p_from Current (smoothed) quaternion.
 * @param[in]  p_to   New measurement.
 * @param[in]  t      Blend factor, 0 = keep p_from, 1 = take p_to.
 **********************************************************************/
void quat_nlerp(float * p_out, const float * p_from,
                const float * p_to, float t);

/**********************************************************************
 * @brief  Rotate the forward axis (0, 0, -1) by a quaternion.
 *
 * Gives the direction the head is looking, in world space.
 *
 * @param[out] p_out        Forward vector [x, y, z], normalised.
 * @param[in]  p_quaternion Orientation [qw, qx, qy, qz].
 **********************************************************************/
void quat_forward(float * p_out, const float * p_quaternion);

/**********************************************************************
 * @brief  Build an upright rotation matrix facing a given direction.
 *
 * Produces a rotation whose +Z axis is p_normal and whose X axis is
 * horizontal, which zeroes any roll. This is what keeps a world-locked
 * panel level regardless of how the wearer's head was tilted when it
 * was anchored.
 *
 * @param[out] p_out    Destination, MAT4_ELEMENTS floats.
 * @param[in]  p_normal Direction the surface should face (normalised).
 **********************************************************************/
void mat4_upright_facing(float * p_out, const float * p_normal);

/**********************************************************************
 * @brief  Turn a head pose into an OpenGL view matrix.
 *
 * The Carina pose is Twb (world-from-body): it places the head in the
 * world. A view matrix does the opposite, so this computes the inverse:
 *
 *     view = inverse(T) = R^T * translate(-position)
 *
 * Because the rotation is orthonormal, the inverse is just its
 * transpose, which is why no general matrix inversion is needed.
 *
 * @param[out] p_out         Destination, MAT4_ELEMENTS floats.
 * @param[in]  p_position    Head position [px, py, pz].
 * @param[in]  p_quaternion  Head orientation [qw, qx, qy, qz].
 **********************************************************************/
void mat4_view_from_pose(float * p_out, const float * p_position,
                         const float * p_quaternion);

#ifdef __cplusplus
}
#endif

#endif /* XR_MATH_H */
