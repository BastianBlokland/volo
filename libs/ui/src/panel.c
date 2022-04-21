#include "core_diag.h"
#include "core_math.h"
#include "ui_canvas.h"
#include "ui_layout.h"
#include "ui_panel.h"
#include "ui_shape.h"
#include "ui_style.h"
#include "ui_widget.h"

static const f32 g_panelTopbarHeight = 23;
static const u32 g_panelOutline      = 3;

static void ui_panel_clamp_to_canvas(UiPanelState* state, const UiVector canvasRes) {
  if (state->flags & UiPanelFlags_Center) {
    state->position.x = 0.5f;
    state->position.y = 0.5f;
  } else {
    const f32 halfWidthFrac  = (state->size.width * 0.5f) / canvasRes.width;
    const f32 halfHeightFrac = (state->size.height * 0.5f) / canvasRes.height;
    const f32 topBarFrac     = (g_panelTopbarHeight + g_panelOutline) / canvasRes.height;

    state->position.x = math_clamp_f32(state->position.x, halfWidthFrac, 1 - halfWidthFrac);
    state->position.y =
        math_clamp_f32(state->position.y, halfHeightFrac, 1 - halfHeightFrac - topBarFrac);
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
  if (status >= UiStatus_Hovered) {
    ui_canvas_interact_type(canvas, UiInteractType_Action);
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

  ui_style_outline(canvas, g_panelOutline);
  ui_canvas_draw_glyph(canvas, UiShape_Square, 10, UiFlags_Interactable);

  ui_style_pop(canvas);
}

static void ui_panel_topbar(UiCanvasComp* canvas, UiPanelState* state, const UiPanelOpts* opts) {

  ui_layout_push(canvas);

  ui_layout_move_dir(canvas, Ui_Up, 1, UiBase_Current);
  ui_layout_move_dir(canvas, Ui_Up, g_panelOutline, UiBase_Absolute);
  ui_layout_resize(
      canvas, UiAlign_BottomCenter, ui_vector(0, g_panelTopbarHeight), UiBase_Absolute, Ui_Y);

  ui_panel_topbar_background(canvas);
  ui_panel_topbar_title(canvas, opts);
  ui_panel_topbar_close_button(canvas, state);

  ui_layout_pop(canvas);
}

static void ui_panel_background(UiCanvasComp* canvas) {
  ui_style_push(canvas);

  ui_style_color(canvas, ui_color(64, 64, 64, 230));
  ui_style_outline(canvas, g_panelOutline);

  ui_canvas_draw_glyph(canvas, UiShape_Square, 10, UiFlags_Interactable);

  ui_style_pop(canvas);
}

UiPanelState ui_panel_init(const UiVector size) {
  return (UiPanelState){
      .flags = UiPanelFlags_Center,
      .size  = size,
  };
}

void ui_panel_begin_with_opts(UiCanvasComp* canvas, UiPanelState* state, const UiPanelOpts* opts) {
  diag_assert_msg(!(state->flags & UiPanelFlags_Drawing), "The given panel is already being drawn");
  state->flags |= UiPanelFlags_Drawing;

  const UiVector inputDelta = ui_canvas_input_delta(canvas);
  const UiVector canvasRes  = ui_canvas_resolution(canvas);
  const UiId     topbarId   = ui_canvas_id_peek(canvas);
  const UiStatus dragStatus = ui_canvas_elem_status(canvas, topbarId);
  if (dragStatus == UiStatus_Pressed) {
    state->position.x += inputDelta.x / canvasRes.width;
    state->position.y += inputDelta.y / canvasRes.height;
  }
  if (dragStatus >= UiStatus_Hovered) {
    ui_canvas_interact_type(canvas, UiInteractType_Drag);
  }
  if (canvasRes.x > 0 && canvasRes.y > 0) {
    ui_panel_clamp_to_canvas(state, canvasRes);
  }

  ui_layout_move(canvas, state->position, UiBase_Canvas, Ui_XY);
  ui_layout_resize(canvas, UiAlign_MiddleCenter, state->size, UiBase_Absolute, Ui_XY);
  ui_panel_topbar(canvas, state, opts);
  ui_panel_background(canvas);

  ui_layout_container_push(canvas);
}

void ui_panel_end(UiCanvasComp* canvas, UiPanelState* state) {
  diag_assert_msg(state->flags & UiPanelFlags_Drawing, "The given panel is not being drawn");
  state->flags &= ~UiPanelFlags_Drawing;

  ui_layout_container_pop(canvas);
}
