#pragma once
#include "core_unicode.h"
#include "ecs_entity.h"
#include "ecs_module.h"
#include "ui_color.h"
#include "ui_vector.h"

/**
 * Identifier for an ui-element.
 * NOTE: For cross frame persistency its important that the same logical elements get the same
 * identifier in different frames.
 */
typedef u64 UiId;

/**
 * Ui coordinate origin.
 */
typedef enum {
  UiOrigin_Current, // Currently active position.
  UiOrigin_Cursor,
  UiOrigin_WindowBottomLeft,
  UiOrigin_WindowBottomRight,
  UiOrigin_WindowTopLeft,
  UiOrigin_WindowTopRight,
} UiOrigin;

/**
 * Ui coordinate scale.
 * For example 0.5 Window units means the middle of the window.
 */
typedef enum {
  UiUnits_Current,
  UiUnits_Absolute,
  UiUnits_Window,
} UiUnits;

/**
 * Text alignment.
 * Controls how text will be layed out relative to the active rectangle.
 */
typedef enum {
  UiTextAlign_TopLeft,
  UiTextAlign_TopCenter,
  UiTextAlign_TopRight,
  UiTextAlign_MiddleLeft,
  UiTextAlign_MiddleCenter,
  UiTextAlign_MiddleRight,
  UiTextAlign_BottomLeft,
  UiTextAlign_BottomCenter,
  UiTextAlign_BottomRight,
} UiTextAlign;

/**
 * Interaction status.
 */
typedef enum {
  UiStatus_Idle,
  UiStatus_Hovered,
  UiStatus_Pressed,
  UiStatus_Activated,
} UiStatus;

typedef enum {
  Ui_X  = 1 << 0,
  Ui_Y  = 1 << 1,
  Ui_XY = Ui_X | Ui_Y,
} UiAxis;

typedef enum {
  UiFlags_None         = 0,
  UiFlags_Interactable = 1 << 0,
} UiFlags;

ecs_comp_extern(UiCanvasComp);

EcsEntityId ui_canvas_create(EcsWorld*, EcsEntityId window);
void        ui_canvas_reset(UiCanvasComp*);

/**
 * Query the value of the next ui-id in the sequence.
 */
UiId ui_canvas_next_id(const UiCanvasComp*);

/**
 * Query the interaction status of a specific element.
 * NOTE: Requires cross frame consistency of identifiers.
 */
UiStatus ui_canvas_status(const UiCanvasComp*, UiId);

/**
 * Push / Pop an element to / from the rectangle stack.
 * Usefull for local changes to the current rectangle with an easy way to restore the previous.
 */
void ui_canvas_rect_push(UiCanvasComp*);
void ui_canvas_rect_pop(UiCanvasComp*);

/**
 * Update the current rect.
 */
void ui_canvas_rect_move(UiCanvasComp*, UiVector, UiOrigin, UiUnits, UiAxis);
void ui_canvas_rect_resize(UiCanvasComp*, UiVector, UiUnits, UiAxis);
void ui_canvas_rect_resize_to(UiCanvasComp*, UiVector, UiOrigin, UiUnits, UiAxis);

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

/**
 * Draw text in the current rectangle.
 */
UiId ui_canvas_draw_text(UiCanvasComp*, String text, u16 fontSize, UiTextAlign, UiFlags);

/**
 * Draw a single glyph in the current rectangle.
 * The glyph will be stretched using 9-slice scaling to fill the rectangle, 'maxCorner' is used to
 * control the size of the 9-slice corner.
 */
UiId ui_canvas_draw_glyph(UiCanvasComp*, Unicode, u16 maxCorner, UiFlags);
