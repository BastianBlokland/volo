#include "core_array.h"
#include "core_float.h"
#include "core_math.h"
#include "geo_color.h"

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
  };
  return g_colors[idx % array_elems(g_colors)];
}

GeoColor geo_color_lerp(const GeoColor x, const GeoColor y, const f32 t) {
  return (GeoColor){
      .r = math_lerp(x.r, y.r, t),
      .g = math_lerp(x.g, y.g, t),
      .b = math_lerp(x.b, y.b, t),
      .a = math_lerp(x.a, y.a, t),
  };
}

void geo_color_pack_f16(const GeoColor color, f16 out[4]) {
  out[0] = float_f32_to_f16(color.r);
  out[1] = float_f32_to_f16(color.g);
  out[2] = float_f32_to_f16(color.b);
  out[3] = float_f32_to_f16(color.a);
}
