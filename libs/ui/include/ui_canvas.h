#pragma once
#include "core_unicode.h"
#include "ecs_entity.h"
#include "ecs_module.h"
#include "ui_rect.h"

typedef u64 UiId;

ecs_comp_extern(UiCanvasComp);

UiCanvasComp* ui_canvas_create(EcsWorld*, EcsEntityId);
void          ui_canvas_reset(UiCanvasComp*);
UiId          ui_canvas_draw_glyph(UiCanvasComp*, Unicode);
