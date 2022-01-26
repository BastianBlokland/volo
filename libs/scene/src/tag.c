#include "ecs_world.h"
#include "scene_tag.h"

ecs_comp_define_public(SceneTagComp);

static void ecs_combine_tags(void* dataA, void* dataB) {
  SceneTagComp* compA = dataA;
  SceneTagComp* compB = dataB;
  compA->tags |= compB->tags;
}

ecs_module_init(scene_tag_module) {
  ecs_register_comp(SceneTagComp, .combinator = ecs_combine_tags);
}

void scene_tag_add(EcsWorld* world, const EcsEntityId entity, const SceneTags tags) {
  ecs_world_add_t(world, entity, SceneTagComp, .tags = tags);
}

bool scene_tag_filter(const SceneTagFilter filter, const SceneTags tags) {
  return ((tags & filter.required) == filter.required) && ((tags & filter.illegal) == 0);
}
