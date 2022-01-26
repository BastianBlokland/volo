#include "core_alloc.h"
#include "ecs_world.h"
#include "scene_stats.h"

ecs_comp_define_public(SceneStatsCamComp);

static void ecs_destruct_rend_stats_comp(void* data) {
  SceneStatsCamComp* comp = data;
  if (!string_is_empty(comp->gpuName)) {
    string_free(g_alloc_heap, comp->gpuName);
  }
}

ecs_module_init(scene_stats_module) {
  ecs_register_comp(SceneStatsCamComp, .destructor = ecs_destruct_rend_stats_comp);
}
