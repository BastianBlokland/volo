#include "core_alloc.h"
#include "ecs_world.h"
#include "scene_camera.h"
#include "scene_stats.h"

ecs_comp_define_public(SceneStatsCamComp);

static void ecs_destruct_rend_stats_comp(void* data) {
  SceneStatsCamComp* comp = data;
  if (!string_is_empty(comp->gpuName)) {
    string_free(g_alloc_heap, comp->gpuName);
  }
}

ecs_view_define(CreateCameraStatsView) {
  ecs_access_with(SceneCameraComp);
  ecs_access_without(SceneStatsCamComp);
}

ecs_system_define(SceneCreateCamStatsSys) {
  EcsView* createView = ecs_world_view_t(world, CreateCameraStatsView);
  for (EcsIterator* itr = ecs_view_itr(createView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_world_add_t(world, entity, SceneStatsCamComp);
  }
}

ecs_module_init(scene_stats_module) {
  ecs_register_comp(SceneStatsCamComp, .destructor = ecs_destruct_rend_stats_comp);

  ecs_register_view(CreateCameraStatsView);

  ecs_register_system(SceneCreateCamStatsSys, ecs_view_id(CreateCameraStatsView));
}
