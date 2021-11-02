#include "core_array.h"
#include "rend_color.h"

RendColor rend_color_get(const u64 idx) {
  static const RendColor colors[] = {
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
  return colors[idx % array_elems(colors)];
}
