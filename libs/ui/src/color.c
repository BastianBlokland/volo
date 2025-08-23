#include "core/math.h"
#include "ui/color.h"

UiColor ui_color_lerp(const UiColor x, const UiColor y, const f32 t) {
  return ui_color(
      (u8)math_lerp(x.r, y.r, t),
      (u8)math_lerp(x.g, y.g, t),
      (u8)math_lerp(x.b, y.b, t),
      (u8)math_lerp(x.a, y.a, t));
}

UiColor ui_color_mul(const UiColor c, const f32 scalar) {
  return ui_color(
      (u8)math_min(c.r * scalar, u8_max),
      (u8)math_min(c.g * scalar, u8_max),
      (u8)math_min(c.b * scalar, u8_max),
      c.a);
}
