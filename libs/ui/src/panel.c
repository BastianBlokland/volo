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
    UiCanvasComp* canvas, UiPanel* panel, const UiId dragHandleId, const UiId resizeHandleId) {
  const UiVector canvasRes = ui_canvas_resolution(canvas);
  if (UNLIKELY(canvasRes.x <= 0 || canvasRes.y <= 0)) {
    return;
  }

  const UiVector inputDelta      = ui_canvas_input_delta(canvas);
  const f32      invCanvasWidth  = 1.0f / canvasRes.width;
  const f32      invCanvasHeight = 1.0f / canvasRes.height;

  const f32 halfWidthFrac     = panel->size.width * 0.5f * invCanvasWidth;
  const f32 halfMinWidthFrac  = panel->minSize.width * 0.5f * invCanvasWidth;
  const f32 halfHeightFrac    = panel->size.height * 0.5f * invCanvasHeight;
  const f32 halfMinHeightFrac = panel->minSize.height * 0.5f * invCanvasHeight;

  if (ui_canvas_elem_status(canvas, dragHandleId) == UiStatus_Pressed) {
    panel->position.x += inputDelta.x * invCanvasWidth;
    panel->position.y += inputDelta.y * invCanvasHeight;
  }

  // Clamp the position to the canvas.
  const f32 topBarFrac = (g_panelTopbarHeight + g_panelOutline) * invCanvasHeight;
  if (panel->position.x >= 1 - halfWidthFrac) {
    panel->position.x = 1 - halfWidthFrac;
  } else if (panel->position.x <= halfWidthFrac) {
    panel->position.x = halfWidthFrac;
  }
  if (panel->position.y <= halfHeightFrac) {
    panel->position.y = halfHeightFrac;
  } else if (panel->position.y >= 1 - halfHeightFrac - topBarFrac) {
    panel->position.y = 1 - halfHeightFrac - topBarFrac;
  }

  if (ui_canvas_elem_status(canvas, resizeHandleId) == UiStatus_Pressed) {
    // Apply the x resizing (clamped to the canvas).
    f32 xDeltaFrac = inputDelta.x * invCanvasWidth;
    if (panel->position.x + halfWidthFrac + xDeltaFrac > 1) {
      xDeltaFrac += 1 - (panel->position.x + halfWidthFrac + xDeltaFrac);
    }
    if (halfWidthFrac + xDeltaFrac < halfMinWidthFrac) {
      xDeltaFrac += halfMinWidthFrac - (halfWidthFrac + xDeltaFrac);
    }
    panel->position.x += xDeltaFrac * 0.5f;
    panel->size.x += xDeltaFrac * canvasRes.width;

    // Apply the y resizing (clamped to the canvas).
    f32 yDeltaFrac = inputDelta.y * invCanvasHeight;
    if (panel->position.y - halfHeightFrac + yDeltaFrac < 0) {
      yDeltaFrac -= panel->position.y - halfHeightFrac + yDeltaFrac;
    }
    if (halfHeightFrac - yDeltaFrac < halfMinHeightFrac) {
      yDeltaFrac -= halfMinHeightFrac - (halfHeightFrac - yDeltaFrac);
    }
    panel->position.y += yDeltaFrac * 0.5f;
    panel->size.y -= yDeltaFrac * canvasRes.height;

    ui_canvas_persistent_flags_set(canvas, resizeHandleId, UiPersistentFlags_Dragging);
  } else if (ui_canvas_persistent_flags(canvas, resizeHandleId) & UiPersistentFlags_Dragging) {
    ui_canvas_sound(canvas, UiSoundType_Click);
    ui_canvas_persistent_flags_unset(canvas, resizeHandleId, UiPersistentFlags_Dragging);
  }
}

static void ui_panel_topbar_title(UiCanvasComp* canvas, const UiPanelOpts* opts) {
  ui_layout_push(canvas);

  ui_layout_move_dir(canvas, Ui_Right, 5, UiBase_Absolute);
  ui_layout_grow(canvas, UiAlign_BottomLeft, ui_vector(-25, 0), UiBase_Absolute, Ui_X);
  ui_label(canvas, opts->title, .fontSize = 18);

  ui_layout_pop(canvas);
}

static void ui_panel_topbar_close_button(UiCanvasComp* canvas, UiPanel* panel) {
  ui_layout_push(canvas);
  ui_style_push(canvas);

  const UiId     id     = ui_canvas_id_peek(canvas);
  const UiStatus status = ui_canvas_elem_status(canvas, id);

  if (status == UiStatus_Activated) {
    panel->flags |= UiPanelFlags_Close;
    ui_canvas_sound(canvas, UiSoundType_Click);
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

static void ui_panel_topbar_background(UiCanvasComp* canvas, const UiPanelOpts* opts) {
  ui_style_push(canvas);

  const UiId id = ui_canvas_id_peek(canvas);
  switch (ui_canvas_elem_status(canvas, id)) {
  case UiStatus_Pressed:
  case UiStatus_Activated:
    ui_style_color_with_mult(canvas, opts->topBarColor, 2);
    break;
  case UiStatus_Hovered:
  case UiStatus_Idle:
    ui_style_color(canvas, opts->topBarColor);
    break;
  }

  ui_style_outline(canvas, g_panelOutline);
  ui_canvas_draw_glyph(canvas, UiShape_Square, 10, UiFlags_Interactable);

  ui_style_pop(canvas);
}

static void ui_panel_topbar(UiCanvasComp* canvas, UiPanel* panel, const UiPanelOpts* opts) {
  ui_layout_push(canvas);

  ui_layout_move_dir(canvas, Ui_Up, 1, UiBase_Current);
  ui_layout_move_dir(canvas, Ui_Up, g_panelOutline, UiBase_Absolute);
  ui_layout_resize(
      canvas, UiAlign_BottomCenter, ui_vector(0, g_panelTopbarHeight), UiBase_Absolute, Ui_Y);

  ui_panel_topbar_background(canvas, opts);
  ui_panel_topbar_title(canvas, opts);
  ui_panel_topbar_close_button(canvas, panel);

  ui_layout_pop(canvas);
}

static void ui_panel_background(UiCanvasComp* canvas) {
  ui_style_push(canvas);

  ui_style_color(canvas, ui_color(64, 64, 64, 220));
  ui_style_outline(canvas, g_panelOutline);

  ui_canvas_draw_glyph(canvas, UiShape_Square, 10, UiFlags_Interactable);

  ui_style_pop(canvas);
}

static void ui_panel_tabs(UiCanvasComp* canvas, UiPanel* panel, const UiPanelOpts* opts) {
  static const f32     g_barHeight        = 25;
  static const f32     g_tabWidth         = 150;
  static const f32     g_spacing          = 2;
  static const UiColor g_tabInactiveColor = {32, 32, 32, 230};

  ui_layout_container_push(canvas, UiClip_Rect);

  ui_layout_push(canvas);
  ui_layout_move_to(canvas, UiBase_Current, UiAlign_TopLeft, Ui_XY);
  ui_layout_resize(canvas, UiAlign_TopLeft, ui_vector(0, g_barHeight), UiBase_Absolute, Ui_Y);

  for (u32 i = 0; i != opts->tabCount; ++i) {
    const bool   isActive = i == panel->activeTab;
    const String name     = opts->tabNames[i];
    ui_layout_resize(canvas, UiAlign_MiddleLeft, ui_vector(g_tabWidth, 0), UiBase_Absolute, Ui_X);

    if (!isActive) {
      ui_style_push(canvas);
      const UiId     id     = ui_canvas_id_peek(canvas);
      const UiStatus status = ui_canvas_elem_status(canvas, id);
      switch (status) {
      case UiStatus_Hovered:
        ui_style_color_with_mult(canvas, g_tabInactiveColor, 2);
        break;
      case UiStatus_Pressed:
      case UiStatus_Activated:
        ui_style_color_with_mult(canvas, g_tabInactiveColor, 3);
        break;
      case UiStatus_Idle:
        ui_style_color(canvas, g_tabInactiveColor);
        break;
      }
      ui_style_outline(canvas, 2);
      ui_canvas_draw_glyph(canvas, UiShape_Square, 0, UiFlags_Interactable);
      ui_style_pop(canvas);

      if (status >= UiStatus_Hovered) {
        ui_canvas_interact_type(canvas, UiInteractType_Action);
      }
      if (status == UiStatus_Activated) {
        panel->activeTab = i;
        ui_canvas_sound(canvas, UiSoundType_Click);
      }
      ui_tooltip(canvas, id, fmt_write_scratch("Switch to the \a.b{}\ar tab.", fmt_text(name)));
    }

    ui_label(canvas, name, .align = UiAlign_MiddleCenter);
    ui_layout_move_dir(canvas, Ui_Right, g_tabWidth + g_spacing, UiBase_Absolute);
  }

  ui_layout_resize_to(canvas, UiBase_Container, UiAlign_MiddleRight, Ui_X);
  ui_style_push(canvas);
  ui_style_color(canvas, ui_color(16, 16, 16, 210));
  ui_style_outline(canvas, 2);
  ui_canvas_draw_glyph(canvas, UiShape_Square, 0, UiFlags_None);
  ui_style_pop(canvas);

  ui_layout_pop(canvas);
  ui_layout_container_pop(canvas);

  ui_layout_grow(
      canvas, UiAlign_BottomCenter, ui_vector(0, -(g_barHeight + 5)), UiBase_Absolute, Ui_Y);
}

static void ui_panel_resize_handle(UiCanvasComp* canvas) {
  ui_layout_push(canvas);
  ui_style_push(canvas);
  ui_layout_inner(canvas, UiBase_Current, UiAlign_BottomRight, ui_vector(25, 25), UiBase_Absolute);
  ui_style_layer(canvas, UiLayer_Invisible);
  const UiId handleId = ui_canvas_draw_glyph(canvas, UiShape_Empty, 0, UiFlags_Interactable);
  ui_layout_pop(canvas);
  ui_style_pop(canvas);

  if (ui_canvas_elem_status(canvas, handleId) >= UiStatus_Hovered) {
    ui_canvas_interact_type(canvas, UiInteractType_Resize);
  }
}

void ui_panel_begin_with_opts(UiCanvasComp* canvas, UiPanel* panel, const UiPanelOpts* opts) {
  diag_assert_msg(!(panel->flags & UiPanelFlags_Active), "The given panel is already active");
  panel->flags |= UiPanelFlags_Active;

  const UiId resizeHandleId = ui_canvas_id_peek(canvas);
  const UiId dragHandleId   = resizeHandleId + 1;
  ui_panel_update_drag_and_resize(canvas, panel, dragHandleId, resizeHandleId);

  ui_layout_move(canvas, panel->position, UiBase_Canvas, Ui_XY);
  ui_layout_resize(canvas, UiAlign_MiddleCenter, panel->size, UiBase_Absolute, Ui_XY);

  ui_panel_resize_handle(canvas);
  ui_panel_topbar(canvas, panel, opts);
  ui_panel_background(canvas);
  if (opts->tabCount) {
    ui_panel_tabs(canvas, panel, opts);
  }

  ui_layout_container_push(canvas, UiClip_Rect);
}

void ui_panel_end(UiCanvasComp* canvas, UiPanel* panel) {
  diag_assert_msg(panel->flags & UiPanelFlags_Active, "The given panel is not active");
  panel->flags &= ~UiPanelFlags_Active;

  ui_layout_container_pop(canvas);
}
