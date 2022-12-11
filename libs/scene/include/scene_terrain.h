#pragma once
#include "ecs_module.h"

ecs_comp_extern(SceneTerrainComp);

void scene_terrain_init(EcsWorld*, String terrainGraphic);
