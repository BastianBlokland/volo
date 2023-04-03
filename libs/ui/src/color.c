#include "core_math.h"
#include "ui_color.h"

UiColor ui_color_lerp(const UiColor x, const UiColor y, const f32 t) {
  return ui_color(
      (u8)math_lerp(x.r, y.r, t),
      (u8)math_lerp(x.g, y.g, t),
      (u8)math_lerp(x.b, y.b, t),
      (u8)math_lerp(x.a, y.a, t));
}
