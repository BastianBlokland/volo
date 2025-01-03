#include "core_diag.h"
#include "ui_canvas.h"
#include "ui_layout.h"
#include "ui_panel.h"
#include "ui_shape.h"
#include "ui_style.h"
#include "ui_widget.h"

static const f32 g_panelTopbarHeight = 23;
static const u32 g_panelOutline      = 3;

static void ui_panel_update_drag_and_resize(
    UiCanvasComp* c, UiPanel* panel, const UiId dragHandleId, const UiId resizeHandleId) {
  const UiVector canvasRes = ui_canvas_resolution(c);
  if (UNLIKELY(canvasRes.x <= 0 || canvasRes.y <= 0)) {
    return;
  }

  const UiVector inputDelta      = ui_canvas_input_delta(c);
  const f32      invCanvasWidth  = 1.0f / canvasRes.width;
  const f32      invCanvasHeight = 1.0f / canvasRes.height;

  const f32 halfWidthFrac     = panel->size.width * 0.5f * invCanvasWidth;
  const f32 halfMinWidthFrac  = panel->minSize.width * 0.5f * invCanvasWidth;
  const f32 halfHeightFrac    = panel->size.height * 0.5f * invCanvasHeight;
  const f32 halfMinHeightFrac = panel->minSize.height * 0.5f * invCanvasHeight;

  if (ui_canvas_elem_status(c, dragHandleId) == UiStatus_Pressed) {
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

  if (ui_canvas_elem_status(c, resizeHandleId) == UiStatus_Pressed) {
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

    ui_canvas_persistent_flags_set(c, resizeHandleId, UiPersistentFlags_Dragging);
  } else if (ui_canvas_persistent_flags(c, resizeHandleId) & UiPersistentFlags_Dragging) {
    ui_canvas_sound(c, UiSoundType_Click);
    ui_canvas_persistent_flags_unset(c, resizeHandleId, UiPersistentFlags_Dragging);
  }
}

/**
 * Draw an invisible hitbox behind the panel.
 * Reason for an extra element instead of making the panel background and topbar's themselves
 * interactable is that this avoids a small gap between the topbar and the background.
 */

static void ui_panel_hitbox_maximized(UiCanvasComp* c) {
  ui_style_push(c);
  ui_style_layer(c, UiLayer_Invisible);
  ui_canvas_draw_glyph(c, UiShape_Empty, 0, UiFlags_Interactable);
  ui_style_pop(c);
}

static void ui_panel_hitbox_with_topbar(UiCanvasComp* c) {
  ui_layout_push(c);
  ui_layout_grow(
      c,
      UiAlign_BottomLeft,
      ui_vector(0, g_panelOutline + g_panelTopbarHeight),
      UiBase_Absolute,
      Ui_Y);

  ui_style_push(c);
  ui_style_layer(c, UiLayer_Invisible);
  ui_canvas_draw_glyph(c, UiShape_Empty, 0, UiFlags_Interactable);
  ui_style_pop(c);

  ui_layout_pop(c);
}

static void ui_panel_topbar_title(UiCanvasComp* c, const UiPanelOpts* opts) {
  ui_layout_push(c);

  ui_layout_move_dir(c, Ui_Right, 5, UiBase_Absolute);
  ui_layout_grow(c, UiAlign_BottomLeft, ui_vector(-55, 0), UiBase_Absolute, Ui_X);
  ui_label(c, opts->title, .fontSize = 18);

  ui_layout_pop(c);
}

static bool ui_panel_topbar_button(UiCanvasComp* c, const Unicode glyph, const String tooltip) {
  ui_layout_push(c);
  ui_style_push(c);

  const UiId     id     = ui_canvas_id_peek(c);
  const UiStatus status = ui_canvas_elem_status(c, id);

  if (status == UiStatus_Activated) {
    ui_canvas_sound(c, UiSoundType_Click);
  }
  if (status >= UiStatus_Hovered) {
    ui_canvas_interact_type(c, UiInteractType_Action);
  }

  if (status > UiStatus_Idle) {
    ui_layout_grow(c, UiAlign_MiddleCenter, ui_vector(3, 3), UiBase_Absolute, Ui_XY);
  }

  switch (status) {
  case UiStatus_Hovered:
    ui_style_outline(c, 2);
    break;
  case UiStatus_Pressed:
  case UiStatus_Activated:
  case UiStatus_ActivatedAlt:
    ui_style_outline(c, 1);
    break;
  case UiStatus_Idle:
    ui_style_outline(c, 2);
    break;
  }

  ui_canvas_draw_glyph(c, glyph, 0, UiFlags_Interactable);

  ui_tooltip(c, id, tooltip);

  ui_style_pop(c);
  ui_layout_pop(c);

  return status == UiStatus_Activated;
}

static void ui_panel_topbar_background(UiCanvasComp* c, const UiPanelOpts* opts) {
  ui_style_push(c);

  const UiId id = ui_canvas_id_peek(c);
  switch (ui_canvas_elem_status(c, id)) {
  case UiStatus_Pressed:
  case UiStatus_Activated:
  case UiStatus_ActivatedAlt:
    ui_style_color_with_mult(c, opts->topBarColor, 2);
    break;
  case UiStatus_Hovered:
  case UiStatus_Idle:
    ui_style_color(c, opts->topBarColor);
    break;
  }

  ui_style_outline(c, g_panelOutline);
  ui_canvas_draw_glyph(c, UiShape_Square, 10, UiFlags_Interactable);

  ui_style_pop(c);
}

static void ui_panel_topbar(UiCanvasComp* c, UiPanel* panel, const UiPanelOpts* opts) {
  ui_layout_push(c);

  ui_layout_move_dir(c, Ui_Up, 1, UiBase_Current);
  ui_layout_move_dir(c, Ui_Up, g_panelOutline, UiBase_Absolute);
  ui_layout_resize(c, UiAlign_BottomLeft, ui_vector(0, g_panelTopbarHeight), UiBase_Absolute, Ui_Y);

  ui_panel_topbar_background(c, opts);
  ui_panel_topbar_title(c, opts);

  ui_layout_push(c);
  {
    const UiVector buttonSize = ui_vector(18, 18);
    ui_layout_move(c, ui_vector(1, 0.5), UiBase_Current, Ui_XY);
    ui_layout_resize(c, UiAlign_MiddleCenter, buttonSize, UiBase_Absolute, Ui_XY);

    ui_layout_move_dir(c, Ui_Left, 12, UiBase_Absolute);
    if (ui_panel_topbar_button(c, UiShape_Close, string_lit("Close this panel"))) {
      panel->flags |= UiPanelFlags_Close;
    }
    if (opts->pinnable) {
      ui_layout_move_dir(c, Ui_Left, 27, UiBase_Absolute);
      ui_style_push(c);

      const bool pinned = (panel->flags & UiPanelFlags_Pinned) != 0;
      if (pinned) {
        ui_style_color(c, ui_color(16, 192, 0, 255));
      }
      const String tooltip = pinned ? string_lit("Unpin this panel") : string_lit("Pin this panel");
      if (ui_panel_topbar_button(c, UiShape_PushPin, tooltip)) {
        panel->flags ^= UiPanelFlags_Pinned;
      }
      ui_style_pop(c);
    }
  }
  ui_layout_pop(c);

  ui_layout_pop(c);
}

static void ui_panel_background(UiCanvasComp* c) {
  ui_style_push(c);

  ui_style_color(c, ui_color(64, 64, 64, 220));
  ui_style_outline(c, g_panelOutline);

  ui_canvas_draw_glyph(c, UiShape_Square, 10, UiFlags_None);

  ui_style_pop(c);
}

static void ui_panel_tabs(UiCanvasComp* c, UiPanel* panel, const UiPanelOpts* opts) {
  static const f32     g_barHeight        = 25;
  static const f32     g_tabWidth         = 150;
  static const f32     g_spacing          = 2;
  static const UiColor g_tabInactiveColor = {32, 32, 32, 230};

  ui_layout_container_push(c, UiClip_Rect, UiLayer_Normal);

  ui_layout_push(c);
  ui_layout_move_to(c, UiBase_Current, UiAlign_TopLeft, Ui_XY);
  ui_layout_resize(c, UiAlign_TopLeft, ui_vector(0, g_barHeight), UiBase_Absolute, Ui_Y);

  for (u32 i = 0; i != opts->tabCount; ++i) {
    const bool   isActive = i == panel->activeTab;
    const String name     = opts->tabNames[i];
    ui_layout_resize(c, UiAlign_MiddleLeft, ui_vector(g_tabWidth, 0), UiBase_Absolute, Ui_X);

    if (!isActive) {
      ui_style_push(c);
      const UiId     id     = ui_canvas_id_peek(c);
      const UiStatus status = ui_canvas_elem_status(c, id);
      switch (status) {
      case UiStatus_Hovered:
        ui_style_color_with_mult(c, g_tabInactiveColor, 2);
        break;
      case UiStatus_Pressed:
      case UiStatus_Activated:
      case UiStatus_ActivatedAlt:
        ui_style_color_with_mult(c, g_tabInactiveColor, 3);
        break;
      case UiStatus_Idle:
        ui_style_color(c, g_tabInactiveColor);
        break;
      }
      ui_style_outline(c, 2);
      ui_canvas_draw_glyph(c, UiShape_Square, 10, UiFlags_Interactable);
      ui_style_pop(c);

      if (status >= UiStatus_Hovered) {
        ui_canvas_interact_type(c, UiInteractType_Action);
      }
      if (status == UiStatus_Activated) {
        panel->activeTab = i;
        ui_canvas_sound(c, UiSoundType_Click);
      }
      ui_tooltip(c, id, fmt_write_scratch("Switch to the \a.b{}\ar tab.", fmt_text(name)));
    }

    ui_label(c, name, .align = UiAlign_MiddleCenter);
    ui_layout_move_dir(c, Ui_Right, g_tabWidth + g_spacing, UiBase_Absolute);
  }

  ui_layout_resize_to(c, UiBase_Container, UiAlign_MiddleRight, Ui_X);
  ui_style_push(c);
  ui_style_color(c, ui_color(16, 16, 16, 210));
  ui_style_outline(c, 2);
  ui_canvas_draw_glyph(c, UiShape_Square, 10, UiFlags_None);
  ui_style_pop(c);

  ui_layout_pop(c);
  ui_layout_container_pop(c);

  ui_layout_grow(c, UiAlign_BottomCenter, ui_vector(0, -(g_barHeight + 5)), UiBase_Absolute, Ui_Y);
}

static void ui_panel_resize_handle(UiCanvasComp* c) {
  ui_layout_push(c);
  ui_style_push(c);
  ui_layout_inner(c, UiBase_Current, UiAlign_BottomRight, ui_vector(25, 25), UiBase_Absolute);
  ui_style_layer(c, UiLayer_Invisible);
  const UiId handleId = ui_canvas_draw_glyph(c, UiShape_Empty, 0, UiFlags_Interactable);
  ui_layout_pop(c);
  ui_style_pop(c);

  if (ui_canvas_elem_status(c, handleId) >= UiStatus_Hovered) {
    ui_canvas_interact_type(c, UiInteractType_Resize);
  }
}

void ui_panel_begin_with_opts(UiCanvasComp* c, UiPanel* panel, const UiPanelOpts* opts) {
  diag_assert_msg(!(panel->flags & UiPanelFlags_Active), "The given panel is already active");
  panel->flags |= UiPanelFlags_Active;

  if (panel->flags & UiPanelFlags_Maximized) {
    ui_layout_resize(c, UiAlign_BottomLeft, ui_vector(1, 1), UiBase_Canvas, Ui_XY);
    ui_panel_hitbox_maximized(c);
  } else {
    const UiId hitboxId       = ui_canvas_id_peek(c);
    const UiId resizeHandleId = hitboxId + 1;
    const UiId dragHandleId   = resizeHandleId + 1;
    ui_panel_update_drag_and_resize(c, panel, dragHandleId, resizeHandleId);

    ui_layout_set_pos(c, UiBase_Canvas, panel->position, UiBase_Canvas);
    ui_layout_resize(c, UiAlign_MiddleCenter, panel->size, UiBase_Absolute, Ui_XY);

    ui_panel_hitbox_with_topbar(c);
    ui_panel_resize_handle(c);
    ui_panel_topbar(c, panel, opts);
  }

  ui_panel_background(c);
  if (opts->tabCount) {
    ui_panel_tabs(c, panel, opts);
  }

  ui_layout_container_push(c, UiClip_Rect, UiLayer_Normal);
}

void ui_panel_end(UiCanvasComp* c, UiPanel* panel) {
  diag_assert_msg(panel->flags & UiPanelFlags_Active, "The given panel is not active");
  panel->flags &= ~UiPanelFlags_Active;

  ui_layout_container_pop(c);
}

void ui_panel_pin(UiPanel* panel) { panel->flags |= UiPanelFlags_Pinned; }

void ui_panel_maximize(UiPanel* panel) { panel->flags |= UiPanelFlags_Maximized; }

bool ui_panel_closed(const UiPanel* panel) { return (panel->flags & UiPanelFlags_Close) != 0; }

bool ui_panel_pinned(const UiPanel* panel) { return (panel->flags & UiPanelFlags_Pinned) != 0; }

bool ui_panel_maximized(const UiPanel* panel) {
  return (panel->flags & UiPanelFlags_Maximized) != 0;
}
