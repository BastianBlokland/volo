#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_nav.h"

// Forward declare from 'core_time.h'.
typedef i64 TimeDuration;

typedef enum {
  SceneNavLayer_Normal,
  SceneNavLayer_Large,

  SceneNavLayer_Count,
} SceneNavLayer;

extern const String g_sceneNavLayerNames[SceneNavLayer_Count];

/**
 * Global navigation environment.
 */
ecs_comp_extern(SceneNavEnvComp);

/**
 * Navigation blocker.
 */

typedef enum {
  SceneNavBlockerFlags_Dirty = 1 << 0,
} SceneNavBlockerFlags;

ecs_comp_extern_public(SceneNavBlockerComp) {
  SceneNavBlockerFlags flags;
  u32                  hash;                     // For dirty detection; automatically generated.
  GeoNavBlockerId      ids[SceneNavLayer_Count]; // Registered blocker ids; automatically generated.
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
  SceneNavLayer      layer;
  EcsEntityId        targetEntity;
  GeoVector          targetPos;
};

ecs_comp_extern_public(SceneNavPathComp) {
  GeoNavCell*   cells;
  u32           cellCount;
  SceneNavLayer layer;
  u32           currentTargetIndex; // Index in the path we are currently moving towards.
  TimeDuration  nextRefreshTime;
  GeoVector     destination;
};

ecs_comp_extern_public(SceneNavRequestComp) {
  EcsEntityId targetEntity;
  GeoVector   targetPos;
};

void scene_nav_travel_to(SceneNavAgentComp*, GeoVector target);
void scene_nav_travel_to_entity(SceneNavAgentComp*, EcsEntityId target);
void scene_nav_stop(SceneNavAgentComp*);

/**
 * Initialize navigation agents and blockers.
 */
void               scene_nav_add_blocker(EcsWorld*, EcsEntityId);
SceneNavAgentComp* scene_nav_add_agent(EcsWorld*, EcsEntityId, SceneNavLayer);

/**
 * Retrieve navigation layer data.
 */
const u32*        scene_nav_grid_stats(const SceneNavEnvComp*, SceneNavLayer);
const GeoNavGrid* scene_nav_grid(const SceneNavEnvComp*, SceneNavLayer);

/**
 * Query cell information.
 */
u32 scene_nav_closest_unblocked_n(const SceneNavEnvComp*, GeoNavCell, GeoNavCellContainer);
u32 scene_nav_closest_free_n(const SceneNavEnvComp*, GeoNavCell, GeoNavCellContainer);

bool scene_nav_reachable_blocker(const SceneNavEnvComp*, GeoNavCell, const SceneNavBlockerComp*);

/**
 * Compute a separation force from blockers and other agents.
 * NOTE: EcsEntityId can be used to ignore an existing agent (for example itself).
 */
GeoVector scene_nav_separate(
    const SceneNavEnvComp*, EcsEntityId, GeoVector position, f32 radius, bool moving);
