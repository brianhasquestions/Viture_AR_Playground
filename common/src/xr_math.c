/**********************************************************************
 * @file    xr_math.c
 * @brief   Minimal 4x4 matrix / quaternion math for AR rendering.
 * @copyright 2026 XR_Playground. Educational sample code.
 *
 * Matrices are column-major: element (row r, column c) lives at index
 * [(c * 4) + r]. This is the layout OpenGL wants, so the arrays can be
 * handed straight to glUniformMatrix4fv without transposing.
 *
 * Coding standard: Barr-C:2018.
 **********************************************************************/

#include "xr_math.h"

#include <math.h>
#include <stddef.h>

/* Index of element (row, col) within a column-major 4x4 matrix. */
#define AT(row, col)    (((col) * 4) + (row))

/**********************************************************************
 * @brief  Write the 4x4 identity matrix.
 **********************************************************************/
void mat4_identity(float * p_out)
{
    int index = 0;

    if (NULL == p_out)
    {
        goto cleanup;
    }

    for (index = 0; index < MAT4_ELEMENTS; index++)
    {
        p_out[index] = 0.0F;
    }

    p_out[AT(0, 0)] = 1.0F;
    p_out[AT(1, 1)] = 1.0F;
    p_out[AT(2, 2)] = 1.0F;
    p_out[AT(3, 3)] = 1.0F;

cleanup:
    return;
}

/**********************************************************************
 * @brief  Build a right-handed perspective projection matrix.
 **********************************************************************/
void mat4_perspective(float * p_out, float fov_y_rad, float aspect,
                      float near_plane, float far_plane)
{
    float focal = 0.0F;
    float depth = 0.0F;
    int   index = 0;

    if (NULL == p_out)
    {
        goto cleanup;
    }
    if ((0.0F >= aspect) || (0.0F >= near_plane) ||
        (far_plane <= near_plane))
    {
        goto cleanup;
    }

    for (index = 0; index < MAT4_ELEMENTS; index++)
    {
        p_out[index] = 0.0F;
    }

    /* cot(fov/2): the focal length in normalised device units. */
    focal = 1.0F / tanf(fov_y_rad * 0.5F);
    depth = near_plane - far_plane;

    p_out[AT(0, 0)] = focal / aspect;
    p_out[AT(1, 1)] = focal;
    p_out[AT(2, 2)] = (far_plane + near_plane) / depth;
    p_out[AT(3, 2)] = -1.0F;
    p_out[AT(2, 3)] = (2.0F * far_plane * near_plane) / depth;

cleanup:
    return;
}

/**********************************************************************
 * @brief  Build a rotation matrix about the X axis.
 **********************************************************************/
void mat4_rotate_x(float * p_out, float angle_rad)
{
    float c = 0.0F;
    float s = 0.0F;

    if (NULL == p_out)
    {
        goto cleanup;
    }

    c = cosf(angle_rad);
    s = sinf(angle_rad);

    mat4_identity(p_out);
    p_out[AT(1, 1)] = c;
    p_out[AT(1, 2)] = -s;
    p_out[AT(2, 1)] = s;
    p_out[AT(2, 2)] = c;

cleanup:
    return;
}

/**********************************************************************
 * @brief  Build a rotation matrix about the Y axis.
 **********************************************************************/
void mat4_rotate_y(float * p_out, float angle_rad)
{
    float c = 0.0F;
    float s = 0.0F;

    if (NULL == p_out)
    {
        goto cleanup;
    }

    c = cosf(angle_rad);
    s = sinf(angle_rad);

    mat4_identity(p_out);
    p_out[AT(0, 0)] = c;
    p_out[AT(0, 2)] = s;
    p_out[AT(2, 0)] = -s;
    p_out[AT(2, 2)] = c;

cleanup:
    return;
}

/**********************************************************************
 * @brief  Multiply two matrices: out = a * b.
 **********************************************************************/
void mat4_multiply(float * p_out, const float * p_a, const float * p_b)
{
    int row = 0;
    int col = 0;
    int k   = 0;

    if ((NULL == p_out) || (NULL == p_a) || (NULL == p_b))
    {
        goto cleanup;
    }

    for (col = 0; col < 4; col++)
    {
        for (row = 0; row < 4; row++)
        {
            float sum = 0.0F;

            for (k = 0; k < 4; k++)
            {
                sum += p_a[AT(row, k)] * p_b[AT(k, col)];
            }

            p_out[AT(row, col)] = sum;
        }
    }

cleanup:
    return;
}

/**********************************************************************
 * @brief  Build a translation matrix.
 **********************************************************************/
void mat4_translation(float * p_out, float x, float y, float z)
{
    if (NULL == p_out)
    {
        goto cleanup;
    }

    mat4_identity(p_out);
    p_out[AT(0, 3)] = x;
    p_out[AT(1, 3)] = y;
    p_out[AT(2, 3)] = z;

cleanup:
    return;
}

/**********************************************************************
 * @brief  Build a scale matrix.
 **********************************************************************/
void mat4_scale(float * p_out, float x, float y, float z)
{
    if (NULL == p_out)
    {
        goto cleanup;
    }

    mat4_identity(p_out);
    p_out[AT(0, 0)] = x;
    p_out[AT(1, 1)] = y;
    p_out[AT(2, 2)] = z;

cleanup:
    return;
}

/**********************************************************************
 * @brief  Normalised linear interpolation between two quaternions.
 **********************************************************************/
void quat_nlerp(float * p_out, const float * p_from,
                const float * p_to, float t)
{
    float dot   = 0.0F;
    float sign  = 1.0F;
    float norm  = 0.0F;
    int   index = 0;

    if ((NULL == p_out) || (NULL == p_from) || (NULL == p_to))
    {
        goto cleanup;
    }

    for (index = 0; index < 4; index++)
    {
        dot += p_from[index] * p_to[index];
    }

    /* q and -q are the same rotation. If the measurement is on the
     * opposite hemisphere, negate it so the blend takes the short arc
     * instead of spinning the long way round. */
    if (0.0F > dot)
    {
        sign = -1.0F;
    }

    for (index = 0; index < 4; index++)
    {
        p_out[index] = p_from[index]
                     + (t * ((sign * p_to[index]) - p_from[index]));
    }

    norm = sqrtf((p_out[0] * p_out[0]) + (p_out[1] * p_out[1]) +
                 (p_out[2] * p_out[2]) + (p_out[3] * p_out[3]));

    if (0.000001F < norm)
    {
        for (index = 0; index < 4; index++)
        {
            p_out[index] /= norm;
        }
    }
    else
    {
        p_out[0] = 1.0F;
        p_out[1] = 0.0F;
        p_out[2] = 0.0F;
        p_out[3] = 0.0F;
    }

cleanup:
    return;
}

/**********************************************************************
 * @brief  Rotate the forward axis (0, 0, -1) by a quaternion.
 **********************************************************************/
void quat_forward(float * p_out, const float * p_quaternion)
{
    float qw   = 0.0F;
    float qx   = 0.0F;
    float qy   = 0.0F;
    float qz   = 0.0F;
    float norm = 0.0F;

    if ((NULL == p_out) || (NULL == p_quaternion))
    {
        goto cleanup;
    }

    qw = p_quaternion[0];
    qx = p_quaternion[1];
    qy = p_quaternion[2];
    qz = p_quaternion[3];

    /* Forward is -Z, so negate the rotated Z basis vector (the third
     * column of the rotation matrix). */
    p_out[0] = -(2.0F * ((qx * qz) + (qy * qw)));
    p_out[1] = -(2.0F * ((qy * qz) - (qx * qw)));
    p_out[2] = -(1.0F - (2.0F * ((qx * qx) + (qy * qy))));

    norm = sqrtf((p_out[0] * p_out[0]) + (p_out[1] * p_out[1]) +
                 (p_out[2] * p_out[2]));
    if (0.000001F < norm)
    {
        p_out[0] /= norm;
        p_out[1] /= norm;
        p_out[2] /= norm;
    }
    else
    {
        p_out[0] = 0.0F;
        p_out[1] = 0.0F;
        p_out[2] = -1.0F;
    }

cleanup:
    return;
}

/**********************************************************************
 * @brief  Build an upright rotation matrix facing a given direction.
 **********************************************************************/
void mat4_upright_facing(float * p_out, const float * p_normal)
{
    float x_axis[3] = { 1.0F, 0.0F, 0.0F };
    float y_axis[3] = { 0.0F, 1.0F, 0.0F };
    float z_axis[3] = { 0.0F, 0.0F, 1.0F };
    float norm      = 0.0F;

    if ((NULL == p_out) || (NULL == p_normal))
    {
        goto cleanup;
    }

    z_axis[0] = p_normal[0];
    z_axis[1] = p_normal[1];
    z_axis[2] = p_normal[2];

    /* X = normalize(world_up cross Z). Deriving X from the world up
     * vector is what removes roll: X is forced horizontal, so the panel
     * can never end up tilted no matter how the head was rolled. */
    x_axis[0] = (1.0F * z_axis[2]) - (0.0F * z_axis[1]);
    x_axis[1] = (0.0F * z_axis[0]) - (0.0F * z_axis[2]);
    x_axis[2] = (0.0F * z_axis[1]) - (1.0F * z_axis[0]);

    norm = sqrtf((x_axis[0] * x_axis[0]) + (x_axis[1] * x_axis[1]) +
                 (x_axis[2] * x_axis[2]));

    if (0.001F > norm)
    {
        /* Looking almost straight up or down: world up and the normal
         * are parallel, so the cross product is degenerate. Fall back
         * to a fixed X axis. */
        x_axis[0] = 1.0F;
        x_axis[1] = 0.0F;
        x_axis[2] = 0.0F;
    }
    else
    {
        x_axis[0] /= norm;
        x_axis[1] /= norm;
        x_axis[2] /= norm;
    }

    /* Y = Z cross X, completing a right-handed orthonormal basis. */
    y_axis[0] = (z_axis[1] * x_axis[2]) - (z_axis[2] * x_axis[1]);
    y_axis[1] = (z_axis[2] * x_axis[0]) - (z_axis[0] * x_axis[2]);
    y_axis[2] = (z_axis[0] * x_axis[1]) - (z_axis[1] * x_axis[0]);

    mat4_identity(p_out);

    p_out[AT(0, 0)] = x_axis[0];
    p_out[AT(1, 0)] = x_axis[1];
    p_out[AT(2, 0)] = x_axis[2];

    p_out[AT(0, 1)] = y_axis[0];
    p_out[AT(1, 1)] = y_axis[1];
    p_out[AT(2, 1)] = y_axis[2];

    p_out[AT(0, 2)] = z_axis[0];
    p_out[AT(1, 2)] = z_axis[1];
    p_out[AT(2, 2)] = z_axis[2];

cleanup:
    return;
}

/**********************************************************************
 * @brief  Turn a head pose into an OpenGL view matrix.
 **********************************************************************/
void mat4_view_from_pose(float * p_out, const float * p_position,
                         const float * p_quaternion)
{
    float qw    = 0.0F;
    float qx    = 0.0F;
    float qy    = 0.0F;
    float qz    = 0.0F;
    float norm  = 0.0F;
    float r[MAT4_ELEMENTS] = { 0.0F };

    if ((NULL == p_out) || (NULL == p_position) ||
        (NULL == p_quaternion))
    {
        goto cleanup;
    }

    qw = p_quaternion[0];
    qx = p_quaternion[1];
    qy = p_quaternion[2];
    qz = p_quaternion[3];

    /* Guard against an all-zero quaternion before the device has a    */
    /* fix; renormalise otherwise so drift cannot skew the rotation.   */
    norm = sqrtf((qw * qw) + (qx * qx) + (qy * qy) + (qz * qz));
    if (0.000001F > norm)
    {
        mat4_identity(p_out);
        p_out[AT(0, 3)] = -p_position[0];
        p_out[AT(1, 3)] = -p_position[1];
        p_out[AT(2, 3)] = -p_position[2];
        goto cleanup;
    }

    qw /= norm;
    qx /= norm;
    qy /= norm;
    qz /= norm;

    /* Rotation matrix R (world-from-body) from the unit quaternion.   */
    mat4_identity(r);
    r[AT(0, 0)] = 1.0F - (2.0F * ((qy * qy) + (qz * qz)));
    r[AT(0, 1)] = 2.0F * ((qx * qy) - (qz * qw));
    r[AT(0, 2)] = 2.0F * ((qx * qz) + (qy * qw));

    r[AT(1, 0)] = 2.0F * ((qx * qy) + (qz * qw));
    r[AT(1, 1)] = 1.0F - (2.0F * ((qx * qx) + (qz * qz)));
    r[AT(1, 2)] = 2.0F * ((qy * qz) - (qx * qw));

    r[AT(2, 0)] = 2.0F * ((qx * qz) - (qy * qw));
    r[AT(2, 1)] = 2.0F * ((qy * qz) + (qx * qw));
    r[AT(2, 2)] = 1.0F - (2.0F * ((qx * qx) + (qy * qy)));

    /* The view matrix is the inverse of the head transform. For an    */
    /* orthonormal rotation the inverse is simply the transpose, and   */
    /* the translation becomes -R^T * position.                        */
    mat4_identity(p_out);

    p_out[AT(0, 0)] = r[AT(0, 0)];
    p_out[AT(0, 1)] = r[AT(1, 0)];
    p_out[AT(0, 2)] = r[AT(2, 0)];

    p_out[AT(1, 0)] = r[AT(0, 1)];
    p_out[AT(1, 1)] = r[AT(1, 1)];
    p_out[AT(1, 2)] = r[AT(2, 1)];

    p_out[AT(2, 0)] = r[AT(0, 2)];
    p_out[AT(2, 1)] = r[AT(1, 2)];
    p_out[AT(2, 2)] = r[AT(2, 2)];

    p_out[AT(0, 3)] = -((p_out[AT(0, 0)] * p_position[0]) +
                        (p_out[AT(0, 1)] * p_position[1]) +
                        (p_out[AT(0, 2)] * p_position[2]));
    p_out[AT(1, 3)] = -((p_out[AT(1, 0)] * p_position[0]) +
                        (p_out[AT(1, 1)] * p_position[1]) +
                        (p_out[AT(1, 2)] * p_position[2]));
    p_out[AT(2, 3)] = -((p_out[AT(2, 0)] * p_position[0]) +
                        (p_out[AT(2, 1)] * p_position[1]) +
                        (p_out[AT(2, 2)] * p_position[2]));

cleanup:
    return;
}
