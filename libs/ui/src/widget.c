#include "core_math.h"
#include "ui_canvas.h"
#include "ui_widget.h"

#include "shape_internal.h"

static UiColor ui_widget_color_mult(const UiColor color, const f32 mult) {
  return ui_color(
      (u8)math_min(color.r * mult, u8_max),
      (u8)math_min(color.g * mult, u8_max),
      (u8)math_min(color.b * mult, u8_max),
      color.a);
}

bool ui_widget_button(UiCanvasComp* canvas, const UiWidgetButtonOpts* opts) {
  const UiId     id     = ui_canvas_next_id(canvas);
  const UiStatus status = ui_canvas_status(canvas, id);

  switch (status) {
  case UiStatus_Hovered:
    ui_canvas_style(canvas, ui_widget_color_mult(opts->frameColor, 2), 5);
    break;
  case UiStatus_Down:
  case UiStatus_Activated:
    ui_canvas_style(canvas, ui_widget_color_mult(opts->frameColor, 3), 3);
    break;
  case UiStatus_Idle:
    ui_canvas_style(canvas, opts->frameColor, 4);
    break;
  }
  ui_canvas_draw_glyph(canvas, ui_shape_circle, 20, UiFlags_Interactable);

  switch (status) {
  case UiStatus_Hovered:
    ui_canvas_style(canvas, opts->labelColor, 4);
    break;
  case UiStatus_Down:
  case UiStatus_Activated:
    ui_canvas_style(canvas, opts->labelColor, 1);
    break;
  case UiStatus_Idle:
    ui_canvas_style(canvas, opts->labelColor, 2);
    break;
  }
  ui_canvas_draw_text(canvas, opts->label, opts->fontSize, UiTextAlign_MiddleCenter, UiFlags_None);

  return status == UiStatus_Activated;
}
