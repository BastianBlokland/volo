#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_color.h"

ecs_comp_extern(SceneTextComp);

/**
 * Create a new text entity.
 */
EcsEntityId scene_text_create(EcsWorld*, f32 x, f32 y, f32 size, GeoColor, String);

/**
 * Update an existing text entity.
 */
void scene_text_update_color(SceneTextComp*, GeoColor);
void scene_text_update_position(SceneTextComp*, f32 x, f32 y);
void scene_text_update_size(SceneTextComp*, f32 size);
void scene_text_update_str(SceneTextComp*, String);
