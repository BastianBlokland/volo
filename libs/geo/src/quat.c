#include "core_diag.h"
#include "core_math.h"
#include "geo_matrix.h"
#include "geo_quat.h"
#include "geo_vector.h"

GeoQuat geo_quat_angle_axis(const GeoVector axis, const f32 angle) {
  // TODO: Should we instaed just add the pre-condition that the axis should be a unit vector?
  const f32 axisMag = geo_vector_mag(axis);
  if (axisMag <= f32_epsilon) {
    return geo_quat_ident;
  }
  const GeoVector unitVecAxis = geo_vector_div(axis, axisMag);
  const GeoVector vec         = geo_vector_mul(unitVecAxis, math_sin_f32(angle * .5f));
  return (GeoQuat){vec.x, vec.y, vec.z, math_cos_f32(angle * .5f)};
}

GeoQuat geo_quat_from_to(const GeoQuat from, const GeoQuat to) {
  GeoQuat toIdentity = geo_quat_inv(from);
  return geo_quat_mul(to, toIdentity);
}

f32 geo_quat_angle(const GeoQuat q) {
  const GeoVector axis    = geo_vector(q.x, q.y, q.z);
  const f32       axisMag = geo_vector_mag(axis);
  return 2 * math_atan2_f32(axisMag, q.w);
}

GeoQuat geo_quat_mul(const GeoQuat a, const GeoQuat b) {
  return (GeoQuat){
      .x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
      .y = a.w * b.y + a.y * b.w + a.z * b.x - a.x * b.z,
      .z = a.w * b.z + a.z * b.w + a.x * b.y - a.y * b.x,
      .w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
  };
}

GeoVector geo_quat_rotate(const GeoQuat q, const GeoVector vec) {
  const GeoVector axis       = {q.x, q.y, q.z};
  const f32       axisSqrMag = geo_vector_mag_sqr(axis);
  const f32       scalar     = q.w;
  const GeoVector a          = geo_vector_mul(axis, geo_vector_dot(axis, vec) * 2);
  const GeoVector b          = geo_vector_mul(vec, scalar * scalar - axisSqrMag);
  const GeoVector c          = geo_vector_mul(geo_vector_cross3(axis, vec), scalar * 2);
  return geo_vector_add(geo_vector_add(a, b), c);
}

GeoQuat geo_quat_inv(const GeoQuat q) {
  // Compute the conjugate ('transposing').
  GeoQuat res = {
      .x = q.x * -1,
      .y = q.y * -1,
      .z = q.z * -1,
      .w = q.w,
  };

  // Divide by the squared length.
  // TODO: Should we just skip this? Is only needed for non-normalized quaternions.
  const f32 sqrMag = geo_vector_mag_sqr((GeoVector){q.x, q.y, q.z, q.w});
  res.x /= sqrMag;
  res.y /= sqrMag;
  res.z /= sqrMag;
  res.w /= sqrMag;
  return res;
}

GeoQuat geo_quat_norm(const GeoQuat q) {
  const f32 mag = geo_vector_mag((GeoVector){q.x, q.y, q.z, q.w});
  diag_assert(mag != 0);
  return (GeoQuat){q.x / mag, q.y / mag, q.z / mag, q.w / mag};
}

GeoQuat geo_quat_look(const GeoVector forward, const GeoVector upRef) {
  if (UNLIKELY(geo_vector_mag_sqr(forward) <= f32_epsilon)) {
    return geo_quat_ident;
  }
  if (UNLIKELY(geo_vector_mag_sqr(upRef) <= f32_epsilon)) {
    return geo_quat_ident;
  }

  const GeoVector dirForward     = geo_vector_norm(forward);
  GeoVector       dirRight       = geo_vector_cross3(upRef, dirForward);
  const f32       dirRightMagSqr = geo_vector_mag_sqr(dirRight);
  if (LIKELY(dirRightMagSqr > f32_epsilon)) {
    dirRight = geo_vector_div(dirRight, math_sqrt_f32(dirRightMagSqr));
  } else {
    dirRight = geo_right;
  }
  const GeoVector dirUp = geo_vector_cross3(dirForward, dirRight);
  const GeoMatrix m     = geo_matrix_rotate(dirRight, dirUp, dirForward);
  return geo_matrix_to_quat(&m);
}
