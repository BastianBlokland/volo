#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo.h"

ecs_comp_extern(SceneTerrainComp);

bool scene_terrain_loaded(const SceneTerrainComp*);

/**
 * Retrieve the terrain's version number.
 * Version is incremented when the terrain is updated and can be used to invalidate cached data.
 */
u32  scene_terrain_version(const SceneTerrainComp*);
bool scene_terrain_updated(const SceneTerrainComp*);

EcsEntityId scene_terrain_resource_asset(const SceneTerrainComp*);
EcsEntityId scene_terrain_resource_graphic(const SceneTerrainComp*);
EcsEntityId scene_terrain_resource_heightmap(const SceneTerrainComp*);

GeoColor scene_terrain_minimap_color_low(const SceneTerrainComp*);  // In linear color space.
GeoColor scene_terrain_minimap_color_high(const SceneTerrainComp*); // In linear color space.

f32    scene_terrain_size(const SceneTerrainComp*);
f32    scene_terrain_play_size(const SceneTerrainComp*);
f32    scene_terrain_height_max(const SceneTerrainComp*);
GeoBox scene_terrain_bounds(const SceneTerrainComp*);
GeoBox scene_terrain_play_bounds(const SceneTerrainComp*);

/**
 * Compute the intersection of the given ray with the terrain.
 * Returns the time along the ray at which the intersection occurred or negative if no intersection
 * occurred.
 */
f32 scene_terrain_intersect_ray(const SceneTerrainComp*, const GeoRay*, f32 maxDist);

/**
 * Compute the terrain's normal vector at the given position.
 * NOTE: Does not interpolate so the normal is not continuous over the terrain surface.
 */
GeoVector scene_terrain_normal(const SceneTerrainComp*, GeoVector position);

/**
 * Sample the terrain height at the given coordinate.
 */
f32 scene_terrain_height(const SceneTerrainComp*, GeoVector position);

/**
 * Snap the given position to the terrain.
 */
void scene_terrain_snap(const SceneTerrainComp*, GeoVector* position);
