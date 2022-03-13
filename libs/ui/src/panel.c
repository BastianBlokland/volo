#include "core_math.h"
#include "ui_canvas.h"
#include "ui_layout.h"
#include "ui_panel.h"
#include "ui_shape.h"
#include "ui_style.h"
#include "ui_widget.h"

static void ui_panel_clamp_to_window(UiCanvasComp* canvas, UiPanel* panel) {
  const UiVector windowSize = ui_canvas_window_size(canvas);
  panel->rect.pos.x = math_clamp_f32(panel->rect.pos.x, 0, windowSize.x - panel->rect.width);
  panel->rect.pos.y = math_clamp_f32(panel->rect.pos.y, 0, windowSize.y - panel->rect.height);
}

static void ui_panel_topbar_title(UiCanvasComp* canvas, const UiPanelOpts* opts) {
  ui_layout_push(canvas);
  ui_style_push(canvas);

  ui_style_outline(canvas, 2);
  ui_layout_move_dir(canvas, Ui_Right, 5, UiBase_Absolute);
  ui_canvas_draw_text(canvas, opts->title, 18, UiAlign_MiddleLeft, UiFlags_None);

  ui_style_pop(canvas);
  ui_layout_pop(canvas);
}

static void ui_panel_topbar_close_button(UiCanvasComp* canvas, UiPanel* panel) {
  ui_layout_push(canvas);
  ui_style_push(canvas);

  const UiId     id     = ui_canvas_id_peek(canvas);
  const UiStatus status = ui_canvas_elem_status(canvas, id);

  if (status == UiStatus_Activated) {
    panel->flags |= UiPanelFlags_Close;
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

static void ui_panel_topbar_background(UiCanvasComp* canvas, UiPanel* panel) {
  ui_style_push(canvas);

  const UiId id = ui_canvas_id_peek(canvas);
  switch (ui_canvas_elem_status(canvas, id)) {
  case UiStatus_Pressed:
  case UiStatus_Activated:
    ui_style_color(canvas, ui_color(32, 32, 32, 240));
    panel->flags |= UiPanelFlags_ToFront;
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

static void ui_panel_topbar(UiCanvasComp* canvas, UiPanel* panel, const UiPanelOpts* opts) {
  const UiStatus status = ui_canvas_elem_status(canvas, ui_canvas_id_peek(canvas));
  if (status == UiStatus_Pressed) {
    const UiVector inputDelta = ui_canvas_input_delta(canvas);
    panel->rect.pos.x += inputDelta.x;
    panel->rect.pos.y += inputDelta.y;
  }
  ui_panel_clamp_to_window(canvas, panel);

  ui_layout_push(canvas);

  ui_layout_set(canvas, panel->rect);
  ui_layout_move(canvas, ui_vector(0, 1), UiBase_Current, Ui_Y);
  ui_layout_resize(canvas, UiAlign_TopCenter, ui_vector(0, 23), UiBase_Absolute, Ui_Y);

  ui_panel_topbar_background(canvas, panel);
  ui_panel_topbar_title(canvas, opts);
  ui_panel_topbar_close_button(canvas, panel);

  ui_layout_pop(canvas);
}

static void ui_panel_background(UiCanvasComp* canvas, UiPanel* panel) {
  ui_style_push(canvas);

  ui_style_color(canvas, ui_color(64, 64, 64, 230));
  ui_style_outline(canvas, 3);

  const UiId id = ui_canvas_id_peek(canvas);
  if (ui_canvas_elem_status(canvas, id) >= UiStatus_Pressed) {
    panel->flags |= UiPanelFlags_ToFront;
  }

  ui_canvas_draw_glyph(canvas, UiShape_Square, 10, UiFlags_Interactable);

  ui_style_pop(canvas);
}

void ui_panel_begin_with_opts(UiCanvasComp* canvas, UiPanel* panel, const UiPanelOpts* opts) {
  ui_panel_topbar(canvas, panel, opts);

  ui_layout_push(canvas);

  const UiRect containerRect = {
      .pos  = panel->rect.pos,
      .size = ui_vector(panel->rect.size.width, panel->rect.size.height - 26),
  };
  ui_layout_set(canvas, containerRect);
  ui_panel_background(canvas, panel);

  ui_layout_container_push(canvas);
  ui_layout_pop(canvas);
}

void ui_panel_end(UiCanvasComp* canvas, UiPanel* panel) {
  (void)panel;
  ui_layout_container_pop(canvas);
}
