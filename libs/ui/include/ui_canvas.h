#pragma once
#include "core_time.h"
#include "core_unicode.h"
#include "ecs_entity.h"
#include "ecs_module.h"
#include "ui_color.h"
#include "ui_rect.h"
#include "ui_units.h"

/**
 * Identifier for an ui-element.
 * NOTE: For cross frame persistency its important that the same logical elements get the same
 * identifier in different frames.
 */
typedef u64 UiId;

typedef enum {
  UiStatus_Idle,
  UiStatus_Hovered,
  UiStatus_Pressed,
  UiStatus_Activated,
} UiStatus;

/**
 * Indicates the possible user interaction.
 */
typedef enum {
  UiInteractType_None,   // No interaction possible (eg. normal cursor).
  UiInteractType_Action, // Action (click) interaction possible (eg. hand cursor).
  UiInteractType_Resize, // Resize (drag) interaction possible (eg. diag resize cursor).
  UiInteractType_Text,   // Text editing interaction possible (eg. text cursor).
} UiInteractType;

typedef enum {
  UiTextFilter_Readonly   = 1 << 0,
  UiTextFilter_DigitsOnly = 1 << 1,
} UiTextFilter;

typedef enum {
  UiFlags_None                = 0,
  UiFlags_Interactable        = 1 << 0,
  UiFlags_InteractOnPress     = 1 << 1, // Activate on 'Press' instead of 'Release'.
  UiFlags_InteractAllowSwitch = 1 << 2, // Allow switching targets while holding input down.
  UiFlags_TrackRect           = 1 << 3, // Allows querying the elem with 'ui_canvas_elem_rect()'.
  UiFlags_TrackTextInfo       = 1 << 4, // Internal use only atm.
  UiFlags_AllowWordBreak      = 1 << 5, // Allow breaking up text in the middle of words.
  UiFlags_SingleLine          = 1 << 6, // Only draw the first line of the text.
  UiFlags_TightTextRect       = 1 << 7, // Clamp the rectangle to the text size.
} UiFlags;

typedef enum {
  UiPersistentFlags_Open = 1 << 0,
} UiPersistentFlags;

typedef enum {
  UiCanvasCreateFlags_None    = 0,
  UiCanvasCreateFlags_ToFront = 1 << 0,
  UiCanvasCreateFlags_ToBack  = 1 << 1,
} UiCanvasCreateFlags;

ecs_comp_extern(UiCanvasComp);

EcsEntityId ui_canvas_create(EcsWorld*, EcsEntityId window, UiCanvasCreateFlags);
void        ui_canvas_reset(UiCanvasComp*);

/**
 * Manipulate the render order with respect to other canvasses.
 */
void ui_canvas_to_front(UiCanvasComp*);
void ui_canvas_to_back(UiCanvasComp*);

/**
 * Ignore interactions below the given layer.
 * NOTE: Is cleared on canvas reset.
 */
void ui_canvas_min_interact_layer(UiCanvasComp*, UiLayer);

/**
 * Set the possible interaction type (used to provide user feedback, eg a different cursor).
 */
void ui_canvas_interact_type(UiCanvasComp*, UiInteractType);

/**
 * Query / manipulate values in the ui-id sequence.
 */
UiId ui_canvas_id_peek(const UiCanvasComp*);
void ui_canvas_id_skip(UiCanvasComp*, u64 count);
void ui_canvas_id_block_next(UiCanvasComp*);
void ui_canvas_id_block_index(UiCanvasComp*, u32 index); // Set explicit idx in current block.
void ui_canvas_id_block_string(UiCanvasComp*, String);   // Set explicit idx based on a string.

/**
 * Query general canvas information.
 */
UiStatus ui_canvas_status(const UiCanvasComp*);
UiVector ui_canvas_resolution(const UiCanvasComp*);
bool     ui_canvas_input_any(const UiCanvasComp*);
UiVector ui_canvas_input_delta(const UiCanvasComp*);
UiVector ui_canvas_input_pos(const UiCanvasComp*);
UiVector ui_canvas_input_scroll(const UiCanvasComp*);

/**
 * Query information about a specific element.
 * NOTE: Requires cross frame consistency of identifiers.
 */
UiStatus     ui_canvas_elem_status(const UiCanvasComp*, UiId);
TimeDuration ui_canvas_elem_status_duration(const UiCanvasComp*, UiId);
UiRect       ui_canvas_elem_rect(const UiCanvasComp*, UiId);

/**
 * Get or set persistent element state.
 */
UiPersistentFlags ui_canvas_persistent_flags(const UiCanvasComp*, UiId);
void              ui_canvas_persistent_flags_set(UiCanvasComp*, UiId, UiPersistentFlags);
void              ui_canvas_persistent_flags_unset(UiCanvasComp*, UiId, UiPersistentFlags);
void              ui_canvas_persistent_flags_toggle(UiCanvasComp*, UiId, UiPersistentFlags);

/**
 * Draw text in the current rectangle.
 */
UiId ui_canvas_draw_text(UiCanvasComp*, String text, u16 fontSize, UiAlign, UiFlags);
UiId ui_canvas_draw_text_editor(UiCanvasComp*, u16 fontSize, UiAlign, UiFlags);

/**
 * Interact with the canvas's text editor.
 */
void   ui_canvas_text_editor_start(UiCanvasComp*, String text, usize maxLen, UiId, UiTextFilter);
void   ui_canvas_text_editor_stop(UiCanvasComp*);
bool   ui_canvas_text_editor_active(UiCanvasComp*, UiId);
String ui_canvas_text_editor_result(UiCanvasComp*);

/**
 * Draw a single glyph in the current rectangle.
 * The glyph will be stretched using 9-slice scaling to fill the rectangle, 'maxCorner' is used to
 * control the size of the 9-slice corner.
 */
UiId ui_canvas_draw_glyph(UiCanvasComp*, Unicode, u16 maxCorner, UiFlags);
