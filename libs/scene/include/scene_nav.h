#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_nav.h"

/**
 * Global navigation environment.
 */
ecs_comp_extern(SceneNavEnvComp);
ecs_comp_extern_public(SceneNavStatsComp) {
  u32 blockerCount;
  u32 pathCount, pathOutputCells, pathItrCells, pathItrEnqueues;
  u32 gridDataSize, workerDataSize;
};

/**
 * Mark the entity as blocking navigation.
 */
ecs_comp_extern(SceneNavBlockerComp);

/**
 * Navigation agent.
 */
ecs_comp_extern_public(SceneNavAgentComp) { GeoVector target; };
ecs_comp_extern_public(SceneNavPathComp) {
  GeoNavCell* cells;
  u32         cellCount;
};

void scene_nav_add_blocker(EcsWorld*, EcsEntityId);
void scene_nav_add_agent(EcsWorld*, EcsEntityId, GeoVector target);

GeoNavRegion scene_nav_bounds(const SceneNavEnvComp*);
GeoVector    scene_nav_cell_size(const SceneNavEnvComp*);

GeoVector scene_nav_position(const SceneNavEnvComp*, GeoNavCell);
GeoVector scene_nav_size(const SceneNavEnvComp*, GeoNavCell);
GeoBox    scene_nav_box(const SceneNavEnvComp*, GeoNavCell);
bool      scene_nav_blocked(const SceneNavEnvComp*, GeoNavCell);
