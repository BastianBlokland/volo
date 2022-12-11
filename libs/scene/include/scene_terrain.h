#pragma once
#include "ecs_module.h"
#include "geo_vector.h"

ecs_comp_extern(SceneTerrainComp);

void scene_terrain_init(EcsWorld*, String heightmapId);

/**
 * Check if the terrain is loaded.
 * NOTE: Returns false if a null terrain is provided.
 */
bool scene_terrain_loaded(const SceneTerrainComp*);

/**
 * Sample the terrain height at the given coordinate.
 */
f32 scene_terrain_height(const SceneTerrainComp*, GeoVector position);
