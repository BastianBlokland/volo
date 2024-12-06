#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_nav.h"

typedef enum eSceneNavLayer {
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

typedef enum {
  SceneNavBlockerMask_Normal = 1 << SceneNavLayer_Normal,
  SceneNavBlockerMask_Large  = 1 << SceneNavLayer_Large,
  SceneNavBlockerMask_All    = ~0,
} SceneNavBlockerMask;

ecs_comp_extern_public(SceneNavBlockerComp) {
  SceneNavBlockerFlags flags : 8;
  SceneNavBlockerMask  mask : 8;                 // NOTE: Set dirty flag when changing the mask.
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
  Allocator*    pathAlloc;
  GeoNavCell*   cells;
  u16           cellCount;
  u16           currentTargetIndex; // Index in the path we are currently moving towards.
  SceneNavLayer layer : 16;
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
void               scene_nav_add_blocker(EcsWorld*, EcsEntityId, SceneNavBlockerMask);
SceneNavAgentComp* scene_nav_add_agent(EcsWorld*, SceneNavEnvComp*, EcsEntityId, SceneNavLayer);

/**
 * Query navigation data.
 */
const u32*        scene_nav_grid_stats(const SceneNavEnvComp*, SceneNavLayer);
const GeoNavGrid* scene_nav_grid(const SceneNavEnvComp*, SceneNavLayer);
