#include "ai_blackboard.h"
#include "ai_eval.h"
#include "core_alloc.h"
#include "ecs_world.h"
#include "scene_brain.h"

ecs_comp_define(SceneBrainComp) {
  AiBlackboard* blackboard;
  EcsEntityId   behaviorAsset;
};

static void ecs_destruct_brain_comp(void* data) {
  SceneBrainComp* brain = data;
  ai_blackboard_destroy(brain->blackboard);
}

ecs_module_init(scene_brain_module) {
  ecs_register_comp(SceneBrainComp, .destructor = ecs_destruct_brain_comp);
}

const AiBlackboard* scene_brain_blackboard(const SceneBrainComp* brain) {
  return brain->blackboard;
}

AiBlackboard* scene_brain_blackboard_mutable(SceneBrainComp* brain) { return brain->blackboard; }

void scene_brain_add(EcsWorld* world, const EcsEntityId entity, const EcsEntityId behaviorAsset) {
  ecs_world_add_t(
      world,
      entity,
      SceneBrainComp,
      .blackboard    = ai_blackboard_create(g_alloc_heap),
      .behaviorAsset = behaviorAsset);
}
