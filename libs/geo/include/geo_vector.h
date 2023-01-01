#pragma once
#include "core_annotation.h"
#include "core_types.h"

// Forward declare from 'core_rng.h'.
typedef struct sRng Rng;

/**
 * 4 component geometric spacial Vector.
 * For describing a position / offset in 2 / 3 / 4 dimensions.
 */

typedef union {
  struct {
    f32 x, y, z, w;
  };
  ALIGNAS(16) f32 comps[4];
} GeoVector;

ASSERT(sizeof(GeoVector) == 16, "GeoVector has to be 128 bits");
ASSERT(alignof(GeoVector) == 16, "GeoVector has to be aligned to 128 bits");

/**
 * Construct a new vector.
 */
#define geo_vector(...) ((GeoVector){__VA_ARGS__})

#define geo_up geo_vector(.y = +1.0f)
#define geo_down geo_vector(.y = -1.0f)
#define geo_right geo_vector(.x = +1.0f)
#define geo_left geo_vector(.x = -1.0f)
#define geo_forward geo_vector(.z = +1.0f)
#define geo_backward geo_vector(.z = -1.0f)

/**
 * Check if the magnitude of the difference vector is less then the given threshold.
 */
bool geo_vector_equal(GeoVector a, GeoVector b, f32 threshold);
bool geo_vector_equal3(GeoVector a, GeoVector b, f32 threshold);

/**
 * Compute the absolute value of each component.
 */
GeoVector geo_vector_abs(GeoVector);

/**
 * Compute a vector where each component is the result of adding the component of both vectors.
 */
GeoVector geo_vector_add(GeoVector a, GeoVector b);

/**
 * Compute a vector where each component is the result of subtracting the component of both vectors.
 */
GeoVector geo_vector_sub(GeoVector a, GeoVector b);

/**
 * Compute a vector where each component is the result of multiplying with the scalar.
 */
GeoVector geo_vector_mul(GeoVector, f32 scalar);
GeoVector geo_vector_mul_comps(GeoVector, GeoVector);

/**
 * Compute a vector where each component is the result of dividing by the scalar.
 */
GeoVector geo_vector_div(GeoVector, f32 scalar);
GeoVector geo_vector_div_comps(GeoVector, GeoVector);

/**
 * Calculate the magnitude of the vector squared.
 */
f32 geo_vector_mag_sqr(GeoVector);

/**
 * Calculate the magnitude of the vector.
 */
f32 geo_vector_mag(GeoVector);

/**
 * Calculate a normalized version of the given vector (unit vector).
 * Pre-condition: geo_vector_mag(v) != 0
 */
GeoVector geo_vector_norm(GeoVector);

/**
 * Calculate the dot product of two vectors.
 */
f32 geo_vector_dot(GeoVector a, GeoVector b);

/**
 * Calculate the cross product of two 3d vectors.
 */
GeoVector geo_vector_cross3(GeoVector a, GeoVector b);

/**
 * Calculate the shortest angle in radians between the given vectors.
 */
f32 geo_vector_angle(GeoVector from, GeoVector to);

/**
 * Project a vector onto another vector.
 */
GeoVector geo_vector_project(GeoVector, GeoVector nrm);

/**
 * Reflect a vector off a normal.
 */
GeoVector geo_vector_reflect(GeoVector, GeoVector nrm);

/**
 * Calculate the linearly interpolated vector from x to y at time t.
 * NOTE: Does not clamp t (so can extrapolate too).
 */
GeoVector geo_vector_lerp(GeoVector x, GeoVector y, f32 t);

/**
 * Calculate the bilinearly interpolated vector in the rectangle formed by v1, v2, v3 and v4.
 * More info: https://en.wikipedia.org/wiki/Bilinear_interpolation
 * NOTE: Does not clamp t (so can extrapolate too).
 */
GeoVector geo_vector_bilerp(GeoVector v1, GeoVector v2, GeoVector v3, GeoVector v4, f32 tX, f32 tY);

/**
 * Calculate the minimum / maximum value per component.
 */
GeoVector geo_vector_min(GeoVector x, GeoVector y);
GeoVector geo_vector_max(GeoVector x, GeoVector y);

/**
 * Clear out the non-specified components.
 */
GeoVector geo_vector_xyz(GeoVector);
GeoVector geo_vector_xz(GeoVector);

/**
 * Calculate the square root of elements x, y, z and w.
 * Pre-condition: vec.x >= 0 && vec.y >= 0 && vec.z >= 0 && vec.w >= 0
 */
GeoVector geo_vector_sqrt(GeoVector);

/**
 * Clamp a vector so its magnitude does not exceed the given value.
 *
 * Pre-condition: maxMagnitude >= 0
 */
GeoVector geo_vector_clamp(GeoVector, f32 maxMagnitude);

/**
 * Round all components to integers.
 */
GeoVector geo_vector_round_nearest(GeoVector);
GeoVector geo_vector_round_down(GeoVector);
GeoVector geo_vector_round_up(GeoVector);

/**
 * Perspective divide: divide the vector by its w component.
 */
GeoVector geo_vector_perspective_div(GeoVector);

/**
 * Quantize a vector to use a limited number of mantissa bits.
 * Pre-condition: maxMantissaBits > 0 && maxMantissaBits <= 23
 */
GeoVector geo_vector_quantize(GeoVector, u8 maxMantissaBits);
GeoVector geo_vector_quantize3(GeoVector, u8 maxMantissaBits);

/**
 * Pack a vector to 16 bit floats.
 */
void geo_vector_pack_f16(GeoVector, f16 out[4]);

/**
 * Generate a random point on the surface of a 3d unit sphere (aka randomly orientated unit vector).
 * NOTE: Resulting points are uniformly distributed.
 */
GeoVector geo_vector_rand_on_unit_sphere3(Rng*);

/**
 * Create a formatting argument for a vector.
 * NOTE: _VEC_ is expanded multiple times, so care must be taken when providing complex expressions.
 */
#define geo_vector_fmt(_VEC_)                                                                      \
  fmt_list_lit(                                                                                    \
      fmt_float((_VEC_).x), fmt_float((_VEC_).y), fmt_float((_VEC_).z), fmt_float((_VEC_).w))
