#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_ray.h"

ecs_comp_extern(SceneTerrainComp);

void scene_terrain_init(EcsWorld*, String graphicId, String heightmapId);

/**
 * Check if the terrain is loaded.
 * NOTE: Returns false if a null terrain is provided.
 */
bool scene_terrain_loaded(const SceneTerrainComp*);

/**
 * Retrieve the terrain's version number.
 * Version is incremented when the terrain is updated and can be used to invalidate cached data.
 */
u32 scene_terrain_version(const SceneTerrainComp*);

EcsEntityId scene_terrain_graphic(const SceneTerrainComp*);
f32         scene_terrain_size(const SceneTerrainComp*);
f32         scene_terrain_height_scale(const SceneTerrainComp*);

/**
 * Compute the intersection of the given ray with the terrain.
 * Returns the time along the ray at which the intersection occurred or negative if no intersection
 * occurred.
 */
f32 scene_terrain_intersect_ray(const SceneTerrainComp*, const GeoRay*, f32 maxDist);

/**
 * Sample the terrain height at the given coordinate.
 */
f32 scene_terrain_height(const SceneTerrainComp*, GeoVector position);

/**
 * Snap the given position to the terrain.
 */
void scene_terrain_snap(const SceneTerrainComp*, GeoVector* position);
