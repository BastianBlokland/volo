#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_float.h"
#include "core_math.h"
#include "core_rng.h"
#include "core_sentinel.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "geo_capsule.h"
#include "geo_nav.h"
#include "log_logger.h"
#include "scene_collision.h"
#include "scene_locomotion.h"
#include "scene_nav.h"
#include "scene_register.h"
#include "scene_terrain.h"
#include "scene_time.h"
#include "scene_transform.h"
#include "trace_tracer.h"

ASSERT(sizeof(EcsEntityId) == sizeof(u64), "EntityId's have to be interpretable as 64bit integers");

static const f32 g_sceneNavFallbackSize                  = 500.0f;
static const f32 g_sceneNavCellSize[SceneNavLayer_Count] = {
    [SceneNavLayer_Normal] = 1.0f,
    [SceneNavLayer_Large]  = 3.0f,
};
static const f32 g_sceneNavCellHeight[SceneNavLayer_Count] = {
    [SceneNavLayer_Normal] = 5.0f,
    [SceneNavLayer_Large]  = 5.0f,
};
static const f32 g_sceneNavCellBlockHeight = 3.0f;

#define path_max_cells 48
#define path_max_queries_per_task 25
#define path_refresh_time_min time_seconds(3)
#define path_refresh_time_max time_seconds(5)
#define path_refresh_max_dist 0.5f
#define path_arrive_threshold 0.15f
#define path_avoid_occupied_cell_dist 4

const String g_sceneNavLayerNames[] = {
    [SceneNavLayer_Normal] = string_static("Normal"),
    [SceneNavLayer_Large]  = string_static("Large"),
};
ASSERT(array_elems(g_sceneNavLayerNames) == SceneNavLayer_Count, "Incorrect number of names");

ecs_comp_define(SceneNavEnvComp) {
  GeoNavGrid* grids[SceneNavLayer_Count];
  u32         gridStats[SceneNavLayer_Count][GeoNavStat_Count];
  Allocator*  pathAlloc; // Block-allocator with size: sizeof(GeoNavCell) * path_max_cells
  u32         terrainVersion;
};

ecs_comp_define_public(SceneNavBlockerComp);
ecs_comp_define_public(SceneNavAgentComp);
ecs_comp_define_public(SceneNavPathComp);
ecs_comp_define_public(SceneNavRequestComp);

static void ecs_destruct_nav_env_comp(void* data) {
  SceneNavEnvComp* comp = data;
  for (SceneNavLayer layer = 0; layer != SceneNavLayer_Count; ++layer) {
    geo_nav_grid_destroy(comp->grids[layer]);
  }
  alloc_block_destroy(comp->pathAlloc);
}

static void ecs_destruct_nav_path_comp(void* data) {
  SceneNavPathComp* comp = data;
  alloc_free(comp->pathAlloc, mem_create(comp->cells, sizeof(GeoNavCell) * path_max_cells));
}

static GeoNavCell* nav_path_alloc(SceneNavEnvComp* navEnv) {
  const usize size = sizeof(GeoNavCell) * path_max_cells;
  return alloc_alloc(navEnv->pathAlloc, size, alignof(GeoNavCell)).ptr;
}

static GeoNavGrid* nav_grid_create(const f32 size, const SceneNavLayer layer) {
  const f32 cellSize    = g_sceneNavCellSize[layer];
  const f32 cellHeight  = g_sceneNavCellHeight[layer];
  const f32 blockHeight = g_sceneNavCellBlockHeight;
  return geo_nav_grid_create(g_allocHeap, size, cellSize, cellHeight, blockHeight);
}

static void nav_env_create(EcsWorld* world) {
  SceneNavEnvComp* env = ecs_world_add_t(world, ecs_world_global(world), SceneNavEnvComp);

  // TODO: Currently we always initialize the grid with the fallback size first, in theory this can
  // be avoided when we know we will load a level immediately after.
  for (SceneNavLayer layer = 0; layer != SceneNavLayer_Count; ++layer) {
    env->grids[layer] = nav_grid_create(g_sceneNavFallbackSize, layer);
  }

  const usize pathSize = sizeof(GeoNavCell) * path_max_cells;
  env->pathAlloc       = alloc_block_create(g_allocHeap, pathSize, alignof(GeoNavCell));
}

static GeoNavBlockerId
nav_block_box_rotated(GeoNavGrid* grid, const u64 id, const GeoBoxRotated* boxRot) {
  GeoBlockerShape shape;
  if (math_abs(geo_quat_dot(boxRot->rotation, geo_quat_ident)) > 1.0f - 1e-4f) {
    /**
     * Substitute rotated-boxes with a (near) identity rotation with axis-aligned boxes which are
     * much faster to insert.
     */
    shape.type = GeoBlockerType_Box;
    shape.box  = &boxRot->box;
  } else {
    shape.type       = GeoBlockerType_BoxRotated;
    shape.boxRotated = boxRot;
  }
  return geo_nav_blocker_add(grid, id, shape);
}

static bool nav_blocker_remove_pred(const void* ctx, const u64 userId) {
  const EcsView* blockerView = ctx;
  return !ecs_view_contains(blockerView, (EcsEntityId)userId);
}

typedef enum {
  NavChange_Reinit          = 1 << 0,
  NavChange_BlockerRemoved  = 1 << 1,
  NavChange_BlockerAdded    = 1 << 2,
  NavChange_PathInvalidated = 1 << 3,

  NavChange_IslandRefresh = NavChange_BlockerRemoved | NavChange_BlockerAdded,
} NavChange;

typedef struct {
  GeoNavGrid*             grid;
  const SceneTerrainComp* terrain;
  u32                     terrainVersion;
  SceneNavLayer           layer : 16;
  NavChange               change : 16;
} NavInitContext;

static void nav_refresh_terrain(NavInitContext* ctx) {
  if (ctx->terrainVersion == scene_terrain_version(ctx->terrain)) {
    return; // Terrain unchanged.
  }

  f32 newSize = g_sceneNavFallbackSize;
  if (scene_terrain_loaded(ctx->terrain)) {
    newSize = scene_terrain_play_size(ctx->terrain);
  }
  const bool reinit = newSize != geo_nav_size(ctx->grid);

  log_d(
      "Refreshing navigation terrain",
      log_param("version", fmt_int(scene_terrain_version(ctx->terrain))),
      log_param("layer", fmt_text(g_sceneNavLayerNames[ctx->layer])),
      log_param("size", fmt_float(newSize)),
      log_param("reinit", fmt_bool(reinit)));

  if (reinit) {
    geo_nav_grid_destroy(ctx->grid);
    ctx->grid = nav_grid_create(newSize, ctx->layer);
    ctx->change |= NavChange_Reinit;
  }

  if (scene_terrain_loaded(ctx->terrain)) {
    const GeoNavRegion bounds = geo_nav_bounds(ctx->grid);
    for (u32 y = bounds.min.y; y != bounds.max.y; ++y) {
      for (u32 x = bounds.min.x; x != bounds.max.x; ++x) {
        const GeoNavCell cell          = {.x = x, .y = y};
        const GeoVector  pos           = geo_nav_position(ctx->grid, cell);
        const f32        terrainHeight = scene_terrain_height(ctx->terrain, pos);
        geo_nav_y_update(ctx->grid, cell, terrainHeight);
      }
    }
    // Conservatively indicate a blocker-update as new cells can be blocked on the updated terrain.
    ctx->change |= NavChange_BlockerRemoved | NavChange_BlockerAdded;
  } else {
    geo_nav_y_clear(ctx->grid);
    // Conservatively indicate a blocker was removed.
    ctx->change |= NavChange_BlockerRemoved;
  }
}

static void nav_refresh_blockers(NavInitContext* ctx, EcsView* blockerView) {
  const bool reinit = (ctx->change & NavChange_Reinit) != 0;
  if (reinit) {
    if (geo_nav_blocker_remove_all(ctx->grid)) {
      ctx->change |= NavChange_BlockerRemoved;
    }
  } else {
    if (geo_nav_blocker_remove_pred(ctx->grid, nav_blocker_remove_pred, blockerView)) {
      ctx->change |= NavChange_BlockerRemoved;
    }
  }

  for (EcsIterator* itr = ecs_view_itr(blockerView); ecs_view_walk(itr);) {
    const SceneCollisionComp* collision = ecs_view_read_t(itr, SceneCollisionComp);
    const SceneTransformComp* trans     = ecs_view_read_t(itr, SceneTransformComp);
    const SceneScaleComp*     scale     = ecs_view_read_t(itr, SceneScaleComp);
    SceneNavBlockerComp*      blocker   = ecs_view_write_t(itr, SceneNavBlockerComp);

    if (!reinit && !(blocker->flags & SceneNavBlockerFlags_Dirty)) {
      continue; // Blocker not dirty; nothing do to.
    }

    if (!reinit && geo_nav_blocker_remove(ctx->grid, blocker->ids[ctx->layer])) {
      ctx->change |= NavChange_BlockerRemoved;
    }

    if (!(blocker->mask & (1 << ctx->layer))) {
      blocker->ids[ctx->layer] = geo_blocker_invalid;
      continue; // Blocker is not enabled on this layer.
    }

    const u64 userId = (u64)ecs_view_entity(itr);
    // TODO: Support multiple shapes.
    const SceneCollisionShape shape =
        scene_collision_shape_world(&collision->shapes[0], trans, scale);
    switch (shape.type) {
    case SceneCollisionType_Sphere: {
      const GeoBlockerShape blockerShape = {.type = GeoBlockerType_Sphere, .sphere = &shape.sphere};
      blocker->ids[ctx->layer]           = geo_nav_blocker_add(ctx->grid, userId, blockerShape);
      break;
    }
    case SceneCollisionType_Capsule: {
      /**
       * NOTE: Uses the capsule bounds at the moment, if more accurate capsule blockers are
       * needed then capsule support should be added to GeoNavGrid.
       */
      const GeoCapsule*   c       = &shape.capsule;
      const GeoBoxRotated cBounds = geo_box_rotated_from_capsule(c->line.a, c->line.b, c->radius);
      blocker->ids[ctx->layer]    = nav_block_box_rotated(ctx->grid, userId, &cBounds);
    } break;
    case SceneCollisionType_Box:
      blocker->ids[ctx->layer] = nav_block_box_rotated(ctx->grid, userId, &shape.box);
      break;
    case SceneCollisionType_Count:
      UNREACHABLE
    }
    if (!sentinel_check(blocker->ids[ctx->layer])) {
      /**
       * A new blocker was registered.
       * NOTE: This doesn't necessarily mean any new cell got blocked that wasn't before so this
       * dirtying is conservative at the moment.
       */
      ctx->change |= NavChange_BlockerAdded;
    }
  }
}

static void nav_refresh_paths(NavInitContext* ctx, EcsView* pathView) {
  if (ctx->change & NavChange_Reinit) {
    /**
     * The navigation grid was reinitialized; we cannot re-use any of the existing paths (as when
     * the size changes the cell coordinates change).
     */
    for (EcsIterator* itr = ecs_view_itr(pathView); ecs_view_walk(itr);) {
      SceneNavPathComp* path = ecs_view_write_t(itr, SceneNavPathComp);
      if (path->layer != ctx->layer) {
        continue;
      }
      path->cellCount       = 0;
      path->nextRefreshTime = 0;
      ctx->change |= NavChange_PathInvalidated;
    }
  } else if (ctx->change & NavChange_BlockerAdded) {
    /**
     * A blocker was added; we need to check if any of the existing paths now cross a blocked
     * cell, if so: mark it for refresh.
     * NOTE: We don't fully invalidate the path as that will cause the unit to stop momentarily
     * while waiting for a new path, this potentially allows a unit to walk against a blocked cell
     * but the separation will keep it out of the blocker.
     */
    for (EcsIterator* itr = ecs_view_itr(pathView); ecs_view_walk(itr);) {
      SceneNavPathComp* path = ecs_view_write_t(itr, SceneNavPathComp);
      if (path->layer != ctx->layer) {
        continue;
      }
      for (u16 i = 0; i != path->cellCount; ++i) {
        if (geo_nav_check(ctx->grid, path->cells[i], GeoNavCond_Blocked)) {
          path->nextRefreshTime = 0;
          ctx->change |= NavChange_PathInvalidated;
          break;
        }
      }
    }
  }
}

static void nav_refresh_occupants(NavInitContext* ctx, EcsView* occupantView) {
  geo_nav_occupant_remove_all(ctx->grid);
  for (EcsIterator* itr = ecs_view_itr(occupantView); ecs_view_walk(itr);) {
    const SceneTransformComp*  trans    = ecs_view_read_t(itr, SceneTransformComp);
    const SceneScaleComp*      scale    = ecs_view_read_t(itr, SceneScaleComp);
    const SceneLocomotionComp* loco     = ecs_view_read_t(itr, SceneLocomotionComp);
    const SceneNavAgentComp*   navAgent = ecs_view_read_t(itr, SceneNavAgentComp);

    const SceneNavLayer layer = navAgent ? navAgent->layer : SceneNavLayer_Normal;
    if (layer != ctx->layer) {
      continue;
    }
    const u64 id     = (u64)ecs_view_entity(itr);
    const f32 radius = scene_locomotion_radius(loco, scale ? scale->scale : 1.0f);
    const f32 weight = scene_locomotion_weight(loco, scale ? scale->scale : 1.0f);

    GeoNavOccupantFlags occupantFlags = 0;
    if (loco->flags & SceneLocomotion_Moving) {
      occupantFlags |= GeoNavOccupantFlags_Moving;
    }
    geo_nav_occupant_add(ctx->grid, id, trans->position, radius, weight, occupantFlags);
  }
}

ecs_view_define(BlockerView) {
  ecs_access_maybe_read(SceneNavAgentComp);
  ecs_access_maybe_read(SceneScaleComp);
  ecs_access_maybe_read(SceneTransformComp);
  ecs_access_read(SceneCollisionComp);
  ecs_access_write(SceneNavBlockerComp);
}

ecs_view_define(OccupantView) {
  ecs_access_maybe_read(SceneNavAgentComp);
  ecs_access_maybe_read(SceneScaleComp);
  ecs_access_read(SceneLocomotionComp);
  ecs_access_read(SceneTransformComp);
  ecs_access_with(SceneNavAgentComp);
}

ecs_view_define(PathView) { ecs_access_write(SceneNavPathComp); }

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

static SceneNavBlockerMask nav_mask_smaller(const SceneNavLayer layer) {
  return SceneNavBlockerMask_All & ~bit_range_32(layer, SceneNavLayer_Count);
}

ecs_system_define(SceneNavBlockerDirtySys) {
  if (!ecs_world_has_t(world, ecs_world_global(world), SceneTerrainComp) ||
      !ecs_world_has_t(world, ecs_world_global(world), SceneNavEnvComp)) {
    /**
     * Global dependencies for 'SceneNavInitSys" are not ready yet, that means we also need to wait
     * with updating the blocker dirty flags as otherwise we risk it being missed.
     */
    return;
  }

  EcsView* blockerView = ecs_world_view_t(world, BlockerView);

  for (EcsIterator* itr = ecs_view_itr_step(blockerView, parCount, parIndex); ecs_view_walk(itr);) {
    const SceneCollisionComp* collision = ecs_view_read_t(itr, SceneCollisionComp);
    const SceneNavAgentComp*  navAgent  = ecs_view_read_t(itr, SceneNavAgentComp);
    const SceneScaleComp*     scale     = ecs_view_read_t(itr, SceneScaleComp);
    const SceneTransformComp* trans     = ecs_view_read_t(itr, SceneTransformComp);
    SceneNavBlockerComp*      blocker   = ecs_view_write_t(itr, SceneNavBlockerComp);

    // Check if the blocker was changed (for example moved).
    const u32 newHash = nav_blocker_hash(collision, trans, scale);
    if (newHash == blocker->hash) {
      blocker->flags &= ~SceneNavBlockerFlags_Dirty;
    } else {
      blocker->flags |= SceneNavBlockerFlags_Dirty;
      blocker->hash = newHash;
    }

    /**
     * Navigation agents that are also blockers will block the smaller nav-layers (they cannot
     * block their own layer or bigger) if they are not traveling.
     */
    if (navAgent) {
      const bool                isTraveling = (navAgent->flags & SceneNavAgent_Traveling) != 0;
      const SceneNavBlockerMask desiredMask = isTraveling ? 0 : nav_mask_smaller(navAgent->layer);
      if (blocker->mask != desiredMask) {
        blocker->mask = desiredMask;
        blocker->flags |= SceneNavBlockerFlags_Dirty;
      }
    }
  }
}

ecs_view_define(InitGlobalView) {
  ecs_access_read(SceneTerrainComp);
  ecs_access_write(SceneNavEnvComp);
}

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

  EcsView* blockerView  = ecs_world_view_t(world, BlockerView);
  EcsView* pathView     = ecs_world_view_t(world, PathView);
  EcsView* occupantView = ecs_world_view_t(world, OccupantView);

  for (SceneNavLayer layer = 0; layer != SceneNavLayer_Count; ++layer) {
    NavInitContext ctx = {
        .grid           = env->grids[layer],
        .terrain        = terrain,
        .terrainVersion = env->terrainVersion,
        .layer          = layer,
    };

    trace_begin("nav_refresh_terrain", TraceColor_Red);
    nav_refresh_terrain(&ctx);
    trace_end();

    trace_begin("nav_refresh_blockers", TraceColor_Red);
    nav_refresh_blockers(&ctx, blockerView);
    trace_end();

    trace_begin("nav_refresh_paths", TraceColor_Red);
    nav_refresh_paths(&ctx, pathView);
    trace_end();

    trace_begin("nav_refresh_occupants", TraceColor_Red);
    nav_refresh_occupants(&ctx, occupantView);
    trace_end();

    trace_begin("nav_refresh_islands", TraceColor_Red);
    const bool islandRefresh = (ctx.change & NavChange_IslandRefresh) != 0;
    geo_nav_island_update(ctx.grid, islandRefresh);
    trace_end();

    env->grids[layer] = ctx.grid;
  }
  env->terrainVersion = scene_terrain_version(terrain);
}

ecs_view_define(UpdateAgentGlobalView) {
  ecs_access_read(SceneNavEnvComp);
  ecs_access_read(SceneTimeComp);
}

ecs_view_define(AgentView) {
  ecs_access_read(SceneTransformComp);
  ecs_access_write(SceneLocomotionComp);
  ecs_access_write(SceneNavAgentComp);
  ecs_access_write(SceneNavPathComp);
}

ecs_view_define(TargetView) {
  ecs_access_read(SceneTransformComp);
  ecs_access_maybe_read(SceneNavBlockerComp);
}

static bool path_needs_refresh(
    const SceneNavAgentComp* agent,
    const SceneNavPathComp*  path,
    const GeoVector          targetPos,
    const SceneTimeComp*     time) {
  if (time->time >= path->nextRefreshTime) {
    return true; // Too much time has elapsed.
  }
  const f32 distToDestSqr = geo_vector_mag_sqr(geo_vector_sub(path->destination, targetPos));
  if (distToDestSqr > (path_refresh_max_dist * path_refresh_max_dist)) {
    return true; // New destination is too far from the old destination.
  }
  if (agent->layer != path->layer) {
    return true; // Agent changed layer.
  }
  return false; // Path still valid.
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
nav_goal_pos(const GeoNavGrid* grid, const GeoNavCell fromCell, const GeoVector targetPos) {
  const GeoNavCell targetCell = geo_nav_at_position(grid, targetPos);
  if (geo_nav_reachable(grid, fromCell, targetCell)) {
    /**
     * Because we navigate until the agent's center is at the position we need to make sure the
     * target position is not too close to a blocker to reach.
     */
    const GeoVector offset = geo_nav_separate_from_blockers(grid, targetPos);
    return (SceneNavGoal){.cell = targetCell, .position = geo_vector_add(targetPos, offset)};
  }
  const GeoNavCell reachableCell = geo_nav_closest_reachable(grid, fromCell, targetCell);
  const GeoVector  reachablePos  = geo_nav_position(grid, reachableCell);
  return (SceneNavGoal){.cell = reachableCell, .position = reachablePos};
}

static SceneNavGoal nav_goal_entity(
    const GeoNavGrid*   grid,
    const SceneNavLayer layer,
    const GeoNavCell    fromCell,
    EcsIterator*        targetItr) {
  const SceneTransformComp*  targetTrans = ecs_view_read_t(targetItr, SceneTransformComp);
  const SceneNavBlockerComp* blocker     = ecs_view_read_t(targetItr, SceneNavBlockerComp);
  if (blocker && !sentinel_check(blocker->ids[layer])) {
    const GeoNavCell closest = geo_nav_blocker_closest(grid, blocker->ids[layer], fromCell);
    return (SceneNavGoal){.cell = closest, .position = geo_nav_position(grid, closest)};
  }
  return nav_goal_pos(grid, fromCell, targetTrans->position);
}

static void nav_move_towards(
    const GeoNavGrid*    grid,
    SceneLocomotionComp* loco,
    const SceneNavGoal*  goal,
    const GeoNavCell     cell) {
  GeoVector locoPos;
  if (cell.data == goal->cell.data) {
    locoPos = goal->position;
  } else {
    locoPos = geo_nav_position(grid, cell);
  }
  scene_locomotion_move(loco, locoPos);
}

/**
 * What navigation condition should block taking a direct shortcut to the given cell.
 */
static GeoNavCond
nav_shortcut_block_cond(const GeoNavGrid* grid, const GeoNavCell from, const GeoNavCell to) {
  /**
   * When checking far enough away make cells with stationary occupants block shortcuts as we hope
   * that we can take a route that only crosses free cells, once we start getting close allow moving
   * through occupied cells.
   */
  const u16 cellDist = geo_nav_chebyshev_dist(grid, from, to);
  if (cellDist >= path_avoid_occupied_cell_dist) {
    return GeoNavCond_NonFree;
  }
  return GeoNavCond_Blocked;
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
  EcsView* agentsView           = ecs_world_view_t(world, AgentView);

  EcsView*     targetView = ecs_world_view_t(world, TargetView);
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

    const GeoNavGrid* grid     = env->grids[agent->layer];
    const GeoNavCell  fromCell = geo_nav_at_position(grid, trans->position);
    SceneNavGoal      goal;
    if (agent->targetEntity) {
      if (!ecs_view_maybe_jump(targetItr, agent->targetEntity)) {
        goto Stop; // Target entity not valid (anymore).
      }
      goal = nav_goal_entity(grid, agent->layer, fromCell, targetItr);
    } else {
      goal = nav_goal_pos(grid, fromCell, agent->targetPos);
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
    if (pathQueriesRemaining && path_needs_refresh(agent, path, goal.position, time)) {
      const GeoNavCellContainer container = {.cells = path->cells, .capacity = path_max_cells};
      path->cellCount                     = geo_nav_path(grid, fromCell, goal.cell, container);
      path->nextRefreshTime               = path_next_refresh_time(time);
      path->destination                   = goal.position;
      path->currentTargetIndex            = 1; // Path includes the start point; should be skipped.
      path->layer                         = agent->layer;
      --pathQueriesRemaining;
    }

    if (!path->cellCount || path->layer != agent->layer) {
      goto Done; // Waiting for path to be computed.
    }

    // Attempt to take a shortcut as far up the path as possible without being obstructed.
    for (u16 i = path->cellCount; --i > path->currentTargetIndex;) {
      const GeoVector  pathPos      = geo_nav_position(grid, path->cells[i]);
      const GeoNavCond shortcutCond = nav_shortcut_block_cond(grid, fromCell, path->cells[i]);
      if (!geo_nav_check_channel(grid, trans->position, pathPos, shortcutCond)) {
        path->currentTargetIndex = i;
        nav_move_towards(grid, loco, &goal, path->cells[i]);
        goto Done;
      }
    }

    // No shortcut available; move to the current target cell in the path.
    nav_move_towards(grid, loco, &goal, path->cells[path->currentTargetIndex]);

  Done:
    continue;
  }
}

ecs_view_define(UpdateStatsGlobalView) { ecs_access_write(SceneNavEnvComp); }

ecs_system_define(SceneNavUpdateStatsSys) {
  EcsView*     globalView = ecs_world_view_t(world, UpdateStatsGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  SceneNavEnvComp* env = ecs_view_write_t(globalItr, SceneNavEnvComp);

  for (SceneNavLayer layer = 0; layer != SceneNavLayer_Count; ++layer) {
    GeoNavGrid* grid     = env->grids[layer];
    const u32*  statsSrc = geo_nav_stats(grid);
    u32*        statsDst = env->gridStats[layer];

    // Copy the grid stats into the stats component.
    const usize statsSize = sizeof(u32) * GeoNavStat_Count;
    mem_cpy(mem_create(statsDst, statsSize), mem_create(statsSrc, statsSize));

    geo_nav_stats_reset(grid);
  }
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
  ecs_register_comp(SceneNavEnvComp, .destructor = ecs_destruct_nav_env_comp, .destructOrder = 1);
  ecs_register_comp(SceneNavBlockerComp);
  ecs_register_comp(SceneNavAgentComp);
  ecs_register_comp(SceneNavPathComp, .destructor = ecs_destruct_nav_path_comp);
  ecs_register_comp(SceneNavRequestComp);

  ecs_register_view(BlockerView);
  ecs_register_view(OccupantView);
  ecs_register_view(PathView);

  ecs_register_system(SceneNavBlockerDirtySys, ecs_view_id(BlockerView));
  ecs_order(SceneNavBlockerDirtySys, SceneOrder_NavInit - 1);
  ecs_parallel(SceneNavBlockerDirtySys, 2);

  ecs_register_system(
      SceneNavInitSys,
      ecs_register_view(InitGlobalView),
      ecs_view_id(BlockerView),
      ecs_view_id(OccupantView),
      ecs_view_id(PathView));

  ecs_order(SceneNavInitSys, SceneOrder_NavInit);

  ecs_register_system(
      SceneNavUpdateAgentsSys,
      ecs_register_view(UpdateAgentGlobalView),
      ecs_register_view(AgentView),
      ecs_register_view(TargetView));

  ecs_parallel(SceneNavUpdateAgentsSys, g_jobsWorkerCount * 2);

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

void scene_nav_add_blocker(EcsWorld* w, const EcsEntityId e, const SceneNavBlockerMask mask) {
  SceneNavBlockerComp* blocker = ecs_world_add_t(w, e, SceneNavBlockerComp, .mask = mask);
  for (SceneNavLayer layer = 0; layer != SceneNavLayer_Count; ++layer) {
    blocker->ids[layer] = geo_blocker_invalid;
  }
}

SceneNavAgentComp* scene_nav_add_agent(
    EcsWorld* w, SceneNavEnvComp* navEnv, const EcsEntityId e, const SceneNavLayer layer) {

  ecs_world_add_t(
      w, e, SceneNavPathComp, .pathAlloc = navEnv->pathAlloc, .cells = nav_path_alloc(navEnv));

  return ecs_world_add_t(w, e, SceneNavAgentComp, .layer = layer);
}

const u32* scene_nav_grid_stats(const SceneNavEnvComp* env, const SceneNavLayer layer) {
  return env->gridStats[layer];
}

const GeoNavGrid* scene_nav_grid(const SceneNavEnvComp* env, const SceneNavLayer layer) {
  return env->grids[layer];
}
