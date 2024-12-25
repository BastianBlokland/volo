#include "core_diag.h"
#include "core_math.h"
#include "ui_canvas.h"
#include "ui_color.h"
#include "ui_layout.h"
#include "ui_scrollview.h"
#include "ui_shape.h"
#include "ui_style.h"

static const f32 g_scrollSensitivity = 30;
static const f32 g_scrollBarWidth    = 10;

typedef enum {
  UiScrollviewStatus_HoveredBg        = 1 << 0,
  UiScrollviewStatus_HoveredBar       = 1 << 1,
  UiScrollviewStatus_HoveredContent   = 1 << 2,
  UiScrollviewStatus_PressedBar       = 1 << 3,
  UiScrollviewStatus_HoveringViewport = UiScrollviewStatus_HoveredBg |
                                        UiScrollviewStatus_HoveredBar |
                                        UiScrollviewStatus_HoveredContent,
} UiScrollviewStatusFlags;

typedef struct {
  UiId                    bgId, barId, handleId, firstContentId;
  UiScrollviewStatusFlags flags;
  UiRect                  viewport;
  f32                     offscreenHeight;
  f32                     offsetFrac;   // Current position of the viewport in the content, 0 - 1.
  f32                     viewportFrac; // Size of the viewport relative to the content, 0 - 1.
  UiVector                inputPos, inputScroll; // In absolute canvas pixels.
} UiScrollviewStatus;

static bool ui_scrollview_content_hovered(UiCanvasComp* canvas, const UiId first, const UiId last) {
  const UiStatus status = ui_canvas_group_status(canvas, first, last);
  return status == UiStatus_Hovered;
}

static UiScrollviewStatus
ui_scrollview_query_status(UiCanvasComp* canvas, const UiScrollview* scrollview, const f32 height) {
  UiScrollviewStatus status = {
      .bgId           = ui_canvas_id_peek(canvas),
      .barId          = ui_canvas_id_peek(canvas) + 1,
      .handleId       = ui_canvas_id_peek(canvas) + 2,
      .firstContentId = ui_canvas_id_peek(canvas) + 3,
  };
  if (ui_canvas_elem_status(canvas, status.bgId) >= UiStatus_Hovered) {
    status.flags |= UiScrollviewStatus_HoveredBg;
  }
  if (ui_canvas_elem_status(canvas, status.barId) >= UiStatus_Hovered) {
    status.flags |= UiScrollviewStatus_HoveredBar;
  }
  if (ui_scrollview_content_hovered(canvas, status.firstContentId, scrollview->lastContentId)) {
    status.flags |= UiScrollviewStatus_HoveredContent;
  }
  if (ui_canvas_elem_status(canvas, status.barId) >= UiStatus_Pressed) {
    status.flags |= UiScrollviewStatus_PressedBar;
  }
  status.viewport        = ui_canvas_elem_rect(canvas, status.bgId);
  status.offscreenHeight = math_max(height - status.viewport.height, 0.0f);
  if (status.offscreenHeight > 0.0f) {
    status.offsetFrac = math_clamp_f32(scrollview->offset / status.offscreenHeight, 0, 1);
  }
  if (height > 0.0f) {
    status.viewportFrac = math_clamp_f32(status.viewport.height / height, 0, 1);
  }
  status.inputPos    = ui_canvas_input_pos(canvas);
  status.inputScroll = ui_canvas_input_scroll(canvas);
  return status;
}

static void ui_scrollview_update(
    UiCanvasComp* canvas, UiScrollview* scrollview, const UiScrollviewStatus* status) {
  /**
   * Allow scrolling when hovering over the viewport.
   */
  const bool blockInput = (scrollview->flags & UiScrollviewFlags_BlockInput) != 0;
  if (!blockInput && status->flags & UiScrollviewStatus_HoveringViewport) {
    scrollview->offset -= status->inputScroll.y * g_scrollSensitivity;
  }

  /**
   * Jump to a specific offset when clicking the bar.
   */
  if (status->offscreenHeight > 0 && status->flags & UiScrollviewStatus_PressedBar) {
    const f32 inputFrac = math_unlerp(
        status->viewport.y, status->viewport.y + status->viewport.height, status->inputPos.y);
    const f32 halfViewportFrac = status->viewportFrac * 0.5f;
    const f32 offscreenFrac    = 1.0f - status->viewportFrac;
    const f32 remappedFrac     = (1.0f - (inputFrac - halfViewportFrac) / offscreenFrac);
    scrollview->offset         = remappedFrac * status->offscreenHeight;

    ui_canvas_persistent_flags_set(canvas, status->barId, UiPersistentFlags_Dragging);
  } else if (ui_canvas_persistent_flags(canvas, status->barId) & UiPersistentFlags_Dragging) {
    ui_canvas_sound(canvas, UiSoundType_Click);
    ui_canvas_persistent_flags_unset(canvas, status->barId, UiPersistentFlags_Dragging);
  }

  if (status->offscreenHeight > 0 && status->flags & UiScrollviewStatus_HoveredBar) {
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

  const bool hovered = status->flags & UiScrollviewStatus_HoveredBar && status->offscreenHeight > 0;
  const UiColor barColor    = ui_color(16, 16, 16, 192);
  const UiColor handleColor = hovered ? ui_color_white : ui_color(255, 255, 255, 178);

  ui_style_push(canvas);

  // Draw bar background.
  if (hovered) {
    ui_style_color_with_mult(canvas, barColor, 2);
  } else {
    ui_style_color(canvas, barColor);
  }
  ui_style_outline(canvas, 0);
  ui_canvas_draw_glyph(canvas, UiShape_Square, 10, UiFlags_Interactable);

  // Draw bar handle.
  const f32 offscreenFrac = 1.0f - status->viewportFrac;
  const f32 handleTopFrac = 1.0f - status->offsetFrac * offscreenFrac;
  const f32 handleInsetX  = hovered ? 4 : 6;

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

void ui_scrollview_begin(
    UiCanvasComp* canvas, UiScrollview* scrollview, const UiLayer layer, const f32 height) {
  diag_assert_msg(
      !(scrollview->flags & UiScrollviewFlags_Active), "The given scrollview is already active");
  diag_assert(height >= 0);
  scrollview->flags |= UiScrollviewFlags_Active;

  const UiScrollviewStatus status = ui_scrollview_query_status(canvas, scrollview, height);
  ui_scrollview_update(canvas, scrollview, &status);

  // Draw an invisible element over the whole viewport to act as a hover target and track the rect.
  ui_canvas_draw_glyph(canvas, UiShape_Empty, 0, UiFlags_Interactable | UiFlags_TrackRect);

  ui_scrollview_draw_bar(canvas, &status);

  // Push a container with the viewport rect to clip the content within the viewport.
  ui_layout_grow(
      canvas, UiAlign_MiddleLeft, ui_vector(-g_scrollBarWidth, 0), UiBase_Absolute, Ui_X);
  ui_layout_container_push(canvas, UiClip_Rect, layer);

  // Push a container with the content rect.
  ui_layout_move_dir(canvas, Ui_Up, scrollview->offset, UiBase_Absolute);
  if (status.offscreenHeight > 0) {
    ui_layout_grow(
        canvas, UiAlign_TopCenter, ui_vector(0, status.offscreenHeight), UiBase_Absolute, Ui_Y);
  }
  ui_layout_container_push(canvas, UiClip_None, layer);
}

void ui_scrollview_end(UiCanvasComp* canvas, UiScrollview* scrollview) {
  diag_assert_msg(
      scrollview->flags & UiScrollviewFlags_Active, "The given scrollview is not active");
  scrollview->flags &= ~UiScrollviewFlags_Active;

  /**
   * Track the last id of the content that was drawn inside this scrollview.
   * Will be used the next frame to determine if any of the content is being hovered by the user.
   */
  scrollview->lastContentId = ui_canvas_id_peek(canvas) - 1;

  ui_layout_container_pop(canvas);
  ui_layout_container_pop(canvas);
}
