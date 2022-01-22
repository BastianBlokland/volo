#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

ecs_comp_extern(SceneTextComp);

/**
 * Create a new text entity.
 */
EcsEntityId scene_text_create(EcsWorld*, f32 x, f32 y, f32 size, String);

/**
 * Update an existing text entity.
 */
void scene_text_update(SceneTextComp*, f32 x, f32 y, f32 size, String);
