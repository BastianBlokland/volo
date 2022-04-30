#include "core_diag.h"
#include "core_math.h"
#include "ui_canvas.h"
#include "ui_layout.h"
#include "ui_scrollview.h"
#include "ui_shape.h"
#include "ui_style.h"

static const f32 g_scrollSensitivity = 30;
static const f32 g_scrollBarWidth    = 10;

typedef enum {
  UiScrollviewStatusFlags_HoveredBg  = 1 << 0,
  UiScrollviewStatusFlags_HoveredBar = 1 << 1,
  UiScrollviewStatusFlags_PressedBar = 1 << 2,
} UiScrollviewStatusFlags;

typedef struct {
  UiId                    bgId, barId, handleId;
  UiScrollviewStatusFlags flags;
  UiRect                  viewport;
  f32                     offscreenHeight;
  f32                     offsetFrac;   // Current position of the viewport in the content, 0 - 1.
  f32                     viewportFrac; // Size of the viewport relative to the content, 0 - 1.
  UiVector                inputPos, inputScroll; // In absolute canvas pixels.
} UiScrollviewStatus;

static UiScrollviewStatus
ui_scrollview_query_status(UiCanvasComp* canvas, const UiScrollview* scrollview, const f32 height) {
  UiScrollviewStatus status = {
      .bgId     = ui_canvas_id_peek(canvas),
      .barId    = ui_canvas_id_peek(canvas) + 1,
      .handleId = ui_canvas_id_peek(canvas) + 2,
  };
  if (ui_canvas_elem_status(canvas, status.bgId) >= UiStatus_Hovered) {
    status.flags |= UiScrollviewStatusFlags_HoveredBg;
  }
  if (ui_canvas_elem_status(canvas, status.barId) >= UiStatus_Hovered) {
    status.flags |= UiScrollviewStatusFlags_HoveredBar;
  }
  if (ui_canvas_elem_status(canvas, status.barId) >= UiStatus_Pressed) {
    status.flags |= UiScrollviewStatusFlags_PressedBar;
  }
  status.viewport        = ui_canvas_elem_rect(canvas, status.bgId);
  status.offscreenHeight = math_max(height - status.viewport.height, 0);
  if (status.offscreenHeight > 0) {
    status.offsetFrac = math_clamp_f32(scrollview->offset / status.offscreenHeight, 0, 1);
  }
  status.viewportFrac = math_clamp_f32(status.viewport.height / height, 0, 1);
  status.inputPos     = ui_canvas_input_pos(canvas);
  status.inputScroll  = ui_canvas_input_scroll(canvas);
  return status;
}

static void ui_scrollview_update(
    UiCanvasComp* canvas, UiScrollview* scrollview, const UiScrollviewStatus* status) {
  /**
   * Allow scrolling when hovering over the viewport.
   * TODO: Support scrolling when there are interactable elements drawn over the viewport.
   */
  if (status->flags & (UiScrollviewStatusFlags_HoveredBg | UiScrollviewStatusFlags_HoveredBar)) {
    scrollview->offset -= status->inputScroll.y * g_scrollSensitivity;
  }

  /**
   * Jump to a specific offset when clicking the bar.
   */
  if (status->offscreenHeight > 0 && status->flags & UiScrollviewStatusFlags_PressedBar) {
    const f32 inputFrac = math_unlerp(
        status->viewport.y, status->viewport.y + status->viewport.height, status->inputPos.y);
    const f32 halfViewportFrac = status->viewportFrac * 0.5f;
    const f32 offscreenFrac    = 1.0f - status->viewportFrac;
    const f32 remappedFrac     = (1.0f - (inputFrac - halfViewportFrac) / offscreenFrac);
    scrollview->offset         = remappedFrac * status->offscreenHeight;
  }

  if (status->flags & UiScrollviewStatusFlags_HoveredBar) {
    ui_canvas_interact_type(canvas, UiInteractType_Action);
  }

  // Clamped the offset to keep the content in view.
  scrollview->offset = math_clamp_f32(scrollview->offset, 0, status->offscreenHeight);
}

static void ui_scrollview_draw_bar(UiCanvasComp* canvas, const UiScrollviewStatus* status) {
  ui_layout_push(canvas);
  ui_layout_move_dir(canvas, Ui_Right, 1, UiBase_Current);
  ui_layout_resize(
      canvas, UiAlign_MiddleRight, ui_vector(g_scrollBarWidth, 0), UiBase_Absolute, Ui_X);

  const UiColor barColor    = ui_color(16, 16, 16, 192);
  const UiColor handleColor = ui_color_white;
  const bool    hovered =
      status->flags & UiScrollviewStatusFlags_HoveredBar && status->offscreenHeight > 0;

  ui_style_push(canvas);

  // Draw bar background.
  if (hovered) {
    ui_style_color_with_mult(canvas, barColor, 2);
  } else {
    ui_style_color(canvas, barColor);
  }
  ui_style_outline(canvas, 0);
  ui_canvas_draw_glyph(canvas, UiShape_Square, 0, UiFlags_Interactable);

  // Draw bar handle.
  const f32 offscreenFrac = 1.0f - status->viewportFrac;
  const f32 handleTopFrac = 1.0f - status->offsetFrac * offscreenFrac;
  const f32 handleInsetX  = hovered ? 5 : 7;

  ui_layout_move(canvas, ui_vector(0, handleTopFrac), UiBase_Current, Ui_Y);
  ui_layout_resize(
      canvas, UiAlign_TopCenter, ui_vector(0, status->viewportFrac), UiBase_Current, Ui_Y);
  ui_layout_grow(canvas, UiAlign_MiddleCenter, ui_vector(-handleInsetX, 0), UiBase_Absolute, Ui_X);

  ui_style_color(canvas, handleColor);
  ui_style_outline(canvas, 1);
  ui_canvas_draw_glyph(canvas, UiShape_Circle, 0, UiFlags_None);

  ui_style_pop(canvas);
  ui_layout_pop(canvas);
}

void ui_scrollview_begin(UiCanvasComp* canvas, UiScrollview* scrollview, const f32 height) {
  diag_assert_msg(
      !(scrollview->flags & UiScrollviewFlags_Active), "The given scrollview is already active");
  diag_assert(height > 0);
  scrollview->flags |= UiScrollviewFlags_Active;

  const UiScrollviewStatus status = ui_scrollview_query_status(canvas, scrollview, height);
  ui_scrollview_update(canvas, scrollview, &status);

  // Draw an invisible element over the whole viewport to act as a hover target and track the rect.
  ui_canvas_draw_glyph(canvas, UiShape_Empty, 0, UiFlags_Interactable | UiFlags_TrackRect);

  ui_scrollview_draw_bar(canvas, &status);

  // Push a container with the viewport rect to clip the content within the viewport.
  ui_layout_grow(
      canvas, UiAlign_MiddleLeft, ui_vector(-g_scrollBarWidth, 0), UiBase_Absolute, Ui_X);
  ui_layout_container_push(canvas);

  // Push a container with the content rect.
  ui_layout_move_dir(canvas, Ui_Up, scrollview->offset, UiBase_Absolute);
  if (status.offscreenHeight > 0) {
    ui_layout_grow(
        canvas, UiAlign_TopCenter, ui_vector(0, status.offscreenHeight), UiBase_Absolute, Ui_Y);
  }
  ui_layout_container_push(canvas);
}

void ui_scrollview_end(UiCanvasComp* canvas, UiScrollview* scrollview) {
  diag_assert_msg(
      scrollview->flags & UiScrollviewFlags_Active, "The given scrollview is not active");
  scrollview->flags &= ~UiScrollviewFlags_Active;

  ui_layout_container_pop(canvas);
  ui_layout_container_pop(canvas);
}
