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
  UiLayer_Normal,
  UiLayer_Overlay,
} UiLayer;

typedef enum {
  UiFlags_None         = 0,
  UiFlags_Interactable = 1 << 0,
} UiFlags;

ecs_comp_extern(UiCanvasComp);

EcsEntityId ui_canvas_create(EcsWorld*, EcsEntityId window);
void        ui_canvas_reset(UiCanvasComp*);

/**
 * Query / manipulate values in the ui-id sequence.
 */
UiId ui_canvas_id_peek(const UiCanvasComp*);
void ui_canvas_id_skip(UiCanvasComp*);

/**
 * Query information about a specific element.
 * NOTE: Requires cross frame consistency of identifiers.
 */
UiStatus     ui_canvas_elem_status(const UiCanvasComp*, UiId);
TimeDuration ui_canvas_elem_status_duration(const UiCanvasComp*, UiId);
UiRect       ui_canvas_elem_rect(const UiCanvasComp*, UiId);

/**
 * Query information about the window.
 */
UiVector ui_canvas_window_size(const UiCanvasComp*);

/**
 * Query input information.
 */
UiVector ui_canvas_input_delta(const UiCanvasComp*);
UiVector ui_canvas_input_pos(const UiCanvasComp*);

/**
 * Push / Pop an element to / from the rectangle stack.
 * Usefull for local changes to the current rectangle with an easy way to restore the previous.
 */
void ui_canvas_rect_push(UiCanvasComp*);
void ui_canvas_rect_pop(UiCanvasComp*);

/**
 * Update the current rect.
 */
void ui_canvas_rect_pos(UiCanvasComp*, UiBase origin, UiVector offset, UiBase units, UiAxis);
void ui_canvas_rect_size(UiCanvasComp*, UiVector size, UiBase units, UiAxis);
void ui_canvas_rect_size_to(UiCanvasComp*, UiBase origin, UiVector offset, UiBase units, UiAxis);

/**
 * Push / Pop an element to / from the container stack.
 * When pushing a new container the current rectangle value will be used.
 */
void ui_canvas_container_push(UiCanvasComp*);
void ui_canvas_container_pop(UiCanvasComp*);

/**
 * Push / Pop an element to / from the style stack.
 * Usefull for local changes to the current style with an easy way to restore the previous.
 */
void ui_canvas_style_push(UiCanvasComp*);
void ui_canvas_style_pop(UiCanvasComp*);

/**
 * Update the current style.
 */
void ui_canvas_style_color(UiCanvasComp*, UiColor);
void ui_canvas_style_outline(UiCanvasComp*, u8 outline);
void ui_canvas_style_layer(UiCanvasComp*, UiLayer);

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
