#include "core_diag.h"
#include "core_math.h"
#include "ui_canvas.h"
#include "ui_layout.h"
#include "ui_shape.h"
#include "ui_style.h"
#include "ui_widget.h"

static UiColor ui_color_mult(const UiColor color, const f32 mult) {
  return ui_color(
      (u8)math_min(color.r * mult, u8_max),
      (u8)math_min(color.g * mult, u8_max),
      (u8)math_min(color.b * mult, u8_max),
      color.a);
}

bool ui_button_with_opts(UiCanvasComp* canvas, const UiButtonOpts* opts) {
  const UiId     id     = ui_canvas_id_peek(canvas);
  const UiStatus status = ui_canvas_elem_status(canvas, id);

  ui_style_push(canvas);
  switch (status) {
  case UiStatus_Hovered:
    ui_style_color(canvas, ui_color_mult(opts->frameColor, 2));
    ui_style_outline(canvas, 5);
    break;
  case UiStatus_Pressed:
  case UiStatus_Activated:
    ui_style_color(canvas, ui_color_mult(opts->frameColor, 3));
    ui_style_outline(canvas, 3);
    break;
  case UiStatus_Idle:
    ui_style_color(canvas, opts->frameColor);
    ui_style_outline(canvas, 4);
    break;
  }
  ui_canvas_draw_glyph(canvas, UiShape_Circle, 15, UiFlags_Interactable);
  ui_style_pop(canvas);

  ui_style_push(canvas);
  switch (status) {
  case UiStatus_Hovered:
    ui_style_outline(canvas, 3);
    break;
  case UiStatus_Pressed:
  case UiStatus_Activated:
    ui_style_outline(canvas, 1);
    break;
  case UiStatus_Idle:
    break;
  }
  ui_canvas_draw_text(canvas, opts->label, opts->fontSize, UiAlign_MiddleCenter, UiFlags_None);
  ui_style_pop(canvas);

  if (!string_is_empty(opts->tooltip)) {
    ui_tooltip(canvas, id, opts->tooltip);
  }

  return status == UiStatus_Activated;
}

static void ui_slider_bar(UiCanvasComp* canvas, const UiStatus status, const UiSliderOpts* opts) {
  ui_layout_push(canvas);
  ui_style_push(canvas);

  ui_layout_move_to(canvas, UiBase_Current, UiAlign_MiddleLeft, Ui_Y);
  ui_layout_resize(
      canvas, UiAlign_MiddleLeft, ui_vector(0, opts->barHeight), UiBase_Absolute, Ui_Y);

  ui_style_outline(canvas, 2);
  switch (status) {
  case UiStatus_Hovered:
  case UiStatus_Pressed:
  case UiStatus_Activated:
    ui_style_color(canvas, ui_color_mult(opts->barColor, 2));
    break;
  case UiStatus_Idle:
    ui_style_color(canvas, opts->barColor);
    break;
  }
  ui_canvas_draw_glyph(canvas, UiShape_Square, 0, UiFlags_Interactable);

  ui_style_pop(canvas);
  ui_layout_pop(canvas);
}

static void ui_slider_handle(
    UiCanvasComp* canvas, const UiStatus status, const f32 normValue, const UiSliderOpts* opts) {
  ui_layout_push(canvas);
  ui_style_push(canvas);

  const UiVector handleSize = ui_vector(opts->handleSize, opts->handleSize);
  ui_layout_move(canvas, ui_vector(normValue, 0.5f), UiBase_Current, Ui_XY);
  ui_layout_resize(canvas, UiAlign_MiddleCenter, handleSize, UiBase_Absolute, Ui_XY);

  switch (status) {
  case UiStatus_Hovered:
    ui_style_outline(canvas, 3);
    break;
  case UiStatus_Pressed:
    ui_style_outline(canvas, 1);
    break;
  case UiStatus_Activated:
  case UiStatus_Idle:
    break;
  }
  ui_canvas_draw_glyph(canvas, UiShape_Circle, 0, UiFlags_Interactable);

  ui_style_pop(canvas);
  ui_layout_pop(canvas);
}

static void ui_slider_label(UiCanvasComp* canvas, const f32 normValue, const UiSliderOpts* opts) {
  ui_layout_push(canvas);
  static const UiVector g_maxSize  = {100, 100};
  static const u16      g_fontSize = 15;

  ui_layout_move(canvas, ui_vector(normValue, 0.5f), UiBase_Current, Ui_XY);
  ui_layout_resize(canvas, UiAlign_MiddleCenter, g_maxSize, UiBase_Absolute, Ui_XY);
  ui_layout_move_dir(canvas, Ui_Up, opts->handleSize + 1, UiBase_Absolute);

  const f32    value = math_lerp(opts->min, opts->max, normValue);
  const String label = fmt_write_scratch("{}", fmt_float(value, .maxDecDigits = 2));
  ui_canvas_draw_text(canvas, label, g_fontSize, UiAlign_MiddleCenter, UiFlags_None);

  ui_layout_pop(canvas);
}

bool ui_slider_with_opts(UiCanvasComp* canvas, f32* input, const UiSliderOpts* opts) {
  const UiId     barId    = ui_canvas_id_peek(canvas);
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

  if (status >= UiStatus_Hovered) {
    ui_slider_label(canvas, normValue, opts);
  } else {
    ui_canvas_id_skip(canvas);
  }

  if (!string_is_empty(opts->tooltip)) {
    ui_tooltip(canvas, barId, opts->tooltip);
    ui_tooltip(canvas, handleId, opts->tooltip);
  }

  *input = math_lerp(opts->min, opts->max, normValue);
  return status >= UiStatus_Pressed;
}

static void ui_toggle_check(UiCanvasComp* canvas, const UiStatus status, const UiToggleOpts* opts) {
  const UiVector size = {opts->size * 1.4f, opts->size * 1.4f};
  ui_layout_inner(canvas, UiBase_Current, UiAlign_MiddleCenter, size, UiBase_Absolute);
  ui_layout_move_dir(canvas, Ui_Right, 0.1f, UiBase_Current);
  ui_layout_move_dir(canvas, Ui_Up, 0.1f, UiBase_Current);

  ui_style_push(canvas);

  ui_style_outline(canvas, status == UiStatus_Hovered ? 4 : 3);
  ui_canvas_draw_glyph(canvas, UiShape_Check, 0, UiFlags_None);

  ui_style_pop(canvas);
}

bool ui_toggle_with_opts(UiCanvasComp* canvas, bool* input, const UiToggleOpts* opts) {
  const UiId     id     = ui_canvas_id_peek(canvas);
  const UiStatus status = ui_canvas_elem_status(canvas, id);
  const UiVector size   = {opts->size, opts->size};

  if (status == UiStatus_Activated) {
    *input ^= true;
  }
  ui_layout_push(canvas);
  ui_layout_inner(canvas, UiBase_Current, UiAlign_MiddleLeft, size, UiBase_Absolute);

  ui_style_push(canvas);
  switch (status) {
  case UiStatus_Hovered:
    ui_style_color(canvas, ui_color_mult(opts->bgColor, 2));
    ui_style_outline(canvas, 3);
    break;
  case UiStatus_Pressed:
  case UiStatus_Activated:
    ui_style_color(canvas, ui_color_mult(opts->bgColor, 3));
    ui_style_outline(canvas, 2);
    break;
  case UiStatus_Idle:
    ui_style_color(canvas, opts->bgColor);
    ui_style_outline(canvas, 2);
    break;
  }
  ui_canvas_draw_glyph(canvas, UiShape_Circle, 5, UiFlags_Interactable);

  ui_style_pop(canvas);

  if (*input) {
    ui_toggle_check(canvas, status, opts);
  } else {
    ui_canvas_id_skip(canvas);
  }

  if (!string_is_empty(opts->tooltip)) {
    ui_tooltip(canvas, id, opts->tooltip);
  }

  ui_layout_pop(canvas);
  return status == UiStatus_Activated;
}

static void ui_tooltip_background(
    UiCanvasComp* canvas, const UiDir dir, const UiAlign align, const UiRect textRect) {
  const UiVector size = ui_vector(textRect.width + 20, textRect.height + 10);

  ui_layout_inner(canvas, UiBase_Cursor, align, size, UiBase_Absolute);
  ui_layout_move_dir(canvas, dir, 15, UiBase_Absolute);

  ui_style_color(canvas, ui_color_white);
  ui_style_outline(canvas, 4);

  ui_canvas_draw_glyph(canvas, UiShape_Circle, 5, UiFlags_None);
}

static void ui_tooltip_text(
    UiCanvasComp*        canvas,
    const UiDir          dir,
    const UiAlign        align,
    const String         text,
    const UiTooltipOpts* opts) {

  ui_layout_inner(canvas, UiBase_Cursor, align, opts->maxSize, UiBase_Absolute);
  ui_layout_move_dir(canvas, dir, 25, UiBase_Absolute);
  ui_layout_move_dir(canvas, Ui_Down, 5, UiBase_Absolute);

  ui_style_color(canvas, ui_color_black);
  ui_style_outline(canvas, 0);

  ui_canvas_draw_text(canvas, text, opts->fontSize, align, UiFlags_None);
}

static UiDir ui_tooltip_dir(UiCanvasComp* canvas) {
  const f32 halfWindow = ui_canvas_window_size(canvas).x * 0.5f;
  return ui_canvas_input_pos(canvas).x > halfWindow ? Ui_Left : Ui_Right;
}

bool ui_tooltip_with_opts(
    UiCanvasComp* canvas, const UiId id, const String text, const UiTooltipOpts* opts) {

  const bool showTooltip = ui_canvas_elem_status(canvas, id) == UiStatus_Hovered &&
                           ui_canvas_elem_status_duration(canvas, id) >= time_second;
  if (!showTooltip) {
    ui_canvas_id_skip(canvas);
    ui_canvas_id_skip(canvas);
    return false;
  }

  const UiDir dir = ui_tooltip_dir(canvas);
  UiAlign     align;
  switch (dir) {
  case Ui_Left:
    align = UiAlign_TopRight;
    break;
  case Ui_Right:
    align = UiAlign_TopLeft;
    break;
  case Ui_Up:
  case Ui_Down:
    diag_crash();
  }

  const UiId   backgroundId = ui_canvas_id_peek(canvas);
  const UiId   textId       = backgroundId + 1;
  const UiRect textRect     = ui_canvas_elem_rect(canvas, textId);

  ui_layout_push(canvas);
  ui_style_push(canvas);

  ui_style_layer(canvas, UiLayer_Overlay);

  ui_tooltip_background(canvas, dir, align, textRect);
  ui_tooltip_text(canvas, dir, align, text, opts);

  ui_style_pop(canvas);
  ui_layout_pop(canvas);
  return true;
}
