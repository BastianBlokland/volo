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

static void ui_panel_update_drag_and_resize(
    UiCanvasComp* canvas, UiPanelState* state, const UiId dragHandleId, const UiId resizeHandleId) {
  const UiVector canvasRes = ui_canvas_resolution(canvas);
  if (UNLIKELY(canvasRes.x <= 0 || canvasRes.y <= 0)) {
    return;
  }

  const UiVector inputDelta      = ui_canvas_input_delta(canvas);
  const f32      invCanvasWidth  = 1.0f / canvasRes.width;
  const f32      invCanvasHeight = 1.0f / canvasRes.height;

  if (state->flags & UiPanelFlags_Center) {
    state->position.x = 0.5f;
    state->position.y = 0.5f;
    state->flags &= ~UiPanelFlags_Center;
    return;
  }

  const f32 halfWidthFrac     = state->size.width * 0.5f * invCanvasWidth;
  const f32 halfMinWidthFrac  = state->minSize.width * 0.5f * invCanvasWidth;
  const f32 halfHeightFrac    = state->size.height * 0.5f * invCanvasHeight;
  const f32 halfMinHeightFrac = state->minSize.height * 0.5f * invCanvasHeight;
  const f32 topBarFrac        = (g_panelTopbarHeight + g_panelOutline) * invCanvasHeight;

  if (ui_canvas_elem_status(canvas, dragHandleId) == UiStatus_Pressed) {
    state->position.x += inputDelta.x * invCanvasWidth;
    state->position.y += inputDelta.y * invCanvasHeight;
  }
  if (ui_canvas_elem_status(canvas, resizeHandleId) == UiStatus_Pressed) {
    // Apply the x resizing (clamped to the canvas).
    f32 xDeltaFrac = inputDelta.x * invCanvasWidth;
    if (state->position.x + halfWidthFrac + xDeltaFrac > 1) {
      xDeltaFrac += 1 - (state->position.x + halfWidthFrac + xDeltaFrac);
    }
    if (halfWidthFrac + xDeltaFrac < halfMinWidthFrac) {
      xDeltaFrac += halfMinWidthFrac - (halfWidthFrac + xDeltaFrac);
    }
    state->position.x += xDeltaFrac * 0.5f;
    state->size.x += xDeltaFrac * canvasRes.width;

    // Apply the y resizing (clamped to the canvas).
    f32 yDeltaFrac = inputDelta.y * invCanvasHeight;
    if (state->position.y - halfHeightFrac + yDeltaFrac < 0) {
      yDeltaFrac -= state->position.y - halfHeightFrac + yDeltaFrac;
    }
    if (halfHeightFrac - yDeltaFrac < halfMinHeightFrac) {
      yDeltaFrac -= halfMinHeightFrac - (halfHeightFrac - yDeltaFrac);
    }
    state->position.y += yDeltaFrac * 0.5f;
    state->size.y -= yDeltaFrac * canvasRes.height;
  }

  // Clamp the position to the canvas.
  if (state->position.x >= 1 - halfWidthFrac) {
    state->position.x = 1 - halfWidthFrac;
  } else if (state->position.x <= halfWidthFrac) {
    state->position.x = halfWidthFrac;
  }
  if (state->position.y <= halfHeightFrac) {
    state->position.y = halfHeightFrac;
  } else if (state->position.y >= 1 - halfHeightFrac - topBarFrac) {
    state->position.y = 1 - halfHeightFrac - topBarFrac;
  }
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

static void ui_panel_resize_handle(UiCanvasComp* canvas) {
  ui_layout_push(canvas);
  ui_style_push(canvas);
  ui_layout_inner(canvas, UiBase_Current, UiAlign_BottomRight, ui_vector(25, 25), UiBase_Absolute);
  ui_style_layer(canvas, UiLayer_Invisible);
  const UiId handleId = ui_canvas_draw_glyph(canvas, UiShape_Square, 0, UiFlags_Interactable);
  ui_layout_pop(canvas);
  ui_style_pop(canvas);

  if (ui_canvas_elem_status(canvas, handleId) >= UiStatus_Hovered) {
    ui_canvas_interact_type(canvas, UiInteractType_Resize);
  }
}

UiPanelState ui_panel_init(const UiVector size) {
  return (UiPanelState){
      .flags   = UiPanelFlags_Center,
      .size    = size,
      .minSize = ui_vector(100, 100),
  };
}

void ui_panel_begin_with_opts(UiCanvasComp* canvas, UiPanelState* state, const UiPanelOpts* opts) {
  diag_assert_msg(!(state->flags & UiPanelFlags_Drawing), "The given panel is already being drawn");
  state->flags |= UiPanelFlags_Drawing;

  const UiId resizeHandleId = ui_canvas_id_peek(canvas);
  const UiId dragHandleId   = resizeHandleId + 1;
  ui_panel_update_drag_and_resize(canvas, state, dragHandleId, resizeHandleId);

  ui_layout_move(canvas, state->position, UiBase_Canvas, Ui_XY);
  ui_layout_resize(canvas, UiAlign_MiddleCenter, state->size, UiBase_Absolute, Ui_XY);

  ui_panel_resize_handle(canvas);
  ui_panel_topbar(canvas, state, opts);
  ui_panel_background(canvas);

  ui_layout_container_push(canvas);
}

void ui_panel_end(UiCanvasComp* canvas, UiPanelState* state) {
  diag_assert_msg(state->flags & UiPanelFlags_Drawing, "The given panel is not being drawn");
  state->flags &= ~UiPanelFlags_Drawing;

  ui_layout_container_pop(canvas);
}
