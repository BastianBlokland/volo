#include "core_alloc.h"
#include "ecs_world.h"
#include "scene_nav.h"
#include "scene_register.h"

static const GeoVector g_sceneNavCenter  = {0, 0, 0};
static const f32       g_sceneNavSize    = 100.0f;
static const f32       g_sceneNavDensity = 1.0f;

ecs_comp_define(SceneNavEnvComp) { GeoNavGrid* navGrid; };

static void ecs_destruct_nav_env_comp(void* data) {
  SceneNavEnvComp* env = data;
  geo_nav_grid_destroy(env->navGrid);
}

ecs_view_define(UpdateGlobalView) { ecs_access_write(SceneNavEnvComp); }

static SceneNavEnvComp* nav_env_create(EcsWorld* world) {
  GeoNavGrid* grid =
      geo_nav_grid_create(g_alloc_heap, g_sceneNavCenter, g_sceneNavSize, g_sceneNavDensity);

  return ecs_world_add_t(world, ecs_world_global(world), SceneNavEnvComp, .navGrid = grid);
}

ecs_system_define(SceneNavUpdateSys) {
  if (!ecs_world_has_t(world, ecs_world_global(world), SceneNavEnvComp)) {
    nav_env_create(world);
    return;
  }
}

ecs_module_init(scene_nav_module) {
  ecs_register_comp(SceneNavEnvComp, .destructor = ecs_destruct_nav_env_comp);

  ecs_register_view(UpdateGlobalView);

  ecs_register_system(SceneNavUpdateSys, ecs_view_id(UpdateGlobalView));
}

GeoNavRegion scene_nav_bounds(const SceneNavEnvComp* env) { return geo_nav_bounds(env->navGrid); }

GeoVector scene_nav_position(const SceneNavEnvComp* env, const GeoNavCell cell) {
  return geo_nav_position(env->navGrid, cell);
}
