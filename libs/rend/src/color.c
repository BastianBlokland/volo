#include "core_array.h"
#include "rend_color.h"

RendColor rend_color_get(const u64 idx) {
  static const RendColor colors[] = {
      rend_red,
      rend_yellow,
      rend_olive,
      rend_silver,
      rend_aqua,
      rend_lime,
      rend_maroon,
      rend_blue,
      rend_teal,
      rend_navy,
      rend_fuchsia,
      rend_green,
      rend_gray,
      rend_purple,
  };
  return colors[idx % array_elems(colors)];
}
