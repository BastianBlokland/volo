#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_math.h"
#include "core_rng.h"
#include "core_sentinel.h"
#include "ecs_world.h"
#include "scene_collision.h"
#include "scene_locomotion.h"
#include "scene_nav.h"
#include "scene_register.h"
#include "scene_time.h"
#include "scene_transform.h"

ASSERT(sizeof(EcsEntityId) == sizeof(u64), "EntityId's have to be interpretable as 64bit integers");

static const GeoVector g_sceneNavCenter  = {0, 0, 0};
static const f32       g_sceneNavSize    = 200.0f;
static const f32       g_sceneNavDensity = 1.25f;
static const f32       g_sceneNavHeight  = 2.0f;

#define path_max_cells 64
#define path_max_queries_per_task 25
#define path_refresh_time_min time_seconds(3)
#define path_refresh_time_max time_seconds(5)
#define path_refresh_max_dist 2.0f
#define nav_arrive_threshold_occupied_cell_mult 1.25f
#define nav_arrive_threshold_min 0.1f

ecs_comp_define(SceneNavEnvComp) { GeoNavGrid* navGrid; };
ecs_comp_define_public(SceneNavStatsComp);

ecs_comp_define_public(SceneNavBlockerComp);
ecs_comp_define_public(SceneNavAgentComp);
ecs_comp_define_public(SceneNavPathComp);

static void ecs_destruct_nav_env_comp(void* data) {
  SceneNavEnvComp* comp = data;
  geo_nav_grid_destroy(comp->navGrid);
}

static void ecs_destruct_nav_path_comp(void* data) {
  SceneNavPathComp* comp = data;
  alloc_free_array_t(g_alloc_heap, comp->cells, path_max_cells);
}

static void scene_nav_env_create(EcsWorld* world) {
  GeoNavGrid* grid = geo_nav_grid_create(
      g_alloc_heap, g_sceneNavCenter, g_sceneNavSize, g_sceneNavDensity, g_sceneNavHeight);

  ecs_world_add_t(world, ecs_world_global(world), SceneNavEnvComp, .navGrid = grid);
  ecs_world_add_t(world, ecs_world_global(world), SceneNavStatsComp);
}

static GeoNavBlockerId
scene_nav_block_box_rotated(SceneNavEnvComp* env, const u64 id, const GeoBoxRotated* boxRot) {
  if (math_abs(geo_quat_dot(boxRot->rotation, geo_quat_ident)) > 1.0f - 1e-4f) {
    /**
     * Substitute rotated-boxes with a (near) identity rotation with axis-aligned boxes which are
     * much faster to insert.
     */
    return geo_nav_blocker_add_box(env->navGrid, id, &boxRot->box);
  }
  return geo_nav_blocker_add_box_rotated(env->navGrid, id, boxRot);
}

static bool scene_nav_blocker_remove_pred(const void* ctx, const u64 userId) {
  const EcsView* blockerEntities = ctx;
  return !ecs_view_contains(blockerEntities, (EcsEntityId)userId);
}

static u32 scene_nav_blocker_hash(
    const SceneCollisionComp* collision,
    const SceneTransformComp* trans,
    const SceneScaleComp*     scale) {
  u32 hash = bits_hash_32(mem_create(collision, sizeof(SceneCollisionComp)));
  if (trans) {
    const u32 transHash = bits_hash_32(mem_create(trans, sizeof(SceneTransformComp)));
    hash                = bits_hash_32_combine(hash, transHash);
  }
  if (scale) {
    const u32 scaleHash = bits_hash_32(mem_create(scale, sizeof(SceneScaleComp)));
    hash                = bits_hash_32_combine(hash, scaleHash);
  }
  return hash;
}

static bool scene_nav_refresh_blockers(SceneNavEnvComp* env, EcsView* blockerEntities) {
  bool blockersChanged = false;
  if (geo_nav_blocker_remove_pred(env->navGrid, scene_nav_blocker_remove_pred, blockerEntities)) {
    blockersChanged = true;
  }

  for (EcsIterator* itr = ecs_view_itr(blockerEntities); ecs_view_walk(itr);) {
    const SceneCollisionComp* collision   = ecs_view_read_t(itr, SceneCollisionComp);
    const SceneTransformComp* trans       = ecs_view_read_t(itr, SceneTransformComp);
    const SceneScaleComp*     scale       = ecs_view_read_t(itr, SceneScaleComp);
    SceneNavBlockerComp*      blockerComp = ecs_view_write_t(itr, SceneNavBlockerComp);

    const u32  newHash = scene_nav_blocker_hash(collision, trans, scale);
    const bool dirty   = newHash != blockerComp->hash;

    if (blockerComp->flags & SceneNavBlockerFlags_RegisteredBlocker) {
      if (dirty) {
        blockersChanged |= geo_nav_blocker_remove(env->navGrid, blockerComp->blockerId);
      } else {
        continue; // Not dirty and already registered; nothing to do.
      }
    }
    blockerComp->hash = newHash;
    blockerComp->flags |= SceneNavBlockerFlags_RegisteredBlocker;

    const u64 userId = (u64)ecs_view_entity(itr);
    switch (collision->type) {
    case SceneCollisionType_Sphere: {
      const GeoSphere s      = scene_collision_world_sphere(&collision->sphere, trans, scale);
      blockerComp->blockerId = geo_nav_blocker_add_sphere(env->navGrid, userId, &s);
    } break;
    case SceneCollisionType_Capsule: {
      /**
       * NOTE: Uses the capsule bounds at the moment, if more accurate capsule blockers are
       * needed then capsule support should be added to GeoNavGrid.
       */
      const GeoCapsule    c = scene_collision_world_capsule(&collision->capsule, trans, scale);
      const GeoBoxRotated cBounds = geo_box_rotated_from_capsule(c.line.a, c.line.b, c.radius);
      blockerComp->blockerId      = scene_nav_block_box_rotated(env, userId, &cBounds);
    } break;
    case SceneCollisionType_Box: {
      const GeoBoxRotated b  = scene_collision_world_box(&collision->box, trans, scale);
      blockerComp->blockerId = scene_nav_block_box_rotated(env, userId, &b);
    } break;
    case SceneCollisionType_Count:
      UNREACHABLE
    }
    if (!sentinel_check(blockerComp->blockerId)) {
      /**
       * A new blocker was registered.
       * NOTE: This doesn't necessarily mean any new cell get blocked that wasn't before so this
       * dirtying is a conservative at the moment.
       */
      blockersChanged = true;
    }
  }
  return blockersChanged;
}

static void scene_nav_add_occupants(SceneNavEnvComp* env, EcsView* occupantEntities) {
  for (EcsIterator* itr = ecs_view_itr(occupantEntities); ecs_view_walk(itr);) {
    const SceneTransformComp*  trans = ecs_view_read_t(itr, SceneTransformComp);
    const SceneScaleComp*      scale = ecs_view_read_t(itr, SceneScaleComp);
    const SceneLocomotionComp* loco  = ecs_view_read_t(itr, SceneLocomotionComp);

    const f32 radius = loco->radius * (scale ? scale->scale : 1.0f);

    const u64           occupantId    = (u64)ecs_view_entity(itr);
    GeoNavOccupantFlags occupantFlags = 0;
    if (loco->flags & SceneLocomotion_Moving) {
      occupantFlags |= GeoNavOccupantFlags_Moving;
    }
    geo_nav_occupant_add(env->navGrid, occupantId, trans->position, radius, occupantFlags);
  }
}

static void scene_nav_stats_update(SceneNavStatsComp* stats, GeoNavGrid* grid) {
  const u32* gridStatsPtr = geo_nav_stats(grid);

  // Copy the grid stats into the stats component.
  mem_cpy(array_mem(stats->gridStats), mem_create(gridStatsPtr, sizeof(u32) * GeoNavStat_Count));

  geo_nav_stats_reset(grid);
}

ecs_view_define(InitGlobalView) { ecs_access_write(SceneNavEnvComp); }

ecs_view_define(BlockerEntityView) {
  ecs_access_maybe_read(SceneScaleComp);
  ecs_access_maybe_read(SceneTransformComp);
  ecs_access_read(SceneCollisionComp);
  ecs_access_write(SceneNavBlockerComp);
}

ecs_view_define(OccupantEntityView) {
  ecs_access_maybe_read(SceneScaleComp);
  ecs_access_read(SceneLocomotionComp);
  ecs_access_read(SceneTransformComp);
  ecs_access_with(SceneNavAgentComp);
}

ecs_system_define(SceneNavInitSys) {
  if (!ecs_world_has_t(world, ecs_world_global(world), SceneNavEnvComp)) {
    scene_nav_env_create(world);
    return;
  }

  EcsView*     globalView = ecs_world_view_t(world, InitGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  SceneNavEnvComp* env = ecs_view_write_t(globalItr, SceneNavEnvComp);

  EcsView* blockerEntities = ecs_world_view_t(world, BlockerEntityView);
  if (scene_nav_refresh_blockers(env, blockerEntities)) {
    geo_nav_compute_islands(env->navGrid);
  }

  geo_nav_occupant_remove_all(env->navGrid);
  EcsView* occupantEntities = ecs_world_view_t(world, OccupantEntityView);
  scene_nav_add_occupants(env, occupantEntities);
}

ecs_view_define(UpdateAgentGlobalView) {
  ecs_access_read(SceneNavEnvComp);
  ecs_access_read(SceneTimeComp);
}

ecs_view_define(AgentEntityView) {
  ecs_access_read(SceneTransformComp);
  ecs_access_write(SceneLocomotionComp);
  ecs_access_write(SceneNavAgentComp);
  ecs_access_write(SceneNavPathComp);
}

static bool path_needs_refresh(
    const SceneNavPathComp* path, const GeoVector targetPos, const SceneTimeComp* time) {
  if (time->time >= path->nextRefreshTime) {
    return true; // Enough time has elapsed.
  }
  const f32 distToDestSqr = geo_vector_mag_sqr(geo_vector_sub(path->destination, targetPos));
  if (distToDestSqr > (path_refresh_max_dist * path_refresh_max_dist)) {
    return true; // New destination is too far from the old destination.
  }
  return false;
}

static TimeDuration path_next_refresh_time(const SceneTimeComp* time) {
  TimeDuration next = time->time;
  next += (TimeDuration)rng_sample_range(g_rng, path_refresh_time_min, path_refresh_time_max);
  return next;
}

/**
 * Compute a rough estimate on how close we can get to our target based on the blocked and occupied
 * cells around the target.
 */
static f32 scene_nav_arrive_threshold(const SceneNavEnvComp* env, const GeoNavCell toCell) {
  const GeoNavCell closestFree = geo_nav_closest_free(env->navGrid, toCell);
  const f32 occupiedDist = math_max(geo_nav_distance(env->navGrid, toCell, closestFree) - 0.5f, 0);
  return occupiedDist * nav_arrive_threshold_occupied_cell_mult + nav_arrive_threshold_min;
}

ecs_system_define(SceneNavUpdateAgentsSys) {
  EcsView*     globalView = ecs_world_view_t(world, UpdateAgentGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneNavEnvComp* env  = ecs_view_read_t(globalItr, SceneNavEnvComp);
  const SceneTimeComp*   time = ecs_view_read_t(globalItr, SceneTimeComp);

  // Limit the amount of path queries per-frame.
  u32      pathQueriesRemaining = path_max_queries_per_task;
  EcsView* agentsView           = ecs_world_view_t(world, AgentEntityView);
  for (EcsIterator* itr = ecs_view_itr_step(agentsView, parCount, parIndex); ecs_view_walk(itr);) {
    const SceneTransformComp* trans = ecs_view_read_t(itr, SceneTransformComp);
    SceneLocomotionComp*      loco  = ecs_view_write_t(itr, SceneLocomotionComp);
    SceneNavAgentComp*        agent = ecs_view_write_t(itr, SceneNavAgentComp);
    SceneNavPathComp*         path  = ecs_view_write_t(itr, SceneNavPathComp);

    if (!(agent->flags & SceneNavAgent_Traveling)) {
      agent->flags &= ~SceneNavAgent_Stop;
      goto Done;
    }

    const GeoNavCell fromCell  = geo_nav_at_position(env->navGrid, trans->position);
    GeoVector        toPos     = agent->target;
    GeoNavCell       toCell    = geo_nav_at_position(env->navGrid, toPos);
    const bool       toBlocked = geo_nav_blocked(env->navGrid, toCell);
    if (toBlocked) {
      // Target is not reachable; pick the closest reachable point.
      toCell = geo_nav_closest_reachable(env->navGrid, fromCell, toCell);
      toPos  = geo_nav_position(env->navGrid, toCell);
    }

    const f32 distToTarget    = geo_vector_mag(geo_vector_sub(toPos, trans->position));
    const f32 arriveThreshold = scene_nav_arrive_threshold(env, toCell);
    if (distToTarget <= (loco->radius + arriveThreshold)) {
      agent->flags |= SceneNavAgent_Stop; // Arrived at destination.
    }

    if (agent->flags & SceneNavAgent_Stop) {
      agent->flags &= ~(SceneNavAgent_Stop | SceneNavAgent_Traveling);
      scene_locomotion_stop(loco);
      goto Done;
    }

    if (!geo_nav_line_blocked(env->navGrid, fromCell, toCell)) {
      // No obstacles between us and the target; move straight to the target
      scene_locomotion_move(loco, toPos);
      goto Done;
    }

    // Compute a new path.
    if (pathQueriesRemaining && path_needs_refresh(path, toPos, time)) {
      const GeoNavPathStorage storage = {.cells = path->cells, .capacity = path_max_cells};
      path->cellCount                 = geo_nav_path(env->navGrid, fromCell, toCell, storage);
      path->nextRefreshTime           = path_next_refresh_time(time);
      path->destination               = toPos;
      --pathQueriesRemaining;

      // Stop if no path is possible at this time.
      if (!path->cellCount) {
        goto Done;
      }
    }

    // Attempt to take a shortcut as far up the path as possible without being obstructed.
    for (u32 i = (path->cellCount); i-- > 1;) {
      if (!geo_nav_line_blocked(env->navGrid, fromCell, path->cells[i])) {
        scene_locomotion_move(loco, geo_nav_position(env->navGrid, path->cells[i]));
        goto Done;
      }
    }

    // No shortcut available; move to the next cell in the path.
    if (path->cellCount > 1) {
      scene_locomotion_move(loco, geo_nav_position(env->navGrid, path->cells[1]));
      goto Done;
    }

    // Waiting for path to be computed.

  Done:
    continue;
  }
}

ecs_view_define(UpdateStatsGlobalView) {
  ecs_access_write(SceneNavEnvComp);
  ecs_access_write(SceneNavStatsComp);
}

ecs_system_define(SceneNavUpdateStatsSys) {
  EcsView*     globalView = ecs_world_view_t(world, UpdateStatsGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  SceneNavEnvComp*   env   = ecs_view_write_t(globalItr, SceneNavEnvComp);
  SceneNavStatsComp* stats = ecs_view_write_t(globalItr, SceneNavStatsComp);

  scene_nav_stats_update(stats, env->navGrid);
}

ecs_module_init(scene_nav_module) {
  ecs_register_comp(SceneNavEnvComp, .destructor = ecs_destruct_nav_env_comp);
  ecs_register_comp(SceneNavStatsComp);
  ecs_register_comp(SceneNavBlockerComp);
  ecs_register_comp(SceneNavAgentComp);
  ecs_register_comp(SceneNavPathComp, .destructor = ecs_destruct_nav_path_comp);

  ecs_register_system(
      SceneNavInitSys,
      ecs_register_view(InitGlobalView),
      ecs_register_view(BlockerEntityView),
      ecs_register_view(OccupantEntityView));

  ecs_order(SceneNavInitSys, SceneOrder_NavInit);

  ecs_register_system(
      SceneNavUpdateAgentsSys,
      ecs_register_view(UpdateAgentGlobalView),
      ecs_register_view(AgentEntityView));

  ecs_parallel(SceneNavUpdateAgentsSys, 4);

  ecs_register_system(SceneNavUpdateStatsSys, ecs_register_view(UpdateStatsGlobalView));

  enum {
    SceneOrder_Normal         = 0,
    SceneOrder_NavStatsUpdate = 1,
  };
  ecs_order(SceneNavUpdateStatsSys, SceneOrder_NavStatsUpdate);
}

void scene_nav_move_to(SceneNavAgentComp* agent, const GeoVector target) {
  agent->flags |= SceneNavAgent_Traveling;
  agent->target = target;
}

void scene_nav_stop(SceneNavAgentComp* agent) { agent->flags |= SceneNavAgent_Stop; }

void scene_nav_add_blocker(EcsWorld* world, const EcsEntityId entity) {
  ecs_world_add_empty_t(world, entity, SceneNavBlockerComp);
}

SceneNavAgentComp* scene_nav_add_agent(EcsWorld* world, const EcsEntityId entity) {
  ecs_world_add_t(
      world,
      entity,
      SceneNavPathComp,
      .cells = alloc_array_t(g_alloc_heap, GeoNavCell, path_max_cells));
  return ecs_world_add_t(world, entity, SceneNavAgentComp);
}

GeoNavRegion scene_nav_bounds(const SceneNavEnvComp* env) { return geo_nav_bounds(env->navGrid); }

GeoVector scene_nav_cell_size(const SceneNavEnvComp* env) {
  return geo_nav_cell_size(env->navGrid);
}

GeoVector scene_nav_position(const SceneNavEnvComp* env, const GeoNavCell cell) {
  return geo_nav_position(env->navGrid, cell);
}

GeoBox scene_nav_box(const SceneNavEnvComp* env, const GeoNavCell cell) {
  return geo_nav_box(env->navGrid, cell);
}

GeoNavRegion scene_nav_region(const SceneNavEnvComp* env, const GeoNavCell cell, const u16 radius) {
  return geo_nav_region(env->navGrid, cell, radius);
}

bool scene_nav_blocked(const SceneNavEnvComp* env, const GeoNavCell cell) {
  return geo_nav_blocked(env->navGrid, cell);
}

bool scene_nav_reachable(const SceneNavEnvComp* env, const GeoNavCell from, const GeoNavCell to) {
  /**
   * NOTE: If the given 'to' cell is not reachable we use the closest unblocked cell.
   * TODO: Should this be handled at this level?
   */
  return geo_nav_reachable(env->navGrid, from, geo_nav_closest_unblocked(env->navGrid, to));
}

bool scene_nav_occupied(const SceneNavEnvComp* env, const GeoNavCell cell) {
  return geo_nav_occupied(env->navGrid, cell);
}

bool scene_nav_occupied_moving(const SceneNavEnvComp* env, const GeoNavCell cell) {
  return geo_nav_occupied_moving(env->navGrid, cell);
}

GeoNavCell scene_nav_at_position(const SceneNavEnvComp* env, const GeoVector pos) {
  return geo_nav_at_position(env->navGrid, pos);
}

GeoNavIsland scene_nav_island(const SceneNavEnvComp* env, const GeoNavCell cell) {
  return geo_nav_island(env->navGrid, cell);
}

GeoVector scene_nav_separate(
    const SceneNavEnvComp* env,
    const EcsEntityId      entity,
    const GeoVector        position,
    const f32              radius,
    const bool             moving) {
  GeoNavOccupantFlags flags = 0;
  if (moving) {
    flags |= GeoNavOccupantFlags_Moving;
  }
  const u64 occupantId = (u64)entity;
  return geo_nav_separate(env->navGrid, occupantId, position, radius, flags);
}
