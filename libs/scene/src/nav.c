#include "core_alloc.h"
#include "core_array.h"
#include "core_math.h"
#include "core_rng.h"
#include "ecs_world.h"
#include "scene_collision.h"
#include "scene_locomotion.h"
#include "scene_nav.h"
#include "scene_register.h"
#include "scene_time.h"
#include "scene_transform.h"

static const GeoVector g_sceneNavCenter  = {0, 0, 0};
static const f32       g_sceneNavSize    = 100.0f;
static const f32       g_sceneNavDensity = 1.0f;
static const f32       g_sceneNavHeight  = 2.0f;

#define path_max_cells 64
#define path_max_queries 100
#define path_refresh_time_min time_seconds(3)
#define path_refresh_time_max time_seconds(5)
#define path_refresh_max_dist 2.0f
#define nav_arrive_threshold 0.25f

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

static void scene_nav_add_blocker_box(SceneNavEnvComp* env, const GeoBox* box) {
  geo_nav_blocker_add_box(env->navGrid, box);
}

static void scene_nav_add_blocker_box_rotated(SceneNavEnvComp* env, const GeoBoxRotated* boxRot) {
  if (math_abs(geo_quat_dot(boxRot->rotation, geo_quat_ident)) > 1.0f - 1e-4f) {
    /**
     * Substitute rotated-boxes with a (near) identity rotation with axis-aligned boxes which are
     * much faster to insert.
     */
    geo_nav_blocker_add_box(env->navGrid, &boxRot->box);
  } else {
    geo_nav_blocker_add_box_rotated(env->navGrid, boxRot);
  }
}

static void scene_nav_add_blockers(SceneNavEnvComp* env, EcsView* blockerEntities) {
  for (EcsIterator* itr = ecs_view_itr(blockerEntities); ecs_view_walk(itr);) {
    const SceneCollisionComp* collision = ecs_view_read_t(itr, SceneCollisionComp);
    const SceneTransformComp* trans     = ecs_view_read_t(itr, SceneTransformComp);
    const SceneScaleComp*     scale     = ecs_view_read_t(itr, SceneScaleComp);

    switch (collision->type) {
    case SceneCollisionType_Sphere: {
      // NOTE: Uses the sphere bounds at the moment, if more accurate sphere blockers are needed
      // then sphere support should be added to GeoNavGrid.
      const GeoSphere s       = scene_collision_world_sphere(&collision->sphere, trans, scale);
      const GeoBox    sBounds = geo_box_from_sphere(s.point, s.radius);
      scene_nav_add_blocker_box(env, &sBounds);
    } break;
    case SceneCollisionType_Capsule: {
      const GeoCapsule    c = scene_collision_world_capsule(&collision->capsule, trans, scale);
      const GeoBoxRotated cBounds = geo_box_rotated_from_capsule(c.line.a, c.line.b, c.radius);
      scene_nav_add_blocker_box_rotated(env, &cBounds);
    } break;
    case SceneCollisionType_Box: {
      const GeoBoxRotated b = scene_collision_world_box(&collision->box, trans, scale);
      scene_nav_add_blocker_box_rotated(env, &b);
    } break;
    case SceneCollisionType_Count:
      UNREACHABLE
    }
  }
}

static void scene_nav_add_occupants(SceneNavEnvComp* env, EcsView* occupantEntities) {
  for (EcsIterator* itr = ecs_view_itr(occupantEntities); ecs_view_walk(itr);) {
    const SceneTransformComp*  trans = ecs_view_read_t(itr, SceneTransformComp);
    const SceneLocomotionComp* loco  = ecs_view_read_t(itr, SceneLocomotionComp);

    const u64           occupantId    = (u64)ecs_view_entity(itr);
    GeoNavOccupantFlags occupantFlags = 0;
    if (loco->flags & SceneLocomotion_Moving) {
      occupantFlags |= GeoNavOccupantFlags_Moving;
    }
    geo_nav_occupant_add(env->navGrid, occupantId, trans->position, loco->radius, occupantFlags);
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
  ecs_access_with(SceneNavBlockerComp);
}

ecs_view_define(OccupantEntityView) {
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

  geo_nav_blocker_clear_all(env->navGrid);
  EcsView* blockerEntities = ecs_world_view_t(world, BlockerEntityView);
  scene_nav_add_blockers(env, blockerEntities);

  geo_nav_occupant_clear_all(env->navGrid);
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

ecs_system_define(SceneNavUpdateAgentsSys) {
  EcsView*     globalView = ecs_world_view_t(world, UpdateAgentGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneNavEnvComp* env  = ecs_view_read_t(globalItr, SceneNavEnvComp);
  const SceneTimeComp*   time = ecs_view_read_t(globalItr, SceneTimeComp);

  // Limit the amount of path queries per-frame.
  u32 pathQueriesRemaining = path_max_queries;

  EcsView* agentEntities = ecs_world_view_t(world, AgentEntityView);
  for (EcsIterator* itr = ecs_view_itr(agentEntities); ecs_view_walk(itr);) {
    const SceneTransformComp* trans = ecs_view_read_t(itr, SceneTransformComp);
    SceneLocomotionComp*      loco  = ecs_view_write_t(itr, SceneLocomotionComp);
    SceneNavAgentComp*        agent = ecs_view_write_t(itr, SceneNavAgentComp);
    SceneNavPathComp*         path  = ecs_view_write_t(itr, SceneNavPathComp);

    if (!(agent->flags & SceneNavAgent_Traveling)) {
      agent->flags &= ~SceneNavAgent_Stop;
      goto Done;
    }

    GeoVector  toPos     = agent->target;
    GeoNavCell toCell    = geo_nav_at_position(env->navGrid, toPos);
    const bool toBlocked = geo_nav_blocked(env->navGrid, toCell);
    if (toBlocked) {
      // Target is not reachable; pick the closest reachable point.
      toCell = geo_nav_closest_unblocked(env->navGrid, toCell);
      toPos  = geo_nav_position(env->navGrid, toCell);
    }

    const f32 distToTarget = geo_vector_mag(geo_vector_sub(toPos, trans->position));
    if (distToTarget <= (loco->radius + nav_arrive_threshold)) {
      agent->flags |= SceneNavAgent_Stop; // Arrived at destination.
    }

    if (agent->flags & SceneNavAgent_Stop) {
      agent->flags &= ~(SceneNavAgent_Stop | SceneNavAgent_Traveling);
      scene_locomotion_stop(loco);
      goto Done;
    }

    const GeoNavCell fromCell = geo_nav_at_position(env->navGrid, trans->position);
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

    // No path possible.
    agent->flags |= SceneNavAgent_Stop;

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
  ecs_register_comp_empty(SceneNavBlockerComp);
  ecs_register_comp(SceneNavAgentComp);
  ecs_register_comp(SceneNavPathComp, .destructor = ecs_destruct_nav_path_comp);

  ecs_register_system(
      SceneNavInitSys,
      ecs_register_view(InitGlobalView),
      ecs_register_view(BlockerEntityView),
      ecs_register_view(OccupantEntityView));

  ecs_register_system(
      SceneNavUpdateAgentsSys,
      ecs_register_view(UpdateAgentGlobalView),
      ecs_register_view(AgentEntityView));

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

bool scene_nav_occupied(const SceneNavEnvComp* env, const GeoNavCell cell) {
  return geo_nav_occupied(env->navGrid, cell);
}

bool scene_nav_occupied_moving(const SceneNavEnvComp* env, const GeoNavCell cell) {
  return geo_nav_occupied_moving(env->navGrid, cell);
}

GeoNavCell scene_nav_at_position(const SceneNavEnvComp* env, const GeoVector pos) {
  return geo_nav_at_position(env->navGrid, pos);
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
