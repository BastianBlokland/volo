#include "core_diag.h"
#include "core_math.h"
#include "ui_canvas.h"
#include "ui_layout.h"
#include "ui_panel.h"
#include "ui_shape.h"
#include "ui_style.h"
#include "ui_widget.h"

static void ui_panel_clamp_to_canvas(UiCanvasComp* canvas, UiPanelState* state) {
  const UiVector canvasRes = ui_canvas_resolution(canvas);
  if (canvasRes.x < 1 || canvasRes.y < 1) {
    return;
  }

  if (state->flags & UiPanelFlags_Center) {
    state->rect.pos.x = (canvasRes.x - state->rect.width) * 0.5f;
    state->rect.pos.y = (canvasRes.y - state->rect.height) * 0.5f;
  } else {
    state->rect.pos.x = math_clamp_f32(state->rect.pos.x, 0, canvasRes.x - state->rect.width);
    state->rect.pos.y = math_clamp_f32(state->rect.pos.y, 0, canvasRes.y - state->rect.height);
  }
  state->flags &= ~UiPanelFlags_Center;
}

static void ui_panel_topbar_title(UiCanvasComp* canvas, const UiPanelOpts* opts) {
  ui_layout_push(canvas);

  ui_layout_move_dir(canvas, Ui_Right, 5, UiBase_Absolute);
  ui_label(canvas, opts->title, .fontSize = 18);

  ui_layout_pop(canvas);
}

static void ui_panel_topbar_close_button(UiCanvasComp* canvas, UiPanelState* state) {
  ui_layout_push(canvas);
  ui_style_push(canvas);

  const UiId     id     = ui_canvas_id_peek(canvas);
  const UiStatus status = ui_canvas_elem_status(canvas, id);

  if (status == UiStatus_Activated) {
    state->flags |= UiPanelFlags_Close;
  }

  UiVector size;
  switch (status) {
  case UiStatus_Hovered:
    ui_style_outline(canvas, 3);
    size = ui_vector(23, 23);
    break;
  case UiStatus_Pressed:
  case UiStatus_Activated:
    ui_style_outline(canvas, 1);
    size = ui_vector(18, 18);
    break;
  case UiStatus_Idle:
    ui_style_outline(canvas, 2);
    size = ui_vector(20, 20);
    break;
  }

  ui_layout_move(canvas, ui_vector(1, 0.5), UiBase_Current, Ui_XY);
  ui_layout_move_dir(canvas, Ui_Left, 12, UiBase_Absolute);
  ui_layout_resize(canvas, UiAlign_MiddleCenter, size, UiBase_Absolute, Ui_XY);

  ui_canvas_draw_glyph(canvas, UiShape_Close, 0, UiFlags_Interactable);

  ui_tooltip(canvas, id, string_lit("Close"));

  ui_style_pop(canvas);
  ui_layout_pop(canvas);
}

static void ui_panel_topbar_background(UiCanvasComp* canvas) {
  ui_style_push(canvas);

  const UiId id = ui_canvas_id_peek(canvas);
  switch (ui_canvas_elem_status(canvas, id)) {
  case UiStatus_Pressed:
  case UiStatus_Activated:
    ui_style_color(canvas, ui_color(32, 32, 32, 240));
    break;
  case UiStatus_Hovered:
  case UiStatus_Idle:
    ui_style_color(canvas, ui_color(8, 8, 8, 240));
    break;
  }

  ui_style_outline(canvas, 3);
  ui_canvas_draw_glyph(canvas, UiShape_Square, 10, UiFlags_Interactable);

  ui_style_pop(canvas);
}

static void ui_panel_topbar(UiCanvasComp* canvas, UiPanelState* state, const UiPanelOpts* opts) {
  const UiStatus status = ui_canvas_elem_status(canvas, ui_canvas_id_peek(canvas));
  if (status == UiStatus_Pressed) {
    const UiVector inputDelta = ui_canvas_input_delta(canvas);
    state->rect.pos.x += inputDelta.x;
    state->rect.pos.y += inputDelta.y;
  }
  ui_panel_clamp_to_canvas(canvas, state);

  ui_layout_push(canvas);

  ui_layout_set(canvas, state->rect);
  ui_layout_move(canvas, ui_vector(0, 1), UiBase_Current, Ui_Y);
  ui_layout_resize(canvas, UiAlign_TopCenter, ui_vector(0, 23), UiBase_Absolute, Ui_Y);

  ui_panel_topbar_background(canvas);
  ui_panel_topbar_title(canvas, opts);
  ui_panel_topbar_close_button(canvas, state);

  ui_layout_pop(canvas);
}

static void ui_panel_background(UiCanvasComp* canvas) {
  ui_style_push(canvas);

  ui_style_color(canvas, ui_color(64, 64, 64, 230));
  ui_style_outline(canvas, 3);

  ui_canvas_draw_glyph(canvas, UiShape_Square, 10, UiFlags_Interactable);

  ui_style_pop(canvas);
}

UiPanelState ui_panel_init(const UiVector size) {
  return (UiPanelState){
      .flags     = UiPanelFlags_Center,
      .rect.size = size,
  };
}

void ui_panel_begin_with_opts(UiCanvasComp* canvas, UiPanelState* state, const UiPanelOpts* opts) {
  diag_assert_msg(!(state->flags & UiPanelFlags_Drawing), "The given panel is already being drawn");
  state->flags |= UiPanelFlags_Drawing;

  ui_panel_topbar(canvas, state, opts);

  const UiRect containerRect = {
      .pos  = state->rect.pos,
      .size = ui_vector(state->rect.size.width, state->rect.size.height - 26),
  };
  ui_layout_set(canvas, containerRect);
  ui_panel_background(canvas);

  ui_layout_container_push(canvas);
}

void ui_panel_end(UiCanvasComp* canvas, UiPanelState* state) {
  diag_assert_msg(state->flags & UiPanelFlags_Drawing, "The given panel is not being drawn");
  state->flags &= ~UiPanelFlags_Drawing;

  ui_layout_container_pop(canvas);
}
