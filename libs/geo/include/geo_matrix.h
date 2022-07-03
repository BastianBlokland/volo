#pragma once
#include "geo_plane.h"
#include "geo_quat.h"
#include "geo_vector.h"

/**
 * Geometric 4x4 matrix.
 *
 * Column major and a left handed coordinate system.
 * - Positive x = right.
 * - Positive y = up.
 * - Positive z = 'into' the screen.
 *
 * Clip space:
 * - Output top left:     -1, -1
 * - Output top right:    +1, -1
 * - Output bottom left:  -1, +1
 * - Output bottom right: +1, +1
 * - Output depth: 0 - 1.
 */

typedef union uGeoMatrix {
  GeoVector columns[4];
  ALIGNAS(16) f32 comps[16];
} GeoMatrix;

ASSERT(sizeof(GeoMatrix) == 64, "GeoMatrix has to be 512 bits");
ASSERT(alignof(GeoMatrix) == 16, "GeoMatrix has to be aligned to 128 bits");

/**
 * Identity matrix.
 * Represents no scaling, no rotation and no translation.
 */
GeoMatrix geo_matrix_ident();

/**
 * Retrieve a row from the matrix.
 * NOTE: Matrix is stored as column-major so prefer using columns.
 * Pre-condition: index < 4
 */
GeoVector geo_matrix_row(const GeoMatrix*, usize index);

/**
 * Compute a new matrix which combines the two given matrices.
 * (dot product of the rows and columns).
 */
GeoMatrix geo_matrix_mul(const GeoMatrix* a, const GeoMatrix* b);
void geo_matrix_mul_batch(const GeoMatrix* a, const GeoMatrix* b, GeoMatrix* restrict out, u32 cnt);

/**
 * Transform a vector by the given matrix.
 */
GeoVector geo_matrix_transform(const GeoMatrix*, GeoVector);
GeoVector geo_matrix_transform3(const GeoMatrix*, GeoVector);
GeoVector geo_matrix_transform3_point(const GeoMatrix*, GeoVector);

/**
 * Return a new matrix that has the matrix's rows exchanged with its columns.
 */
GeoMatrix geo_matrix_transpose(const GeoMatrix*);

/**
 * Return a new matrix that is the inverse of the given matrix.
 */
GeoMatrix geo_matrix_inverse(const GeoMatrix*);

/**
 * Create a translation matrix.
 */
GeoMatrix geo_matrix_translate(GeoVector translation);

/**
 * Extract the translation vector from the given matrix.
 */
GeoVector geo_matrix_to_translation(const GeoMatrix*);

/**
 * Create a scale matrix.
 */
GeoMatrix geo_matrix_scale(GeoVector scale);

/**
 * Extract the scale (magnitude) vector from the given matrix.
 * NOTE: Scale sign is not extracted.
 */
GeoVector geo_matrix_to_scale(const GeoMatrix*);

/**
 * Create a rotation matrix around one of the dimensions.
 * NOTE: Angle is in radians.
 */
GeoMatrix geo_matrix_rotate_x(f32 angle);
GeoMatrix geo_matrix_rotate_y(f32 angle);
GeoMatrix geo_matrix_rotate_z(f32 angle);

/**
 * Create a rotation matrix from the identity rotation to the given axes set.
 * Pre-condition: right, up, fwd are a orthonormal set.
 */
GeoMatrix geo_matrix_rotate(GeoVector right, GeoVector up, GeoVector fwd);

/**
 * Create a rotation matrix that rotates from the rotation to a new axis system.
 * NOTE: Vectors do not need to be normalized, but should not be zero.
 * NOTE: Up does not need to be orthogonal to fwd as the up is reconstructed.
 */
GeoMatrix geo_matrix_rotate_look(GeoVector forward, GeoVector upRef);

/**
 * Create a rotation matrix from a quaternion.
 * Pre-condition: quaternion is normalized.
 */
GeoMatrix geo_matrix_from_quat(GeoQuat);

/**
 * Convert a rotation matrix to a quaternion.
 * Pre-condition: matrix has to be an orthogonal matrix.
 */
GeoQuat geo_matrix_to_quat(const GeoMatrix*);

/**
 * Construct transformation matrix from the given translation, rotation and scale.
 */
GeoMatrix geo_matrix_trs(GeoVector t, GeoQuat r, GeoVector s);

/**
 * Create an orthographic projection matrix.
 * NOTE: Uses reversed-z depth so near objects are at depth 1 and far at 0.
 */
GeoMatrix geo_matrix_proj_ortho(f32 width, f32 height, f32 zNear, f32 zFar);
GeoMatrix geo_matrix_proj_ortho_ver(f32 size, f32 aspect, f32 zNear, f32 zFar);
GeoMatrix geo_matrix_proj_ortho_hor(f32 size, f32 aspect, f32 zNear, f32 zFar);

/**
 * Create a perspective projection matrix.
 * NOTE: Uses reversed-z with an infinite far plane, so near objects are at depth 1 and depth
 * reaches 0 at infinite z.
 * NOTE: Angles are in radians.
 */
GeoMatrix geo_matrix_proj_pers(f32 horAngle, f32 verAngle, f32 zNear);
GeoMatrix geo_matrix_proj_pers_ver(f32 verAngle, f32 aspect, f32 zNear);
GeoMatrix geo_matrix_proj_pers_hor(f32 horAngle, f32 aspect, f32 zNear);

/**
 * Extract frustum planes from a projection matrix.
 * NOTE: The near and the far planes are not extracted. Reasoning is that near is not often usefull
 * for clipping against and we're using an infinite far plane for perspective projections.
 * NOTE: Plane normals point towards the inside of the frustum.
 *
 * [0] = Left plane.
 * [1] = Right plane.
 * [2] = Top plane.
 * [3] = Bottom plane.
 */
void geo_matrix_frustum4(const GeoMatrix* viewProj, GeoPlane out[4]);
