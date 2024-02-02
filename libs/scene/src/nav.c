#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_float.h"
#include "core_math.h"
#include "core_rng.h"
#include "core_sentinel.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "scene_collision.h"
#include "scene_locomotion.h"
#include "scene_nav.h"
#include "scene_register.h"
#include "scene_terrain.h"
#include "scene_time.h"
#include "scene_transform.h"

ASSERT(sizeof(EcsEntityId) == sizeof(u64), "EntityId's have to be interpretable as 64bit integers");

static const f32 g_sceneNavFallbackSize    = 500.0f;
static const f32 g_sceneNavDensity         = 1.0f;
static const f32 g_sceneNavCellHeight      = 5.0f;
static const f32 g_sceneNavCellBlockHeight = 3.0f;

#define path_max_cells 128
#define path_max_queries_per_task 25
#define path_refresh_time_min time_seconds(3)
#define path_refresh_time_max time_seconds(5)
#define path_refresh_max_dist 0.5f
#define path_arrive_threshold 0.15f

ecs_comp_define(SceneNavEnvComp) {
  GeoNavGrid* navGrid;
  u32         terrainVersion;
};

ecs_comp_define_public(SceneNavStatsComp);

ecs_comp_define_public(SceneNavBlockerComp);
ecs_comp_define_public(SceneNavAgentComp);
ecs_comp_define_public(SceneNavPathComp);
ecs_comp_define_public(SceneNavRequestComp);

static void ecs_destruct_nav_env_comp(void* data) {
  SceneNavEnvComp* comp = data;
  geo_nav_grid_destroy(comp->navGrid);
}

static void ecs_destruct_nav_path_comp(void* data) {
  SceneNavPathComp* comp = data;
  alloc_free_array_t(g_alloc_heap, comp->cells, path_max_cells);
}

static void nav_env_grid_init(SceneNavEnvComp* env, const f32 size) {
  if (env->navGrid) {
    geo_nav_grid_destroy(env->navGrid);
  }
  env->navGrid = geo_nav_grid_create(
      g_alloc_heap, size, g_sceneNavDensity, g_sceneNavCellHeight, g_sceneNavCellBlockHeight);
}

static void nav_env_create(EcsWorld* world) {
  SceneNavEnvComp* env = ecs_world_add_t(world, ecs_world_global(world), SceneNavEnvComp);

  // TODO: Currently we always initialize the grid with the fallback size first, in theory this can
  // be avoided when we know we will load a level immediately after.
  nav_env_grid_init(env, g_sceneNavFallbackSize);

  ecs_world_add_t(world, ecs_world_global(world), SceneNavStatsComp);
}

static GeoNavBlockerId
nav_block_box_rotated(SceneNavEnvComp* env, const u64 id, const GeoBoxRotated* boxRot) {
  if (math_abs(geo_quat_dot(boxRot->rotation, geo_quat_ident)) > 1.0f - 1e-4f) {
    /**
     * Substitute rotated-boxes with a (near) identity rotation with axis-aligned boxes which are
     * much faster to insert.
     */
    return geo_nav_blocker_add_box(env->navGrid, id, &boxRot->box);
  }
  return geo_nav_blocker_add_box_rotated(env->navGrid, id, boxRot);
}

static bool nav_blocker_remove_pred(const void* ctx, const u64 userId) {
  const EcsView* blockerEntities = ctx;
  return !ecs_view_contains(blockerEntities, (EcsEntityId)userId);
}

static u32 nav_blocker_hash(
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

typedef enum {
  NavChange_Reinit          = 1 << 0,
  NavChange_BlockerRemoved  = 1 << 1,
  NavChange_BlockerAdded    = 1 << 2,
  NavChange_PathInvalidated = 1 << 3,
} NavChange;

static void
nav_refresh_terrain(SceneNavEnvComp* env, const SceneTerrainComp* terrain, NavChange* change) {
  if (env->terrainVersion == scene_terrain_version(terrain)) {
    return; // Terrain unchanged.
  }

  f32 newSize = g_sceneNavFallbackSize;
  if (scene_terrain_loaded(terrain)) {
    newSize = scene_terrain_play_size(terrain);
  }
  if (newSize != geo_nav_size(env->navGrid)) {
    *change |= NavChange_Reinit;
  }

  log_d(
      "Refreshing navigation terrain",
      log_param("version", fmt_int(scene_terrain_version(terrain))),
      log_param("size", fmt_float(newSize)),
      log_param("reinit", fmt_bool((*change & NavChange_Reinit) != 0)));

  if (*change & NavChange_Reinit) {
    nav_env_grid_init(env, newSize);
  }

  if (scene_terrain_loaded(terrain)) {
    const GeoNavRegion bounds = geo_nav_bounds(env->navGrid);
    for (u32 y = bounds.min.y; y != bounds.max.y; ++y) {
      for (u32 x = bounds.min.x; x != bounds.max.x; ++x) {
        const GeoNavCell cell          = {.x = x, .y = y};
        const GeoVector  pos           = geo_nav_position(env->navGrid, cell);
        const f32        terrainHeight = scene_terrain_height(terrain, pos);
        geo_nav_y_update(env->navGrid, cell, terrainHeight);
      }
    }
    // Conservatively indicate a blocker-update as new cells can be blocked on the updated terrain.
    *change |= NavChange_BlockerRemoved | NavChange_BlockerAdded;
  } else {
    geo_nav_y_clear(env->navGrid);
    // Conservatively indicate a blocker was removed.
    *change |= NavChange_BlockerRemoved;
  }

  env->terrainVersion = scene_terrain_version(terrain);
}

static void
nav_refresh_blockers(SceneNavEnvComp* env, EcsView* blockerEntities, NavChange* change) {

  const bool reinit = (*change & NavChange_Reinit) != 0;
  if (reinit) {
    if (geo_nav_blocker_remove_all(env->navGrid)) {
      *change |= NavChange_BlockerRemoved;
    }
  } else {
    if (geo_nav_blocker_remove_pred(env->navGrid, nav_blocker_remove_pred, blockerEntities)) {
      *change |= NavChange_BlockerRemoved;
    }
  }

  for (EcsIterator* itr = ecs_view_itr(blockerEntities); ecs_view_walk(itr);) {
    const SceneCollisionComp* collision   = ecs_view_read_t(itr, SceneCollisionComp);
    const SceneTransformComp* trans       = ecs_view_read_t(itr, SceneTransformComp);
    const SceneScaleComp*     scale       = ecs_view_read_t(itr, SceneScaleComp);
    SceneNavBlockerComp*      blockerComp = ecs_view_write_t(itr, SceneNavBlockerComp);

    const u32 newHash = nav_blocker_hash(collision, trans, scale);

    if (!reinit && blockerComp->flags & SceneNavBlockerFlags_Registered) {
      if (newHash != blockerComp->hash) {
        if (geo_nav_blocker_remove(env->navGrid, blockerComp->blockerId)) {
          *change |= NavChange_BlockerRemoved;
        }
      } else {
        continue; // Terrain not changed, not dirty and already registered; nothing to do.
      }
    }
    blockerComp->hash = newHash;
    blockerComp->flags |= SceneNavBlockerFlags_Registered;

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
      blockerComp->blockerId      = nav_block_box_rotated(env, userId, &cBounds);
    } break;
    case SceneCollisionType_Box: {
      const GeoBoxRotated b  = scene_collision_world_box(&collision->box, trans, scale);
      blockerComp->blockerId = nav_block_box_rotated(env, userId, &b);
    } break;
    case SceneCollisionType_Count:
      UNREACHABLE
    }
    if (!sentinel_check(blockerComp->blockerId)) {
      /**
       * A new blocker was registered.
       * NOTE: This doesn't necessarily mean any new cell got blocked that wasn't before so this
       * dirtying is conservative at the moment.
       */
      *change |= NavChange_BlockerAdded;
    }
  }
}

static void nav_refresh_paths(SceneNavEnvComp* env, EcsView* pathEntities, NavChange* change) {
  if (*change & NavChange_Reinit) {
    /**
     * The navigation grid was reinitialized; we cannot re-use any of the existing paths (as when
     * the size changes the cell coordinates change).
     */
    for (EcsIterator* itr = ecs_view_itr(pathEntities); ecs_view_walk(itr);) {
      SceneNavPathComp* path = ecs_view_write_t(itr, SceneNavPathComp);
      path->cellCount        = 0;
      path->nextRefreshTime  = 0;
      *change |= NavChange_PathInvalidated;
    }
  } else if (*change & NavChange_BlockerAdded) {
    /**
     * A blocker was added; we need to check if any of the existing paths now cross a blocked
     * cell, if so: mark it for refresh.
     * NOTE: We don't fully invalidate the path as that will cause the unit to stop momentarily
     * while waiting for a new path, this potentially allows a unit to walk against a blocked cell
     * but the separation will keep it out of the blocker.
     */
    for (EcsIterator* itr = ecs_view_itr(pathEntities); ecs_view_walk(itr);) {
      SceneNavPathComp* path = ecs_view_write_t(itr, SceneNavPathComp);
      for (u32 i = 0; i != path->cellCount; ++i) {
        if (geo_nav_blocked(env->navGrid, path->cells[i])) {
          path->nextRefreshTime = 0;
          *change |= NavChange_PathInvalidated;
          break;
        }
      }
    }
  }
}

static void nav_add_occupants(SceneNavEnvComp* env, EcsView* occupantEntities) {
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

static void nav_stats_update(SceneNavStatsComp* stats, GeoNavGrid* grid) {
  const u32* gridStatsPtr = geo_nav_stats(grid);

  // Copy the grid stats into the stats component.
  mem_cpy(array_mem(stats->gridStats), mem_create(gridStatsPtr, sizeof(u32) * GeoNavStat_Count));

  geo_nav_stats_reset(grid);
}

ecs_view_define(InitGlobalView) {
  ecs_access_read(SceneTerrainComp);
  ecs_access_write(SceneNavEnvComp);
}

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

ecs_view_define(PathEntityView) { ecs_access_write(SceneNavPathComp); }

ecs_system_define(SceneNavInitSys) {
  if (!ecs_world_has_t(world, ecs_world_global(world), SceneNavEnvComp)) {
    nav_env_create(world);
    return;
  }

  EcsView*     globalView = ecs_world_view_t(world, InitGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneTerrainComp* terrain = ecs_view_read_t(globalItr, SceneTerrainComp);
  SceneNavEnvComp*        env     = ecs_view_write_t(globalItr, SceneNavEnvComp);

  EcsView* blockerEntities  = ecs_world_view_t(world, BlockerEntityView);
  EcsView* pathEntities     = ecs_world_view_t(world, PathEntityView);
  EcsView* occupantEntities = ecs_world_view_t(world, OccupantEntityView);

  NavChange change = 0;
  nav_refresh_terrain(env, terrain, &change);
  nav_refresh_blockers(env, blockerEntities, &change);
  nav_refresh_paths(env, pathEntities, &change);

  if (change & (NavChange_BlockerRemoved | NavChange_BlockerAdded)) {
    geo_nav_compute_islands(env->navGrid);
  }

  geo_nav_occupant_remove_all(env->navGrid);
  nav_add_occupants(env, occupantEntities);
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

ecs_view_define(TargetEntityView) {
  ecs_access_read(SceneTransformComp);
  ecs_access_maybe_read(SceneNavBlockerComp);
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

typedef struct {
  GeoNavCell cell;
  GeoVector  position;
} SceneNavGoal;

static SceneNavGoal
nav_goal_pos(const SceneNavEnvComp* env, const GeoNavCell fromCell, const GeoVector targetPos) {
  const GeoNavCell targetCell = geo_nav_at_position(env->navGrid, targetPos);
  if (geo_nav_reachable(env->navGrid, fromCell, targetCell)) {
    return (SceneNavGoal){.cell = targetCell, .position = targetPos};
  }
  const GeoNavCell reachableCell = geo_nav_closest_reachable(env->navGrid, fromCell, targetCell);
  const GeoVector  reachablePos  = geo_nav_position(env->navGrid, reachableCell);
  return (SceneNavGoal){.cell = reachableCell, .position = reachablePos};
}

static SceneNavGoal
nav_goal_entity(const SceneNavEnvComp* env, const GeoNavCell fromCell, EcsIterator* targetItr) {
  const SceneTransformComp*  targetTrans = ecs_view_read_t(targetItr, SceneTransformComp);
  const SceneNavBlockerComp* blocker     = ecs_view_read_t(targetItr, SceneNavBlockerComp);
  if (blocker && !sentinel_check(blocker->blockerId)) {
    const GeoNavCell closest = geo_nav_blocker_closest(env->navGrid, blocker->blockerId, fromCell);
    return (SceneNavGoal){.cell = closest, .position = geo_nav_position(env->navGrid, closest)};
  }
  return nav_goal_pos(env, fromCell, targetTrans->position);
}

static void nav_move_towards(
    const SceneNavEnvComp* env,
    SceneLocomotionComp*   loco,
    const SceneNavGoal*    goal,
    const GeoNavCell       cell) {
  GeoVector locoPos;
  if (cell.data == goal->cell.data) {
    locoPos = goal->position;
  } else {
    locoPos = geo_nav_position(env->navGrid, cell);
  }
  scene_locomotion_move(loco, locoPos);
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

  EcsView*     targetView = ecs_world_view_t(world, TargetEntityView);
  EcsIterator* targetItr  = ecs_view_itr(targetView);

  for (EcsIterator* itr = ecs_view_itr_step(agentsView, parCount, parIndex); ecs_view_walk(itr);) {
    const SceneTransformComp* trans = ecs_view_read_t(itr, SceneTransformComp);
    SceneLocomotionComp*      loco  = ecs_view_write_t(itr, SceneLocomotionComp);
    SceneNavAgentComp*        agent = ecs_view_write_t(itr, SceneNavAgentComp);
    SceneNavPathComp*         path  = ecs_view_write_t(itr, SceneNavPathComp);

    if (!(agent->flags & SceneNavAgent_Traveling)) {
      agent->flags &= ~SceneNavAgent_Stop;
      goto Done;
    }

    const GeoNavCell fromCell = geo_nav_at_position(env->navGrid, trans->position);
    SceneNavGoal     goal;
    if (agent->targetEntity) {
      if (!ecs_view_maybe_jump(targetItr, agent->targetEntity)) {
        goto Stop; // Target entity not valid (anymore).
      }
      goal = nav_goal_entity(env, fromCell, targetItr);
    } else {
      goal = nav_goal_pos(env, fromCell, agent->targetPos);
    }

    const GeoVector toTarget        = geo_vector_xz(geo_vector_sub(goal.position, trans->position));
    const f32       distToTargetSqr = geo_vector_mag_sqr(toTarget);
    if (distToTargetSqr <= (path_arrive_threshold * path_arrive_threshold)) {
      goto Stop; // Arrived at destination.
    }

    if (agent->flags & SceneNavAgent_Stop) {
    Stop:
      agent->flags &= ~(SceneNavAgent_Stop | SceneNavAgent_Traveling);
      scene_locomotion_stop(loco);
      goto Done;
    }

    if (fromCell.data == goal.cell.data) {
      // In the same cell as the target; move in a straight line.
      scene_locomotion_move(loco, goal.position);
      goto Done;
    }

    /**
     * TODO: We can potentially avoid pathing if there's a straight line to the target. Care must
     * be taken however to avoid oscillating between the straight line and the path, which can
     * easily happen when moving on the border of a nav cell.
     */

    // Compute a new path.
    if (pathQueriesRemaining && path_needs_refresh(path, goal.position, time)) {
      const GeoNavCellContainer container = {.cells = path->cells, .capacity = path_max_cells};
      path->cellCount          = geo_nav_path(env->navGrid, fromCell, goal.cell, container);
      path->nextRefreshTime    = path_next_refresh_time(time);
      path->destination        = goal.position;
      path->currentTargetIndex = 1; // Path includes the start point; should be skipped.
      --pathQueriesRemaining;
    }

    if (!path->cellCount) {
      // Waiting for path to be computed.
      goto Done;
    }

    // Attempt to take a shortcut as far up the path as possible without being obstructed.
    for (u32 i = path->cellCount; --i > path->currentTargetIndex;) {
      if (!geo_nav_line_blocked(env->navGrid, fromCell, path->cells[i])) {
        path->currentTargetIndex = i;
        nav_move_towards(env, loco, &goal, path->cells[i]);
        goto Done;
      }
    }

    // No shortcut available; move to the current target cell in the path.
    nav_move_towards(env, loco, &goal, path->cells[path->currentTargetIndex]);

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

  nav_stats_update(stats, env->navGrid);
}

ecs_view_define(NavRequestsView) {
  ecs_access_write(SceneNavAgentComp);
  ecs_access_read(SceneNavRequestComp);
}

ecs_system_define(SceneNavApplyRequestsSys) {
  EcsView* reqView = ecs_world_view_t(world, NavRequestsView);
  for (EcsIterator* itr = ecs_view_itr(reqView); ecs_view_walk(itr);) {
    SceneNavAgentComp*         agent   = ecs_view_write_t(itr, SceneNavAgentComp);
    const SceneNavRequestComp* request = ecs_view_read_t(itr, SceneNavRequestComp);
    if (request->targetEntity) {
      scene_nav_travel_to_entity(agent, request->targetEntity);
    } else {
      scene_nav_travel_to(agent, request->targetPos);
    }
    ecs_world_remove_t(world, ecs_view_entity(itr), SceneNavRequestComp);
  }
}

ecs_module_init(scene_nav_module) {
  ecs_register_comp(SceneNavEnvComp, .destructor = ecs_destruct_nav_env_comp);
  ecs_register_comp(SceneNavStatsComp);
  ecs_register_comp(SceneNavBlockerComp);
  ecs_register_comp(SceneNavAgentComp);
  ecs_register_comp(SceneNavPathComp, .destructor = ecs_destruct_nav_path_comp);
  ecs_register_comp(SceneNavRequestComp);

  ecs_register_system(
      SceneNavInitSys,
      ecs_register_view(InitGlobalView),
      ecs_register_view(BlockerEntityView),
      ecs_register_view(OccupantEntityView),
      ecs_register_view(PathEntityView));

  ecs_order(SceneNavInitSys, SceneOrder_NavInit);

  ecs_register_system(
      SceneNavUpdateAgentsSys,
      ecs_register_view(UpdateAgentGlobalView),
      ecs_register_view(AgentEntityView),
      ecs_register_view(TargetEntityView));

  ecs_parallel(SceneNavUpdateAgentsSys, 4);

  ecs_register_system(SceneNavApplyRequestsSys, ecs_register_view(NavRequestsView));

  ecs_register_system(SceneNavUpdateStatsSys, ecs_register_view(UpdateStatsGlobalView));

  enum {
    SceneOrder_Normal         = 0,
    SceneOrder_NavStatsUpdate = 1,
  };
  ecs_order(SceneNavUpdateStatsSys, SceneOrder_NavStatsUpdate);
}

void scene_nav_travel_to(SceneNavAgentComp* agent, const GeoVector target) {
  agent->flags |= SceneNavAgent_Traveling;
  agent->targetEntity = 0;
  agent->targetPos    = target;
}

void scene_nav_travel_to_entity(SceneNavAgentComp* agent, const EcsEntityId target) {
  agent->flags |= SceneNavAgent_Traveling;
  agent->targetEntity = target;
}

void scene_nav_stop(SceneNavAgentComp* agent) {
  agent->flags |= SceneNavAgent_Stop;
  agent->targetEntity = 0;
  agent->targetPos    = geo_vector(f32_max, f32_max, f32_max);
}

void scene_nav_add_blocker(EcsWorld* world, const EcsEntityId entity) {
  ecs_world_add_t(world, entity, SceneNavBlockerComp);
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

bool scene_nav_blocked(const SceneNavEnvComp* env, const GeoNavCell cell) {
  return geo_nav_blocked(env->navGrid, cell);
}

bool scene_nav_blocked_box(const SceneNavEnvComp* env, const GeoBoxRotated* boxRotated) {
  return geo_nav_blocked_box_rotated(env->navGrid, boxRotated);
}

bool scene_nav_blocked_sphere(const SceneNavEnvComp* env, const GeoSphere* sphere) {
  return geo_nav_blocked_sphere(env->navGrid, sphere);
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

u32 scene_nav_closest_unblocked_n(
    const SceneNavEnvComp* env, const GeoNavCell cell, const GeoNavCellContainer out) {
  return geo_nav_closest_unblocked_n(env->navGrid, cell, out);
}

u32 scene_nav_closest_free_n(
    const SceneNavEnvComp* env, const GeoNavCell cell, const GeoNavCellContainer out) {
  return geo_nav_closest_free_n(env->navGrid, cell, out);
}

bool scene_nav_reachable(const SceneNavEnvComp* env, const GeoNavCell from, const GeoNavCell to) {
  return geo_nav_reachable(env->navGrid, from, to);
}

bool scene_nav_reachable_blocker(
    const SceneNavEnvComp* env, const GeoNavCell from, const SceneNavBlockerComp* blocker) {
  if (!(blocker->flags & SceneNavBlockerFlags_Registered)) {
    return false;
  }
  return geo_nav_blocker_reachable(env->navGrid, blocker->blockerId, from);
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
