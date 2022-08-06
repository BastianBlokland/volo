#include "core_alloc.h"
#include "core_math.h"
#include "ecs_world.h"
#include "scene_collision.h"
#include "scene_locomotion.h"
#include "scene_nav.h"
#include "scene_register.h"
#include "scene_transform.h"

static const GeoVector g_sceneNavCenter  = {0, 0, 0};
static const f32       g_sceneNavSize    = 100.0f;
static const f32       g_sceneNavDensity = 1.0f;
static const f32       g_sceneNavHeight  = 2.0f;

#define scene_nav_path_max_cells 64

ecs_comp_define(SceneNavEnvComp) { GeoNavGrid* navGrid; };

ecs_comp_define_public(SceneNavBlockerComp);
ecs_comp_define_public(SceneNavAgentComp);
ecs_comp_define_public(SceneNavPathComp);

static void ecs_destruct_nav_env_comp(void* data) {
  SceneNavEnvComp* comp = data;
  geo_nav_grid_destroy(comp->navGrid);
}

static void ecs_destruct_nav_path_comp(void* data) {
  SceneNavPathComp* comp = data;
  alloc_free_array_t(g_alloc_heap, comp->cells, scene_nav_path_max_cells);
}

static SceneNavEnvComp* scene_nav_env_create(EcsWorld* world) {
  GeoNavGrid* grid = geo_nav_grid_create(
      g_alloc_heap, g_sceneNavCenter, g_sceneNavSize, g_sceneNavDensity, g_sceneNavHeight);

  return ecs_world_add_t(world, ecs_world_global(world), SceneNavEnvComp, .navGrid = grid);
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
    }
  }
}

ecs_view_define(UpdateBlockerGlobalView) { ecs_access_write(SceneNavEnvComp); }

ecs_view_define(BlockerEntityView) {
  ecs_access_maybe_read(SceneScaleComp);
  ecs_access_maybe_read(SceneTransformComp);
  ecs_access_read(SceneCollisionComp);
  ecs_access_with(SceneNavBlockerComp);
}

ecs_system_define(SceneNavUpdateBlockersSys) {
  if (!ecs_world_has_t(world, ecs_world_global(world), SceneNavEnvComp)) {
    scene_nav_env_create(world);
    return;
  }

  EcsView*     globalView = ecs_world_view_t(world, UpdateBlockerGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }

  SceneNavEnvComp* env = ecs_view_write_t(globalItr, SceneNavEnvComp);
  geo_nav_blocker_clear_all(env->navGrid);

  EcsView* blockerEntities = ecs_world_view_t(world, BlockerEntityView);
  scene_nav_add_blockers(env, blockerEntities);
}

ecs_view_define(UpdateAgentGlobalView) { ecs_access_read(SceneNavEnvComp); }

ecs_view_define(AgentEntityView) {
  ecs_access_read(SceneNavAgentComp);
  ecs_access_read(SceneTransformComp);
  ecs_access_write(SceneLocomotionComp);
  ecs_access_write(SceneNavPathComp);
}

ecs_system_define(SceneNavUpdateAgentsSys) {
  EcsView*     globalView = ecs_world_view_t(world, UpdateAgentGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneNavEnvComp* env = ecs_view_read_t(globalItr, SceneNavEnvComp);

  EcsView* agentEntities = ecs_world_view_t(world, AgentEntityView);
  for (EcsIterator* itr = ecs_view_itr(agentEntities); ecs_view_walk(itr);) {
    const SceneNavAgentComp*  agent      = ecs_view_read_t(itr, SceneNavAgentComp);
    const SceneTransformComp* trans      = ecs_view_read_t(itr, SceneTransformComp);
    SceneLocomotionComp*      locomotion = ecs_view_write_t(itr, SceneLocomotionComp);
    SceneNavPathComp*         path       = ecs_view_write_t(itr, SceneNavPathComp);

    const GeoNavCell from = geo_nav_at_position(env->navGrid, trans->position);
    const GeoNavCell to   = geo_nav_at_position(env->navGrid, agent->target);

    const GeoNavPathStorage storage = {.cells = path->cells, .capacity = scene_nav_path_max_cells};
    path->cellCount                 = geo_nav_path(env->navGrid, from, to, storage);

    if (path->cellCount > 1) {
      locomotion->target = geo_nav_position(env->navGrid, path->cells[1]);
    } else {
      locomotion->target = trans->position;
    }
  }
}

ecs_module_init(scene_nav_module) {
  ecs_register_comp(SceneNavEnvComp, .destructor = ecs_destruct_nav_env_comp);
  ecs_register_comp_empty(SceneNavBlockerComp);
  ecs_register_comp(SceneNavAgentComp);
  ecs_register_comp(SceneNavPathComp, .destructor = ecs_destruct_nav_path_comp);

  ecs_register_system(
      SceneNavUpdateBlockersSys,
      ecs_register_view(UpdateBlockerGlobalView),
      ecs_register_view(BlockerEntityView));

  ecs_register_system(
      SceneNavUpdateAgentsSys,
      ecs_register_view(UpdateAgentGlobalView),
      ecs_register_view(AgentEntityView));
}

void scene_nav_add_blocker(EcsWorld* world, const EcsEntityId entity) {
  ecs_world_add_empty_t(world, entity, SceneNavBlockerComp);
}

void scene_nav_add_agent(EcsWorld* world, const EcsEntityId entity, const GeoVector target) {
  ecs_world_add_t(world, entity, SceneNavAgentComp, .target = target);
  ecs_world_add_t(
      world,
      entity,
      SceneNavPathComp,
      .cells = alloc_array_t(g_alloc_heap, GeoNavCell, scene_nav_path_max_cells));
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

bool scene_nav_blocked(const SceneNavEnvComp* env, const GeoNavCell cell) {
  return geo_nav_blocked(env->navGrid, cell);
}
