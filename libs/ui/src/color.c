#include "core/math.h"
#include "ui/color.h"

UiColor ui_color_from_f32(const f32 r, const f32 g, const f32 b, const f32 a) {
  static const f32 g_u8MaxPlusOneRoundDown = 255.999f;
  return (UiColor){
      .r = (u8)(r * g_u8MaxPlusOneRoundDown),
      .g = (u8)(g * g_u8MaxPlusOneRoundDown),
      .b = (u8)(b * g_u8MaxPlusOneRoundDown),
      .a = (u8)(a * g_u8MaxPlusOneRoundDown),
  };
}

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

UiColor ui_color_add(const UiColor a, const UiColor b) {
  return ui_color(
      (u8)math_min(a.r + b.r, u8_max),
      (u8)math_min(a.g + b.g, u8_max),
      (u8)math_min(a.b + b.b, u8_max),
      (u8)math_min(a.a + b.a, u8_max));
}
