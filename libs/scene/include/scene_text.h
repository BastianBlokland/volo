#pragma once
#include "core_format.h"
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_color.h"

typedef enum {
  SceneTextColor_A,
  SceneTextColor_B,
  SceneTextColor_C,
  SceneTextColor_D,
} SceneTextColor;

ecs_comp_extern(SceneTextComp);

SceneTextComp* scene_text_add(EcsWorld*, EcsEntityId);
void           scene_text_update_palette(SceneTextComp*, SceneTextColor, GeoColor);
void           scene_text_update_position(SceneTextComp*, f32 x, f32 y);
void           scene_text_update_size(SceneTextComp*, f32 size);
void           scene_text_update_str(SceneTextComp*, String);

/**
 * Retrieve a formatting argument that can be placed in text strings to switch to a different color
 * from the text palette.
 */
FormatArg scene_text_color(SceneTextColor);
