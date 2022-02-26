#pragma once
#include "core_unicode.h"
#include "ecs_entity.h"
#include "ecs_module.h"
#include "ui_color.h"
#include "ui_vector.h"

typedef u64 UiElementId;

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

ecs_comp_extern(UiCanvasComp);

EcsEntityId ui_canvas_create(EcsWorld*, EcsEntityId window);
void        ui_canvas_reset(UiCanvasComp*);

void ui_canvas_move(UiCanvasComp*, UiVector, UiOrigin, UiUnits);
void ui_canvas_size(UiCanvasComp*, UiVector, UiUnits);
void ui_canvas_size_to(UiCanvasComp*, UiVector, UiOrigin, UiUnits);
void ui_canvas_style(UiCanvasComp*, UiColor, u8 outline);

UiElementId ui_canvas_draw_text(UiCanvasComp*, String text, u16 fontSize);
UiElementId ui_canvas_draw_glyph(UiCanvasComp*, Unicode, u16 maxCorner);
UiElementId ui_canvas_draw_square(UiCanvasComp*);
UiElementId ui_canvas_draw_circle(UiCanvasComp*, u16 maxCorner);
