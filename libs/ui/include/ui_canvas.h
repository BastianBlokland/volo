#pragma once
#include "core_unicode.h"
#include "ecs_entity.h"
#include "ecs_module.h"
#include "ui_color.h"
#include "ui_vector.h"

typedef u64 UiElementId;

ecs_comp_extern(UiCanvasComp);

UiCanvasComp* ui_canvas_create(EcsWorld*, EcsEntityId);
void          ui_canvas_reset(UiCanvasComp*);

void ui_canvas_set_size(UiCanvasComp*, UiVector);
void ui_canvas_set_color(UiCanvasComp*, UiColor);

UiElementId ui_canvas_draw_glyph(UiCanvasComp*, Unicode);
