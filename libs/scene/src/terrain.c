#include "core_alloc.h"
#include "core_diag.h"
#include "ecs_world.h"
#include "scene_renderable.h"

ecs_comp_define(SceneTerrainComp) {
  String graphicId;
  f32    size;
};

static void ecs_destruct_terrain(void* data) {
  SceneTerrainComp* comp = data;
  string_free(g_alloc_heap, comp->graphicId);
}

ecs_module_init(scene_terrain_module) {
  ecs_register_comp(SceneTerrainComp, .destructor = ecs_destruct_terrain);
}

void scene_terrain_init(EcsWorld* world, const String graphicId) {
  diag_assert_msg(graphicId.size, "Invalid terrain graphicId");

  ecs_world_add_t(
      world,
      ecs_world_global(world),
      SceneTerrainComp,
      .graphicId = string_dup(g_alloc_heap, graphicId));
}
