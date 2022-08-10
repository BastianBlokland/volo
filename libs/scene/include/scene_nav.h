#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_nav.h"

/**
 * Global navigation environment.
 */
ecs_comp_extern(SceneNavEnvComp);
ecs_comp_extern_public(SceneNavStatsComp) { u32 gridStats[GeoNavStat_Count]; };

/**
 * Navigation blocker.
 */

ecs_comp_extern(SceneNavBlockerComp);

/**
 * Navigation agent.
 */

typedef enum {
  SceneNavAgent_Moving = 1 << 0,
} SceneNavAgentFlags;

ecs_comp_extern_public(SceneNavAgentComp) {
  SceneNavAgentFlags flags;
  GeoVector          target;
};
ecs_comp_extern_public(SceneNavPathComp) {
  GeoNavCell* cells;
  u32         cellCount;
};

void scene_nav_move_to(SceneNavAgentComp*, GeoVector target);

void               scene_nav_add_blocker(EcsWorld*, EcsEntityId);
SceneNavAgentComp* scene_nav_add_agent(EcsWorld*, EcsEntityId);

GeoNavRegion scene_nav_bounds(const SceneNavEnvComp*);
GeoVector    scene_nav_cell_size(const SceneNavEnvComp*);

GeoVector    scene_nav_position(const SceneNavEnvComp*, GeoNavCell);
GeoVector    scene_nav_size(const SceneNavEnvComp*, GeoNavCell);
GeoBox       scene_nav_box(const SceneNavEnvComp*, GeoNavCell);
GeoNavRegion scene_nav_region(const SceneNavEnvComp*, GeoNavCell, u16 radius);
bool         scene_nav_blocked(const SceneNavEnvComp*, GeoNavCell);
bool         scene_nav_occupied(const SceneNavEnvComp*, GeoNavCell);
bool         scene_nav_occupied_moving(const SceneNavEnvComp*, GeoNavCell);
GeoNavCell   scene_nav_at_position(const SceneNavEnvComp*, GeoVector);

/**
 * Query the separation force at the given position.
 * NOTE: EntityId is used to avoid trying to separate from itself.
 */
GeoVector scene_nav_separation_force(const SceneNavEnvComp*, EcsEntityId, GeoVector position);
