#pragma once
#include "core_annotation.h"
#include "core_format.h"
#include "core_types.h"

typedef union {
  struct {
    f32 x, y, z, w;
  };
  struct {
    f32 r, g, b, a;
  };
  ALIGNAS(16) f32 comps[4];
} GeoVector;

ASSERT(sizeof(GeoVector) == 16, "GeoVector has to be 128 bits");
ASSERT(alignof(GeoVector) == 16, "GeoVector has to be aligned to 128 bits");

/**
 * Construct a new vector.
 */
#define geo_vector(...) ((GeoVector){0, ##__VA_ARGS__})

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

/**
 * Compute a vector where each component is the result of dividing by the scalar.
 */
GeoVector geo_vector_div(GeoVector, f32 scalar);

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
 * Perspective divide: divide the vector by its w component.
 */
GeoVector geo_vector_perspective_div(GeoVector);

/**
 * Create a formatting argument for a vector.
 * NOTE: _VEC_ is expanded multiple times, so care must be taken when providing complex expressions.
 */
#define geo_vector_fmt(_VEC_)                                                                      \
  fmt_list_lit(                                                                                    \
      fmt_float((_VEC_).x), fmt_float((_VEC_).y), fmt_float((_VEC_).z), fmt_float((_VEC_).w))
