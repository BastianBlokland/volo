#include "core_array.h"
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
