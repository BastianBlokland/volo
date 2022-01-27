#pragma once
#include "core_format.h"
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_color.h"

typedef enum {
  TextPalette_A,
  TextPalette_B,
  TextPalette_C,
  TextPalette_D,
} TextPalette;

ecs_comp_extern(SceneTextComp);

SceneTextComp* scene_text_add(EcsWorld*, EcsEntityId);
void           scene_text_update_palette(SceneTextComp*, TextPalette, GeoColor);
void           scene_text_update_position(SceneTextComp*, f32 x, f32 y);
void           scene_text_update_size(SceneTextComp*, f32 size);
void           scene_text_update_str(SceneTextComp*, String);

/**
 * Retrieve a formatting argument that can be placed in text to switch to a different palette.
 */
FormatArg fmt_text_palette(TextPalette);
