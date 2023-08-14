#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_nav.h"

// Forward declare from 'core_time.h'.
typedef i64 TimeDuration;

/**
 * Global navigation environment.
 */
ecs_comp_extern(SceneNavEnvComp);
ecs_comp_extern_public(SceneNavStatsComp) { u32 gridStats[GeoNavStat_Count]; };

/**
 * Navigation blocker.
 */

typedef enum {
  SceneNavBlockerFlags_RegisteredBlocker = 1 << 0,
} SceneNavBlockerFlags;

ecs_comp_extern_public(SceneNavBlockerComp) {
  SceneNavBlockerFlags flags;
  u32                  hash;      // Hash to detect a dirty blocker; automatically generated
  GeoNavBlockerId      blockerId; // Registered blocker id; automatically generated.
};

/**
 * Navigation agent.
 */

typedef enum {
  SceneNavAgent_Traveling = 1 << 0,
  SceneNavAgent_Stop      = 1 << 1,
} SceneNavAgentFlags;

ecs_comp_extern_public(SceneNavAgentComp) {
  SceneNavAgentFlags flags;
  EcsEntityId        targetEntity;
  GeoVector          targetPos;
};

ecs_comp_extern_public(SceneNavPathComp) {
  GeoNavCell*  cells;
  u32          cellCount;
  u32          currentTargetIndex; // Index in the path we are currently moving towards.
  GeoVector    destination;
  TimeDuration nextRefreshTime;
};

ecs_comp_extern_public(SceneNavRequestComp) {
  EcsEntityId targetEntity;
  GeoVector   targetPos;
};

void scene_nav_move_to(SceneNavAgentComp*, GeoVector target);
void scene_nav_move_to_entity(SceneNavAgentComp*, EcsEntityId target);
void scene_nav_stop(SceneNavAgentComp*);

/**
 * Initialize navigation agents and blockers.
 */
void               scene_nav_add_blocker(EcsWorld*, EcsEntityId);
SceneNavAgentComp* scene_nav_add_agent(EcsWorld*, EcsEntityId);

/**
 * Retrieve global navigation settings.
 */
GeoNavRegion scene_nav_bounds(const SceneNavEnvComp*);
GeoVector    scene_nav_cell_size(const SceneNavEnvComp*);

/**
 * Query cell information.
 */
GeoVector    scene_nav_position(const SceneNavEnvComp*, GeoNavCell);
GeoVector    scene_nav_size(const SceneNavEnvComp*, GeoNavCell);
GeoBox       scene_nav_box(const SceneNavEnvComp*, GeoNavCell);
GeoNavRegion scene_nav_region(const SceneNavEnvComp*, GeoNavCell, u16 radius);
bool         scene_nav_blocked(const SceneNavEnvComp*, GeoNavCell);
bool         scene_nav_blocked_box(const SceneNavEnvComp*, const GeoBoxRotated*);
bool         scene_nav_blocked_sphere(const SceneNavEnvComp*, const GeoSphere*);
bool         scene_nav_occupied(const SceneNavEnvComp*, GeoNavCell);
bool         scene_nav_occupied_moving(const SceneNavEnvComp*, GeoNavCell);
GeoNavCell   scene_nav_at_position(const SceneNavEnvComp*, GeoVector);
GeoNavIsland scene_nav_island(const SceneNavEnvComp*, GeoNavCell);
u32          scene_nav_closest_unblocked_n(const SceneNavEnvComp*, GeoNavCell, GeoNavCellContainer);
u32          scene_nav_closest_free_n(const SceneNavEnvComp*, GeoNavCell, GeoNavCellContainer);

bool scene_nav_reachable(const SceneNavEnvComp*, GeoNavCell from, GeoNavCell to);
bool scene_nav_reachable_blocker(const SceneNavEnvComp*, GeoNavCell, const SceneNavBlockerComp*);

/**
 * Compute a separation force from blockers and other agents.
 * NOTE: EcsEntityId can be used to ignore an existing agent (for example itself).
 */
GeoVector scene_nav_separate(
    const SceneNavEnvComp*, EcsEntityId, GeoVector position, f32 radius, bool moving);
