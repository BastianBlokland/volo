#include "core_diag.h"
#include "core_dynstring.h"
#include "core_float.h"
#include "core_math.h"
#include "core_sentinel.h"
#include "ui_canvas.h"
#include "ui_layout.h"
#include "ui_shape.h"
#include "ui_style.h"
#include "ui_widget.h"

static const f32 g_uiDisabledMult = 0.4f;

static void
ui_interactable_frame_style(UiCanvasComp* canvas, const UiColor color, const UiStatus status) {
  switch (status) {
  case UiStatus_Hovered:
    ui_style_color_with_mult(canvas, color, 2);
    ui_style_outline(canvas, 3);
    break;
  case UiStatus_Pressed:
  case UiStatus_Activated:
  case UiStatus_ActivatedAlt:
    ui_style_color_with_mult(canvas, color, 3);
    ui_style_outline(canvas, 1);
    break;
  case UiStatus_Idle:
    ui_style_color(canvas, color);
    ui_style_outline(canvas, 2);
    break;
  }
}

static void ui_interactable_text_style(UiCanvasComp* canvas, const UiStatus status) {
  switch (status) {
  case UiStatus_Hovered:
    ui_style_outline(canvas, 2);
    break;
  case UiStatus_Pressed:
  case UiStatus_Activated:
  case UiStatus_ActivatedAlt:
    ui_style_outline(canvas, 0);
    break;
  case UiStatus_Idle:
    break;
  }
}

static UiId ui_label_selectable(UiCanvasComp* canvas, const String text, const UiLabelOpts* opts) {
  const UiId     id       = ui_canvas_id_peek(canvas);
  const UiStatus status   = ui_canvas_elem_status(canvas, id);
  bool           selected = ui_canvas_text_editor_active(canvas, id);

  if (!selected && status == UiStatus_Activated) {
    ui_canvas_text_editor_start(canvas, text, id, text.size, UiTextFilter_Readonly);
    selected = true;
  }

  const UiFlags flags = UiFlags_AllowWordBreak | UiFlags_SingleLine | UiFlags_Interactable |
                        UiFlags_InteractOnPress | UiFlags_TightTextRect;

  if (selected) {
    ui_canvas_draw_text_editor(canvas, opts->fontSize, UiAlign_MiddleLeft, flags);
  } else {
    ui_canvas_draw_text(canvas, text, opts->fontSize, UiAlign_MiddleLeft, flags);
  }

  if (status >= UiStatus_Hovered) {
    ui_canvas_interact_type(canvas, UiInteractType_Text);
  }
  return id;
}

void ui_label_with_opts(UiCanvasComp* canvas, const String text, const UiLabelOpts* opts) {
  UiId id;
  if (opts->selectable) {
    id = ui_label_selectable(canvas, text, opts);
  } else {
    const UiFlags flags = !string_is_empty(opts->tooltip) ? UiFlags_Interactable : UiFlags_None;
    id                  = ui_canvas_draw_text(canvas, text, opts->fontSize, opts->align, flags);
  }
  if (!string_is_empty(opts->tooltip)) {
    ui_tooltip(canvas, id, opts->tooltip, .maxSize = opts->tooltipMaxSize);
  }
}

void ui_label_entity(UiCanvasComp* canvas, const EcsEntityId entity) {
  const u32 index  = ecs_entity_id_index(entity);
  const u32 serial = ecs_entity_id_serial(entity);
  ui_style_push(canvas);
  ui_style_variation(canvas, UiVariation_Monospace);
  ui_label(
      canvas,
      fmt_write_scratch("{}", ecs_entity_fmt(entity)),
      .selectable = true,
      .tooltip    = fmt_write_scratch("Index: {}\nSerial: {}", fmt_int(index), fmt_int(serial)));
  ui_style_pop(canvas);
}

bool ui_button_with_opts(UiCanvasComp* canvas, const UiButtonOpts* opts) {
  const UiId     id       = ui_canvas_id_peek(canvas);
  const bool     disabled = (opts->flags & UiWidget_Disabled) != 0;
  const UiStatus status   = disabled ? UiStatus_Idle : ui_canvas_elem_status(canvas, id);

  ui_style_push(canvas);
  if (opts->noFrame) {
    ui_style_layer(canvas, UiLayer_Invisible);
  } else {
    ui_interactable_frame_style(canvas, opts->frameColor, status);
  }
  ui_canvas_draw_glyph(canvas, UiShape_Circle, 10, UiFlags_Interactable);
  ui_style_pop(canvas);

  ui_style_push(canvas);
  if (disabled) {
    ui_style_color_mult(canvas, g_uiDisabledMult);
  }
  ui_interactable_text_style(canvas, status);
  ui_canvas_draw_text(canvas, opts->label, opts->fontSize, UiAlign_MiddleCenter, UiFlags_None);
  ui_style_pop(canvas);

  if (status >= UiStatus_Hovered) {
    ui_canvas_interact_type(canvas, UiInteractType_Action);
  }
  if (status == UiStatus_Activated) {
    ui_canvas_sound(canvas, UiSoundType_Click);
  }

  if (!string_is_empty(opts->tooltip)) {
    ui_tooltip(canvas, id, opts->tooltip);
  }

  if (opts->activate && status != UiStatus_Activated) {
    ui_canvas_sound(canvas, UiSoundType_ClickAlt);
    return true;
  }
  return status == UiStatus_Activated;
}

static void ui_slider_bar(UiCanvasComp* canvas, const UiStatus status, const UiSliderOpts* opts) {
  ui_layout_push(canvas);
  ui_style_push(canvas);

  if (opts->vertical) {
    ui_layout_move_to(canvas, UiBase_Current, UiAlign_BottomCenter, Ui_X);
    ui_layout_resize(
        canvas, UiAlign_BottomCenter, ui_vector(opts->thickness, 0), UiBase_Absolute, Ui_X);
  } else {
    ui_layout_move_to(canvas, UiBase_Current, UiAlign_MiddleLeft, Ui_Y);
    ui_layout_resize(
        canvas, UiAlign_MiddleLeft, ui_vector(0, opts->thickness), UiBase_Absolute, Ui_Y);
  }

  ui_style_outline(canvas, 2);
  switch (status) {
  case UiStatus_Hovered:
  case UiStatus_Pressed:
  case UiStatus_Activated:
  case UiStatus_ActivatedAlt:
    ui_style_color_with_mult(canvas, opts->barColor, 1.5);
    break;
  case UiStatus_Idle:
    ui_style_color(canvas, opts->barColor);
    break;
  }
  ui_canvas_draw_glyph(canvas, UiShape_Square, 0, UiFlags_Interactable | UiFlags_TrackRect);

  ui_style_pop(canvas);
  ui_layout_pop(canvas);
}

static void ui_slider_handle(
    UiCanvasComp* canvas, const UiStatus status, const f32 normValue, const UiSliderOpts* opts) {
  ui_layout_push(canvas);
  ui_style_push(canvas);

  const UiVector size = ui_vector(opts->handleSize, opts->handleSize);
  if (opts->vertical) {
    ui_layout_grow(canvas, UiAlign_MiddleCenter, ui_vector(0, -size.y), UiBase_Absolute, Ui_Y);
    ui_layout_move(canvas, ui_vector(0.5f, normValue), UiBase_Current, Ui_XY);
  } else {
    ui_layout_grow(canvas, UiAlign_MiddleCenter, ui_vector(-size.x, 0), UiBase_Absolute, Ui_X);
    ui_layout_move(canvas, ui_vector(normValue, 0.5f), UiBase_Current, Ui_XY);
  }
  ui_layout_resize(canvas, UiAlign_MiddleCenter, size, UiBase_Absolute, Ui_XY);

  if (opts->flags & UiWidget_Disabled) {
    ui_style_color_mult(canvas, g_uiDisabledMult);
  }

  switch (status) {
  case UiStatus_Hovered:
    ui_style_outline(canvas, 2);
    break;
  case UiStatus_Pressed:
    ui_style_outline(canvas, 0);
    break;
  case UiStatus_Activated:
  case UiStatus_ActivatedAlt:
  case UiStatus_Idle:
    break;
  }
  ui_canvas_draw_glyph(canvas, UiShape_Circle, 0, UiFlags_Interactable);

  if (status >= UiStatus_Hovered) {
    ui_layout_move(canvas, ui_vector(0.5, 1), UiBase_Current, Ui_XY);
    ui_layout_resize(canvas, UiAlign_BottomCenter, ui_vector(100, 100), UiBase_Absolute, Ui_XY);

    ui_style_outline(canvas, 2);
    ui_style_layer(canvas, UiLayer_Overlay);
    ui_style_variation(canvas, UiVariation_Monospace);

    const f32    value = math_lerp(opts->min, opts->max, normValue);
    const String label = fmt_write_scratch("{}", fmt_float(value, .maxDecDigits = 2));
    ui_canvas_draw_text(canvas, label, 15, UiAlign_BottomCenter, UiFlags_None);
  } else {
    ui_canvas_id_skip(canvas, 1);
  }

  ui_style_pop(canvas);
  ui_layout_pop(canvas);
}

bool ui_slider_with_opts(UiCanvasComp* canvas, f32* input, const UiSliderOpts* opts) {
  const UiId     barId    = ui_canvas_id_peek(canvas);
  const UiId     handleId = barId + 1;
  const UiStatus status =
      opts->flags & UiWidget_Disabled
          ? UiStatus_Idle
          : math_max(ui_canvas_elem_status(canvas, barId), ui_canvas_elem_status(canvas, handleId));

  const f32      halfHandleSize = opts->handleSize * 0.5f;
  const UiRect   barRect        = ui_canvas_elem_rect(canvas, barId);
  const UiVector inputPos       = ui_canvas_input_pos(canvas);

  const UiPersistentFlags persistFlags = ui_canvas_persistent_flags(canvas, barId);
  const bool              wasDragging  = (persistFlags & UiPersistentFlags_Dragging) != 0;
  if (!wasDragging && status >= UiStatus_Pressed) {
    ui_canvas_persistent_flags_set(canvas, barId, UiPersistentFlags_Dragging);
  } else if (wasDragging && status < UiStatus_Pressed) {
    ui_canvas_persistent_flags_unset(canvas, barId, UiPersistentFlags_Dragging);
    ui_canvas_sound(canvas, UiSoundType_Click);
  }

  f32 normValue;
  if (status >= UiStatus_Pressed) {
    normValue = opts->vertical ? math_unlerp(
                                     barRect.y + halfHandleSize,
                                     barRect.y + barRect.height - halfHandleSize,
                                     inputPos.y)
                               : math_unlerp(
                                     barRect.x + halfHandleSize,
                                     barRect.x + barRect.width - halfHandleSize,
                                     inputPos.x);
  } else {
    normValue = math_unlerp(opts->min, opts->max, *input);
  }
  if (opts->step > f32_epsilon) {
    const f32 normStep = opts->step / math_abs(opts->max - opts->min);
    normValue          = math_round_nearest_f32(normValue / normStep) * normStep;
  }
  normValue = math_clamp_f32(normValue, 0, 1);

  ui_slider_bar(canvas, status, opts);
  ui_slider_handle(canvas, status, normValue, opts);

  if (status >= UiStatus_Hovered) {
    ui_canvas_interact_type(canvas, UiInteractType_Action);
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

  if (status == UiStatus_Hovered) {
    ui_style_outline(canvas, 2);
  }
  if (opts->flags & UiWidget_Disabled) {
    ui_style_color_mult(canvas, g_uiDisabledMult);
  }
  ui_canvas_draw_glyph(canvas, UiShape_Check, 0, UiFlags_None);

  ui_style_pop(canvas);
}

bool ui_toggle_with_opts(UiCanvasComp* canvas, bool* input, const UiToggleOpts* opts) {
  const UiId     id = ui_canvas_id_peek(canvas);
  const UiStatus status =
      opts->flags & UiWidget_Disabled ? UiStatus_Idle : ui_canvas_elem_status(canvas, id);
  const UiVector size = {opts->size, opts->size};

  if (status == UiStatus_Activated) {
    *input ^= true;
  }
  ui_layout_push(canvas);
  ui_layout_inner(canvas, UiBase_Current, UiAlign_MiddleLeft, size, UiBase_Absolute);

  ui_style_push(canvas);
  switch (status) {
  case UiStatus_Hovered:
    ui_style_color_with_mult(canvas, opts->bgColor, 2);
    ui_style_outline(canvas, 3);
    break;
  case UiStatus_Pressed:
  case UiStatus_Activated:
  case UiStatus_ActivatedAlt:
    ui_style_color_with_mult(canvas, opts->bgColor, 3);
    ui_style_outline(canvas, 1);
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
    ui_canvas_id_skip(canvas, 1);
  }

  if (status >= UiStatus_Hovered) {
    ui_canvas_interact_type(canvas, UiInteractType_Action);
  }
  if (status == UiStatus_Activated) {
    ui_canvas_sound(canvas, UiSoundType_Click);
  }

  if (!string_is_empty(opts->tooltip)) {
    ui_tooltip(canvas, id, opts->tooltip);
  }

  ui_layout_pop(canvas);
  return status == UiStatus_Activated;
}

bool ui_toggle_flag_with_opts(
    UiCanvasComp* canvas, u32* value, const u32 flag, const UiToggleOpts* opts) {
  bool set = (*value & flag) != 0;
  if (ui_toggle_with_opts(canvas, &set, opts)) {
    *value ^= flag;
    return true;
  }
  return false;
}

static void ui_select_header(
    UiCanvasComp*       canvas,
    const String        label,
    const UiStatus      status,
    const bool          isOpen,
    const UiSelectOpts* opts) {

  ui_style_push(canvas);
  ui_interactable_frame_style(canvas, opts->frameColor, status);
  if (isOpen) {
    ui_style_outline(canvas, 3);
  }
  const UiFlags flags =
      isOpen ? (UiFlags_Interactable | UiFlags_InteractAllowSwitch)
             : (UiFlags_Interactable | UiFlags_InteractOnPress | UiFlags_InteractAllowSwitch);
  ui_canvas_draw_glyph(canvas, UiShape_Square, 0, flags);
  ui_style_pop(canvas);

  ui_style_push(canvas);

  ui_layout_push(canvas);
  ui_interactable_text_style(canvas, status);
  ui_layout_grow(canvas, UiAlign_MiddleCenter, ui_vector(-10, 0), UiBase_Absolute, Ui_X);
  ui_canvas_draw_text(canvas, label, opts->fontSize, UiAlign_MiddleLeft, UiFlags_None);

  ui_layout_inner(canvas, UiBase_Current, UiAlign_MiddleRight, ui_vector(20, 20), UiBase_Absolute);
  ui_canvas_draw_glyph(canvas, isOpen ? UiShape_ExpandLess : UiShape_ExpandMore, 0, UiFlags_None);
  ui_layout_pop(canvas);

  ui_style_pop(canvas);
}

typedef enum {
  UiSelectFlags_Changed = 1 << 0,
  UiSelectFlags_Hovered = 1 << 1,
} UiSelectFlags;

static UiSelectFlags ui_select_dropdown(
    UiCanvasComp*       canvas,
    i32*                input,
    const String*       options,
    const u32           optionCount,
    const UiSelectOpts* opts) {

  ui_layout_push(canvas);
  UiSelectFlags selectFlags = 0;
  for (u32 i = 0; i != optionCount; ++i) {
    const u32 optionIndex = opts->dir == Ui_Up ? (optionCount - 1 - i) : i;

    ui_layout_next(canvas, opts->dir, 2);
    const UiId id     = ui_canvas_id_peek(canvas);
    UiStatus   status = ui_canvas_elem_status(canvas, id);

    ui_style_push(canvas);
    ui_style_outline(canvas, 3);
    switch (status) {
    case UiStatus_Hovered:
      ui_style_color_with_mult(canvas, opts->dropFrameColor, 2);
      break;
    case UiStatus_Pressed:
    case UiStatus_Activated:
    case UiStatus_ActivatedAlt:
      ui_style_color_with_mult(canvas, opts->dropFrameColor, 3);
      break;
    case UiStatus_Idle:
      ui_style_color(canvas, opts->dropFrameColor);
      break;
    }
    ui_canvas_draw_glyph(
        canvas, UiShape_Square, 0, UiFlags_Interactable | UiFlags_InteractAllowSwitch);
    ui_style_pop(canvas);

    ui_layout_push(canvas);
    ui_layout_grow(canvas, UiAlign_MiddleCenter, ui_vector(-10, 0), UiBase_Absolute, Ui_X);

    ui_style_push(canvas);
    ui_interactable_text_style(canvas, status);
    ui_canvas_draw_text(canvas, options[optionIndex], opts->fontSize, UiAlign_MiddleLeft, 0);
    ui_style_pop(canvas);

    ui_layout_pop(canvas);

    if (status >= UiStatus_Hovered) {
      selectFlags |= UiSelectFlags_Hovered;
    }
    if (status == UiStatus_Activated) {
      *input = optionIndex;
      selectFlags |= UiSelectFlags_Changed;
    }

    if (status >= UiStatus_Hovered) {
      ui_canvas_interact_type(canvas, UiInteractType_Action);
    }
  }
  ui_layout_pop(canvas);
  return selectFlags;
}

bool ui_select_with_opts(
    UiCanvasComp*       canvas,
    i32*                input,
    const String*       options,
    const u32           optionCount,
    const UiSelectOpts* opts) {

  const UiId     headerId     = ui_canvas_id_peek(canvas);
  const bool     disabled     = (opts->flags & UiWidget_Disabled) != 0;
  const UiStatus headerStatus = disabled ? UiStatus_Idle : ui_canvas_elem_status(canvas, headerId);
  UiSelectFlags  selectFlags  = 0;

  if (headerStatus >= UiStatus_Hovered) {
    selectFlags |= UiSelectFlags_Hovered;
  }
  if (headerStatus == UiStatus_Activated) {
    ui_canvas_persistent_flags_toggle(canvas, headerId, UiPersistentFlags_Open);
  }
  const bool isOpen = (ui_canvas_persistent_flags(canvas, headerId) & UiPersistentFlags_Open) != 0;
  const bool outOfBounds   = *input < 0 || *input >= (i32)optionCount;
  const String headerLabel = outOfBounds ? string_lit("- Select -") : options[*input];

  ui_style_push(canvas);
  if (isOpen) {
    ui_style_layer(canvas, UiLayer_Overlay);
    ui_canvas_min_interact_layer(canvas, UiLayer_Overlay);
  }
  if (disabled) {
    ui_style_color_mult(canvas, g_uiDisabledMult);
  }
  ui_select_header(canvas, headerLabel, headerStatus, isOpen, opts);

  if (isOpen) {
    selectFlags |= ui_select_dropdown(canvas, input, options, optionCount, opts);
  } else {
    ui_canvas_id_skip(canvas, optionCount * 2);
  }
  if (selectFlags & UiSelectFlags_Changed || disabled) {
    ui_canvas_persistent_flags_unset(canvas, headerId, UiPersistentFlags_Open);
  }
  if (!(selectFlags & UiSelectFlags_Hovered) && ui_canvas_input_any(canvas)) {
    ui_canvas_persistent_flags_unset(canvas, headerId, UiPersistentFlags_Open);
  }

  if (!string_is_empty(opts->tooltip)) {
    ui_tooltip(canvas, headerId, opts->tooltip);
  }

  if (headerStatus >= UiStatus_Hovered) {
    ui_canvas_interact_type(canvas, UiInteractType_Action);
  }
  if (headerStatus == UiStatus_Activated || selectFlags & UiSelectFlags_Changed) {
    ui_canvas_sound(canvas, UiSoundType_Click);
  }

  ui_style_pop(canvas);
  return (selectFlags & UiSelectFlags_Changed) != 0;
}

static void ui_tooltip_background(
    UiCanvasComp* canvas, const UiDir dir, const UiAlign align, const UiRect lastTextRect) {
  const UiVector size = ui_vector(lastTextRect.width + 20, lastTextRect.height + 10);

  ui_layout_inner(canvas, UiBase_Input, align, size, UiBase_Absolute);
  ui_layout_move_dir(canvas, dir, 15, UiBase_Absolute);

  ui_style_color(canvas, ui_color_white);
  ui_style_outline(canvas, 3);

  ui_canvas_draw_glyph(canvas, UiShape_Circle, 5, UiFlags_None);
}

static void ui_tooltip_text(
    UiCanvasComp*        canvas,
    const UiDir          dir,
    const UiAlign        align,
    const String         text,
    const UiRect         lastRect,
    const UiTooltipOpts* opts) {

  ui_layout_inner(canvas, UiBase_Input, align, opts->maxSize, UiBase_Absolute);
  ui_layout_move_dir(canvas, dir, 25, UiBase_Absolute);
  ui_layout_move_dir(canvas, Ui_Down, 5, UiBase_Absolute);

  if (dir == Ui_Left) {
    /**
     * Because we always draw the text left aligned it needs to be offset if the tooltip should be
     * on the left side of the input.
     */
    ui_layout_move_dir(canvas, Ui_Right, opts->maxSize.width - lastRect.width, UiBase_Absolute);
  }

  ui_style_color(canvas, ui_color_black);
  ui_style_outline(canvas, 0);
  ui_style_variation(canvas, opts->variation);

  ui_canvas_draw_text(canvas, text, opts->fontSize, UiAlign_TopLeft, UiFlags_TrackRect);
}

static UiDir ui_tooltip_dir(UiCanvasComp* canvas) {
  const f32 halfCanvas = ui_canvas_resolution(canvas).width * 0.5f;
  return ui_canvas_input_pos(canvas).x > halfCanvas ? Ui_Left : Ui_Right;
}

static bool ui_tooltip_show(UiCanvasComp* canvas, const UiId id, const UiTooltipOpts* opts) {
  if (opts->flags & UiWidget_Disabled) {
    return false;
  }
  if (sentinel_check(id)) {
    return true; // Always show the tooltip if no id was provided.
  }
  if (ui_canvas_elem_status(canvas, id) != UiStatus_Hovered) {
    return false;
  }
  return ui_canvas_elem_status_duration(canvas, id) >= time_second;
}

bool ui_tooltip_with_opts(
    UiCanvasComp* canvas, const UiId id, const String text, const UiTooltipOpts* opts) {

  if (string_is_empty(text) || !ui_tooltip_show(canvas, id, opts)) {
    ui_canvas_id_skip(canvas, 2);
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
  const UiRect lastTextRect = ui_canvas_elem_rect(canvas, textId);
  const bool   firstFrame   = lastTextRect.width == 0;

  ui_layout_push(canvas);
  ui_style_push(canvas);
  ui_style_weight(canvas, UiWeight_Normal);

  /**
   * To draw the tooltip background we need to know the size of the text. We achieve this by using
   * the text rectangle of the last frame. If this is the first frame that we're drawing the tooltip
   * then we skip the background and draw the text invisible.
   */

  ui_style_layer(canvas, firstFrame ? UiLayer_OverlayInvisible : UiLayer_Overlay);
  if (firstFrame) {
    ui_canvas_id_skip(canvas, 1);
  } else {
    ui_tooltip_background(canvas, dir, align, lastTextRect);
  }
  ui_tooltip_text(canvas, dir, align, text, lastTextRect, opts);

  ui_style_pop(canvas);
  ui_layout_pop(canvas);
  return true;
}

bool ui_section_with_opts(UiCanvasComp* canvas, const UiSectionOpts* opts) {
  const UiId     iconId = ui_canvas_id_peek(canvas);
  const UiId     textId = iconId + 1;
  const UiStatus status =
      math_max(ui_canvas_elem_status(canvas, iconId), ui_canvas_elem_status(canvas, textId));
  if (status == UiStatus_Activated) {
    ui_canvas_persistent_flags_toggle(canvas, iconId, UiPersistentFlags_Open);
  }
  const bool isOpen = (ui_canvas_persistent_flags(canvas, iconId) & UiPersistentFlags_Open) != 0;

  ui_style_push(canvas);
  ui_style_weight(canvas, UiWeight_Bold);
  ui_interactable_text_style(canvas, status);

  ui_layout_push(canvas);
  ui_layout_inner(canvas, UiBase_Current, UiAlign_MiddleLeft, ui_vector(15, 15), UiBase_Absolute);
  ui_canvas_draw_glyph(
      canvas, isOpen ? UiShape_UnfoldLess : UiShape_UnfoldMore, 0, UiFlags_Interactable);
  ui_layout_pop(canvas);

  ui_layout_push(canvas);
  ui_layout_grow(canvas, UiAlign_MiddleRight, ui_vector(-15, 0), UiBase_Absolute, Ui_X);
  ui_canvas_draw_text(
      canvas, opts->label, opts->fontSize, UiAlign_MiddleLeft, UiFlags_Interactable);
  ui_layout_pop(canvas);

  if (status >= UiStatus_Hovered) {
    ui_canvas_interact_type(canvas, UiInteractType_Action);
  }
  if (status == UiStatus_Activated) {
    ui_canvas_sound(canvas, UiSoundType_Click);
  }

  ui_style_pop(canvas);
  return isOpen;
}

bool ui_textbox_with_opts(UiCanvasComp* canvas, DynString* text, const UiTextboxOpts* opts) {
  const UiId     frameId  = ui_canvas_id_peek(canvas);
  const UiId     textId   = frameId + 1;
  const bool     disabled = (opts->flags & UiWidget_Disabled) != 0;
  bool           editing  = ui_canvas_text_editor_active(canvas, textId);
  const UiStatus status   = disabled ? UiStatus_Idle : ui_canvas_elem_status(canvas, textId);

  // Draw frame.
  ui_style_push(canvas);
  if (editing) {
    ui_style_color_with_mult(canvas, opts->frameColor, 1.2f);
    ui_style_outline(canvas, 1);
  } else if (status >= UiStatus_Hovered) {
    ui_style_color_with_mult(canvas, opts->frameColor, 2);
    ui_style_outline(canvas, 3);
  } else {
    ui_style_color(canvas, opts->frameColor);
    ui_style_outline(canvas, 2);
  }
  ui_canvas_draw_glyph(canvas, UiShape_Square, 0, UiFlags_None);
  ui_style_pop(canvas);

  // Start editing on press.
  if (!editing && status == UiStatus_Activated) {
    const UiTextFilter filter = opts->type == UiTextbox_Digits ? UiTextFilter_DigitsOnly : 0;
    ui_canvas_text_editor_start(canvas, dynstring_view(text), textId, opts->maxTextLength, filter);
    ui_canvas_sound(canvas, UiSoundType_Click);
    editing = true;
  }

  const UiFlags flags =
      UiFlags_AllowWordBreak | UiFlags_SingleLine | UiFlags_Interactable | UiFlags_InteractOnPress;
  bool changed = false;

  // Draw text.
  static const f32 g_textInset = 3;
  ui_layout_push(canvas);
  ui_layout_grow(canvas, UiAlign_MiddleRight, ui_vector(-g_textInset, 0), UiBase_Absolute, Ui_X);
  ui_style_push(canvas);
  if (disabled) {
    ui_style_color_mult(canvas, g_uiDisabledMult);
  }
  if (editing) {
    const String newText = ui_canvas_text_editor_result(canvas);
    if (!string_eq(dynstring_view(text), newText)) {
      dynstring_clear(text);
      dynstring_append(text, newText);
      changed = true;
    }
    ui_canvas_draw_text_editor(canvas, opts->fontSize, UiAlign_MiddleLeft, flags);
  } else {
    const String inputText = text->size ? dynstring_view(text) : opts->placeholder;
    ui_canvas_draw_text(canvas, inputText, opts->fontSize, UiAlign_MiddleLeft, flags);
  }
  ui_style_pop(canvas);
  ui_layout_pop(canvas);

  if (!string_is_empty(opts->tooltip)) {
    ui_tooltip(canvas, textId, opts->tooltip, .flags = editing ? UiWidget_Disabled : 0);
  }

  if (status >= UiStatus_Hovered) {
    ui_canvas_interact_type(canvas, UiInteractType_Text);
  }

  return changed || ((opts->flags & UiWidget_DirtyWhileEditing) && editing);
}

bool ui_numbox_with_opts(UiCanvasComp* canvas, f64* input, const UiNumboxOpts* opts) {
  DynString text = dynstring_create_over(mem_stack(64));
  format_write_f64(&text, *input, &format_opts_float(.maxDecDigits = 4));
  if (ui_textbox(
          canvas,
          &text,
          .flags         = opts->flags,
          .type          = UiTextbox_Digits,
          .fontSize      = opts->fontSize,
          .maxTextLength = 64,
          .frameColor    = opts->frameColor,
          .tooltip       = opts->tooltip)) {
    format_read_f64(dynstring_view(&text), input);
    if (opts->step > f64_epsilon) {
      *input = math_round_nearest_f64(*input / opts->step) * opts->step;
    }
    *input = math_clamp_f64(*input, opts->min, opts->max);
    return true;
  }
  return false;
}

bool ui_durbox_with_opts(UiCanvasComp* canvas, TimeDuration* input, const UiDurboxOpts* opts) {
  DynString text = dynstring_create_over(mem_stack(64));
  format_write_time_duration_pretty(&text, *input, &format_opts_float(.maxDecDigits = 4));
  if (ui_textbox(
          canvas,
          &text,
          .flags         = opts->flags,
          .fontSize      = opts->fontSize,
          .maxTextLength = 64,
          .frameColor    = opts->frameColor,
          .tooltip       = opts->tooltip)) {
    format_read_time_duration(dynstring_view(&text), input);
    *input = math_clamp_i64(*input, opts->min, opts->max);
    return true;
  }
  return false;
}

void ui_circle_with_opts(UiCanvasComp* canvas, const UiVector pos, const UiCircleOpts* opts) {
  const UiVector size = ui_vector(opts->radius * 2, opts->radius * 2);

  ui_layout_push(canvas);
  ui_layout_set_pos(canvas, opts->base, pos, opts->base);
  ui_layout_resize(canvas, UiAlign_MiddleCenter, size, UiBase_Absolute, Ui_XY);
  ui_canvas_draw_glyph(canvas, UiShape_Circle, opts->maxCorner, UiFlags_None);
  ui_layout_pop(canvas);
}

void ui_line_with_opts(
    UiCanvasComp* canvas, const UiVector from, const UiVector to, const UiLineOpts* opts) {

  const UiVector center   = ui_vector((to.x + from.x) * 0.5f, (to.y + from.y) * 0.5f);
  const UiVector delta    = ui_vector(to.x - from.x, to.y - from.y);
  const f32      magSqr   = delta.x * delta.x + delta.y * delta.y;
  const f32      mag      = magSqr > f32_epsilon ? math_sqrt_f32(magSqr) : 0;
  const f32      angleRad = magSqr > f32_epsilon ? -math_atan2_f32(delta.y, delta.x) : 0;

  /**
   * NOTE: The following logic has an issue when using a different base then 'Absolute' (for example
   * 'Current' or 'Container') and the parent isn't square, as we always use the X axis of the
   * parent for the scale of the line.
   */

  ui_layout_push(canvas);
  ui_layout_set_pos(canvas, opts->base, center, opts->base);
  ui_layout_resize(canvas, UiAlign_MiddleCenter, ui_vector(mag, 0), opts->base, Ui_X);
  ui_layout_resize(canvas, UiAlign_MiddleCenter, ui_vector(0, opts->width), UiBase_Absolute, Ui_Y);
  ui_canvas_draw_glyph_rotated(canvas, UiShape_Square, 10, angleRad, UiFlags_None);
  ui_layout_pop(canvas);
}
