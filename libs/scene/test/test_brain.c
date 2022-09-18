#include "ai_blackboard.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "ecs.h"
#include "scene_brain.h"
#include "scene_register.h"

spec(brain) {

  EcsDef*    def    = null;
  EcsWorld*  world  = null;
  EcsRunner* runner = null;

  setup() {
    def = ecs_def_create(g_alloc_heap);
    scene_register(def);

    world  = ecs_world_create(g_alloc_heap, def);
    runner = ecs_runner_create(g_alloc_heap, world, EcsRunnerFlags_None);
    ecs_run_sync(runner);
  }

  it("allows updating its blackboard knowledge") {
    // TODO: Add proper test behavior asset.
    const EcsEntityId behaviorAsset = ecs_world_entity_create(world);

    const EcsEntityId agent = ecs_world_entity_create(world);
    SceneBrainComp*   brain = scene_brain_add(world, agent, behaviorAsset);

    const StringHash knowledgeKey = string_hash_lit("test");

    check(!ai_blackboard_get_bool(scene_brain_blackboard(brain), knowledgeKey));
    ai_blackboard_set_bool(scene_brain_blackboard_mutable(brain), knowledgeKey, true);
    check(ai_blackboard_get_bool(scene_brain_blackboard(brain), knowledgeKey));
  }

  teardown() {
    ecs_runner_destroy(runner);
    ecs_world_destroy(world);
    ecs_def_destroy(def);
  }
}
