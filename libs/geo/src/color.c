#include "core_array.h"
#include "core_float.h"
#include "core_math.h"
#include "geo_color.h"

#define geo_color_simd_enable 1

#if geo_color_simd_enable
#include "simd_sse_internal.h"
#endif

GeoColor geo_color_get(const u64 idx) {
  // TODO: Consider replacing this with generating a random hue and then converting from hsv to rgb.
  static const GeoColor g_colors[] = {
      {1.0f, 0.0f, 0.0f, 1.0f},
      {1.0f, 1.0f, 0.0f, 1.0f},
      {0.5f, 0.5f, 0.0f, 1.0f},
      {0.75f, 0.75f, 0.75f, 1.0f},
      {0.0f, 1.0f, 1.0f, 1.0f},
      {0.0f, 1.0f, 0.0f, 1.0f},
      {0.5f, 0.0f, 0.0f, 1.0f},
      {0.0f, 0.0f, 1.0f, 1.0f},
      {0.0f, 0.5f, 0.5f, 1.0f},
      {0.0f, 0.0f, 0.5f, 1.0f},
      {1.0f, 0.0f, 1.0f, 1.0f},
      {0.0f, 0.5f, 0.0f, 1.0f},
      {0.5f, 0.5f, 0.5f, 1.0f},
      {0.5f, 0.0f, 0.5f, 1.0f},
      {1.0f, 0.5f, 0.0f, 1.0f},
  };
  return g_colors[idx % array_elems(g_colors)];
}

GeoColor geo_color_add(const GeoColor a, const GeoColor b) {
#if geo_color_simd_enable
  GeoColor res;
  simd_vec_store(simd_vec_add(simd_vec_load(a.data), simd_vec_load(b.data)), res.data);
  return res;
#else
  return geo_color(a.r + b.r, a.g + b.g, a.b + b.b, a.a + b.a);
#endif
}

GeoColor geo_color_mul(const GeoColor c, const f32 scalar) {
#if geo_color_simd_enable
  GeoColor res;
  simd_vec_store(simd_vec_mul(simd_vec_load(c.data), simd_vec_broadcast(scalar)), res.data);
  return res;
#else
  return geo_color(c.r * scalar, c.g * scalar, c.b * scalar, c.a * scalar);
#endif
}

GeoColor geo_color_div(const GeoColor c, const f32 scalar) {
#if geo_color_simd_enable
  GeoColor res;
  simd_vec_store(simd_vec_div(simd_vec_load(c.data), simd_vec_broadcast(scalar)), res.data);
  return res;
#else
  const f32 scalarInv = 1.0f / scalar;
  return geo_color(c.r * scalarInv, c.g * scalarInv, c.b * scalarInv, c.a * scalarInv);
#endif
}

GeoColor geo_color_lerp(const GeoColor x, const GeoColor y, const f32 t) {
#if geo_color_simd_enable
  const SimdVec vX = simd_vec_load(x.data);
  const SimdVec vY = simd_vec_load(y.data);
  const SimdVec vT = simd_vec_broadcast(t);
  GeoColor      res;
  simd_vec_store(simd_vec_add(vX, simd_vec_mul(simd_vec_sub(vY, vX), vT)), res.data);
  return res;
#else
  return geo_color(
      math_lerp(x.r, y.r, t),
      math_lerp(x.g, y.g, t),
      math_lerp(x.b, y.b, t),
      math_lerp(x.a, y.a, t));
#endif
}

GeoColor geo_color_bilerp(
    const GeoColor c1,
    const GeoColor c2,
    const GeoColor c3,
    const GeoColor c4,
    const f32      tX,
    const f32      tY) {
#if geo_color_simd_enable
  const SimdVec vec1  = simd_vec_load(c1.data);
  const SimdVec vec2  = simd_vec_load(c2.data);
  const SimdVec vec3  = simd_vec_load(c3.data);
  const SimdVec vec4  = simd_vec_load(c4.data);
  const SimdVec vecTX = simd_vec_broadcast(tX);
  const SimdVec vecTY = simd_vec_broadcast(tY);
  const SimdVec tmp1  = simd_vec_add(vec1, simd_vec_mul(simd_vec_sub(vec2, vec1), vecTX));
  const SimdVec tmp2  = simd_vec_add(vec3, simd_vec_mul(simd_vec_sub(vec4, vec3), vecTX));
  GeoColor      res;
  simd_vec_store(simd_vec_add(tmp1, simd_vec_mul(simd_vec_sub(tmp2, tmp1), vecTY)), res.data);
  return res;
#else
  return geo_color_lerp(geo_color_lerp(c1, c2, tX), geo_color_lerp(c3, c4, tX), tY);
#endif
}

void geo_color_pack_f16(const GeoColor color, f16 out[4]) {
  out[0] = float_f32_to_f16(color.r);
  out[1] = float_f32_to_f16(color.g);
  out[2] = float_f32_to_f16(color.b);
  out[3] = float_f32_to_f16(color.a);
}
