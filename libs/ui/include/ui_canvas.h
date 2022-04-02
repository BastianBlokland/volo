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

typedef enum {
  UiFlags_None                = 0,
  UiFlags_Interactable        = 1 << 0,
  UiFlags_InteractOnPress     = 1 << 1, // Activate on 'Press' instead of 'Release'.
  UiFlags_InteractAllowSwitch = 1 << 2, // Allow switching targets while holding input down.
  UiFlags_TrackRect           = 1 << 3,
} UiFlags;

typedef enum {
  UiPersistentFlags_Open = 1 << 0,
} UiPersistentFlags;

ecs_comp_extern(UiCanvasComp);

EcsEntityId ui_canvas_create(EcsWorld*, EcsEntityId window);
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
 * Query / manipulate values in the ui-id sequence.
 */
UiId ui_canvas_id_peek(const UiCanvasComp*);
void ui_canvas_id_skip(UiCanvasComp*, u64 count);
void ui_canvas_id_next_block(UiCanvasComp*);

/**
 * Query general canvas information.
 */
UiStatus ui_canvas_status(const UiCanvasComp*);
UiVector ui_canvas_resolution(const UiCanvasComp*);
bool     ui_canvas_input_any(const UiCanvasComp*);
UiVector ui_canvas_input_delta(const UiCanvasComp*);
UiVector ui_canvas_input_pos(const UiCanvasComp*);

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

/**
 * Draw a single glyph in the current rectangle.
 * The glyph will be stretched using 9-slice scaling to fill the rectangle, 'maxCorner' is used to
 * control the size of the 9-slice corner.
 */
UiId ui_canvas_draw_glyph(UiCanvasComp*, Unicode, u16 maxCorner, UiFlags);
