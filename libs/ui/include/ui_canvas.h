#pragma once
#include "core_unicode.h"
#include "ecs_entity.h"
#include "ecs_module.h"
#include "ui_color.h"
#include "ui_vector.h"

typedef u64 UiElementId;

typedef enum {
  UiOrigin_BottomLeft,
  UiOrigin_BottomRight,
  UiOrigin_TopLeft,
  UiOrigin_TopRight,
} UiOrigin;

typedef enum {
  UiUnits_Absolute,
  UiUnits_Window,
} UiUnits;

ecs_comp_extern(UiCanvasComp);

EcsEntityId ui_canvas_create(EcsWorld*, EcsEntityId window);
void        ui_canvas_reset(UiCanvasComp*);

void ui_canvas_set_pos(UiCanvasComp*, UiVector, UiOrigin, UiUnits);
void ui_canvas_set_size(UiCanvasComp*, UiVector, UiUnits);
void ui_canvas_set_color(UiCanvasComp*, UiColor);

UiElementId ui_canvas_draw_glyph(UiCanvasComp*, Unicode);
