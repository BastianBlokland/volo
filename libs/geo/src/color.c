#include "core_array.h"
#include "geo_color.h"

GeoColor geo_color_get(const u64 idx) {
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
