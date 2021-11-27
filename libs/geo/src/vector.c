#include "core_diag.h"
#include "core_float.h"
#include "core_math.h"
#include "geo_vector.h"

bool geo_vector_equal(const GeoVector a, const GeoVector b, const f32 threshold) {
  const GeoVector diff = geo_vector_sub(a, b);
  return geo_vector_mag_sqr(diff) <= (threshold * threshold);
}

GeoVector geo_vector_add(const GeoVector a, const GeoVector b) {
  return geo_vector(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w);
}

GeoVector geo_vector_sub(const GeoVector a, const GeoVector b) {
  return geo_vector(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w);
}

GeoVector geo_vector_mul(const GeoVector v, const f32 scalar) {
  return geo_vector(v.x * scalar, v.y * scalar, v.z * scalar, v.w * scalar);
}

GeoVector geo_vector_div(const GeoVector v, f32 scalar) {
  diag_assert(scalar != 0);
  return geo_vector(v.x / scalar, v.y / scalar, v.z / scalar, v.w / scalar);
}

f32 geo_vector_mag_sqr(const GeoVector v) { return geo_vector_dot(v, v); }

f32 geo_vector_mag(const GeoVector v) {
  const f32 sqrMag = geo_vector_mag_sqr(v);
  return sqrMag != 0 ? math_sqrt_f32(sqrMag) : 0;
}

GeoVector geo_vector_norm(const GeoVector v) {
  const f32 mag = geo_vector_mag(v);
  diag_assert(mag != 0);
  return geo_vector_div(v, mag);
}

f32 geo_vector_dot(const GeoVector a, const GeoVector b) {
  f32 res = 0;
  res += a.x * b.x;
  res += a.y * b.y;
  res += a.z * b.z;
  res += a.w * b.w;
  return res;
}

GeoVector geo_vector_cross3(const GeoVector a, const GeoVector b) {
  const f32 x = a.y * b.z - a.z * b.y;
  const f32 y = a.z * b.x - a.x * b.z;
  const f32 z = a.x * b.y - a.y * b.x;
  return geo_vector(x, y, z);
}

f32 geo_vector_angle(const GeoVector from, const GeoVector to) {
  const f32 denom = math_sqrt_f32(geo_vector_mag_sqr(from) * geo_vector_mag_sqr(to));
  if (denom <= f32_epsilon) {
    return 0;
  }
  const f32 dot = geo_vector_dot(from, to);
  return math_acos_f32(math_clamp_f32(dot / denom, -1, 1));
}

GeoVector geo_vector_project(const GeoVector v, const GeoVector nrm) {
  const f32 nrmSqrMag = geo_vector_mag_sqr(nrm);
  if (nrmSqrMag <= f32_epsilon) {
    return geo_vector(0);
  }
  return geo_vector_mul(nrm, geo_vector_dot(v, nrm) / nrmSqrMag);
}

GeoVector geo_vector_reflect(const GeoVector v, const GeoVector nrm) {
  const f32 dot = geo_vector_dot(v, nrm);
  return geo_vector_sub(v, geo_vector_mul(nrm, dot * 2));
}

GeoVector geo_vector_lerp(const GeoVector x, const GeoVector y, const f32 t) {
  GeoVector v;
  v.x = math_lerp(x.x, y.x, t);
  v.y = math_lerp(x.y, y.y, t);
  v.z = math_lerp(x.z, y.z, t);
  v.w = math_lerp(x.w, y.w, t);
  return v;
}

GeoVector geo_vector_perspective_div(const GeoVector v) {
  return geo_vector_div(geo_vector(v.x, v.y, v.z), v.w);
}
