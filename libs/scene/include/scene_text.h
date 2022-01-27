#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_color.h"

ecs_comp_extern(SceneTextComp);

SceneTextComp* scene_text_add(EcsWorld*, EcsEntityId);
void           scene_text_update_color(SceneTextComp*, GeoColor);
void           scene_text_update_position(SceneTextComp*, f32 x, f32 y);
void           scene_text_update_size(SceneTextComp*, f32 size);
void           scene_text_update_str(SceneTextComp*, String);
