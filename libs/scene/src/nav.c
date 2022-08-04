#include "core_alloc.h"
#include "core_math.h"
#include "ecs_world.h"
#include "scene_collision.h"
#include "scene_nav.h"
#include "scene_register.h"

static const GeoVector g_sceneNavCenter  = {0, 0, 0};
static const f32       g_sceneNavSize    = 100.0f;
static const f32       g_sceneNavDensity = 1.0f;
static const f32       g_sceneNavHeight  = 2.0f;

ecs_comp_define(SceneNavEnvComp) { GeoNavGrid* navGrid; };

ecs_comp_define(SceneNavBlockerComp);

static void ecs_destruct_nav_env_comp(void* data) {
  SceneNavEnvComp* env = data;
  geo_nav_grid_destroy(env->navGrid);
}

ecs_view_define(UpdateGlobalView) { ecs_access_write(SceneNavEnvComp); }

ecs_view_define(BlockerEntityView) {
  ecs_access_maybe_read(SceneScaleComp);
  ecs_access_maybe_read(SceneTransformComp);
  ecs_access_read(SceneCollisionComp);
  ecs_access_with(SceneNavBlockerComp);
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

ecs_system_define(SceneNavUpdateSys) {
  if (!ecs_world_has_t(world, ecs_world_global(world), SceneNavEnvComp)) {
    scene_nav_env_create(world);
    return;
  }

  EcsView*     globalView = ecs_world_view_t(world, UpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }

  SceneNavEnvComp* env = ecs_view_write_t(globalItr, SceneNavEnvComp);
  geo_nav_blocker_clear_all(env->navGrid);

  EcsView* blockerEntities = ecs_world_view_t(world, BlockerEntityView);
  scene_nav_add_blockers(env, blockerEntities);
}

ecs_module_init(scene_nav_module) {
  ecs_register_comp(SceneNavEnvComp, .destructor = ecs_destruct_nav_env_comp);
  ecs_register_comp_empty(SceneNavBlockerComp);

  ecs_register_view(UpdateGlobalView);
  ecs_register_view(BlockerEntityView);

  ecs_register_system(
      SceneNavUpdateSys, ecs_view_id(UpdateGlobalView), ecs_view_id(BlockerEntityView));
}

void scene_nav_add_blocker(EcsWorld* world, const EcsEntityId entity) {
  ecs_world_add_empty_t(world, entity, SceneNavBlockerComp);
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
