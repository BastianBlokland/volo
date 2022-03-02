#pragma once
#include "core_unicode.h"
#include "ecs_entity.h"
#include "ecs_module.h"
#include "ui_color.h"
#include "ui_vector.h"

typedef u64 UiId;

typedef enum {
  UiOrigin_Current,
  UiOrigin_Cursor,
  UiOrigin_WindowBottomLeft,
  UiOrigin_WindowBottomRight,
  UiOrigin_WindowTopLeft,
  UiOrigin_WindowTopRight,
} UiOrigin;

typedef enum {
  UiUnits_Current,
  UiUnits_Absolute,
  UiUnits_Window,
} UiUnits;

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

typedef enum {
  UiStatus_Idle,
  UiStatus_Hovered,
  UiStatus_Down,
  UiStatus_Activated,
} UiStatus;

typedef enum {
  UiFlags_None         = 0,
  UiFlags_Interactable = 1 << 0,
} UiFlags;

ecs_comp_extern(UiCanvasComp);

EcsEntityId ui_canvas_create(EcsWorld*, EcsEntityId window);
void        ui_canvas_reset(UiCanvasComp*);

UiId     ui_canvas_next_id(const UiCanvasComp*);
UiStatus ui_canvas_status(const UiCanvasComp*, UiId);

void ui_canvas_move(UiCanvasComp*, UiVector, UiOrigin, UiUnits);
void ui_canvas_size(UiCanvasComp*, UiVector, UiUnits);
void ui_canvas_size_to(UiCanvasComp*, UiVector, UiOrigin, UiUnits);
void ui_canvas_style(UiCanvasComp*, UiColor, u8 outline);

UiId ui_canvas_draw_text(UiCanvasComp*, String text, u16 fontSize, UiTextAlign, UiFlags);
UiId ui_canvas_draw_glyph(UiCanvasComp*, Unicode, u16 maxCorner, UiFlags);
