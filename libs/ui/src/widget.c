#include "core_math.h"
#include "ui_canvas.h"
#include "ui_widget.h"

#include "shape_internal.h"

static UiColor ui_color_mult(const UiColor color, const f32 mult) {
  return ui_color(
      (u8)math_min(color.r * mult, u8_max),
      (u8)math_min(color.g * mult, u8_max),
      (u8)math_min(color.b * mult, u8_max),
      color.a);
}

bool ui_button_with_opts(UiCanvasComp* canvas, const UiButtonOpts* opts) {
  const UiId     id     = ui_canvas_next_id(canvas);
  const UiStatus status = ui_canvas_elem_status(canvas, id);

  ui_canvas_style_push(canvas);
  switch (status) {
  case UiStatus_Hovered:
    ui_canvas_style_color(canvas, ui_color_mult(opts->frameColor, 2));
    ui_canvas_style_outline(canvas, 5);
    break;
  case UiStatus_Pressed:
  case UiStatus_Activated:
    ui_canvas_style_color(canvas, ui_color_mult(opts->frameColor, 3));
    ui_canvas_style_outline(canvas, 3);
    break;
  case UiStatus_Idle:
    ui_canvas_style_color(canvas, opts->frameColor);
    ui_canvas_style_outline(canvas, 4);
    break;
  }
  ui_canvas_draw_glyph(canvas, ui_shape_circle, 20, UiFlags_Interactable);
  ui_canvas_style_pop(canvas);

  ui_canvas_style_push(canvas);
  switch (status) {
  case UiStatus_Hovered:
    ui_canvas_style_outline(canvas, 3);
    break;
  case UiStatus_Pressed:
  case UiStatus_Activated:
    ui_canvas_style_outline(canvas, 1);
    break;
  case UiStatus_Idle:
    break;
  }
  ui_canvas_draw_text(canvas, opts->label, opts->fontSize, UiTextAlign_MiddleCenter, UiFlags_None);
  ui_canvas_style_pop(canvas);

  return status == UiStatus_Activated;
}

static void ui_slider_bar(UiCanvasComp* canvas, const UiStatus status, const UiSliderOpts* opts) {
  ui_canvas_rect_push(canvas);
  ui_canvas_style_push(canvas);

  ui_canvas_rect_move(canvas, ui_vector(0, 0.5f), UiOrigin_Current, UiUnits_Current, Ui_Y);
  ui_canvas_rect_resize(canvas, ui_vector(0, opts->barHeight), UiUnits_Absolute, Ui_Y);
  ui_canvas_rect_move(canvas, ui_vector(0, -0.5f), UiOrigin_Current, UiUnits_Current, Ui_Y);

  ui_canvas_style_outline(canvas, 2);
  switch (status) {
  case UiStatus_Hovered:
  case UiStatus_Pressed:
  case UiStatus_Activated:
    ui_canvas_style_color(canvas, ui_color_mult(opts->barColor, 2));
    break;
  case UiStatus_Idle:
    ui_canvas_style_color(canvas, opts->barColor);
    break;
  }
  ui_canvas_draw_glyph(canvas, ui_shape_square, 0, UiFlags_Interactable);

  ui_canvas_style_pop(canvas);
  ui_canvas_rect_pop(canvas);
}

static void ui_slider_handle(
    UiCanvasComp* canvas, const UiStatus status, const f32 normValue, const UiSliderOpts* opts) {
  ui_canvas_rect_push(canvas);
  ui_canvas_style_push(canvas);

  const UiVector handleSize = ui_vector(opts->handleSize, opts->handleSize);
  ui_canvas_rect_move(canvas, ui_vector(normValue, 0.5f), UiOrigin_Current, UiUnits_Current, Ui_XY);
  ui_canvas_rect_resize(canvas, handleSize, UiUnits_Absolute, Ui_XY);
  ui_canvas_rect_move(canvas, ui_vector(-0.5f, -0.5f), UiOrigin_Current, UiUnits_Current, Ui_XY);

  switch (status) {
  case UiStatus_Hovered:
    ui_canvas_style_outline(canvas, 3);
    break;
  case UiStatus_Pressed:
    ui_canvas_style_outline(canvas, 1);
    break;
  case UiStatus_Activated:
  case UiStatus_Idle:
    break;
  }
  ui_canvas_draw_glyph(canvas, ui_shape_circle, 0, UiFlags_Interactable);

  ui_canvas_style_pop(canvas);
  ui_canvas_rect_pop(canvas);
}

bool ui_slider_with_opts(UiCanvasComp* canvas, f32* input, const UiSliderOpts* opts) {
  const UiId     barId    = ui_canvas_next_id(canvas);
  const UiId     handleId = barId + 1;
  const UiStatus status =
      math_max(ui_canvas_elem_status(canvas, barId), ui_canvas_elem_status(canvas, handleId));

  const UiRect   barRect  = ui_canvas_elem_rect(canvas, barId);
  const UiVector inputPos = ui_canvas_input_pos(canvas);

  f32 normValue;
  if (status >= UiStatus_Pressed) {
    normValue = math_unlerp(barRect.x, barRect.x + barRect.width, inputPos.x);
  } else {
    normValue = math_unlerp(opts->min, opts->max, *input);
  }
  normValue = math_clamp_f32(normValue, 0, 1);

  ui_slider_bar(canvas, status, opts);
  ui_slider_handle(canvas, status, normValue, opts);

  *input = math_lerp(opts->min, opts->max, normValue);
  return status >= UiStatus_Pressed;
}
