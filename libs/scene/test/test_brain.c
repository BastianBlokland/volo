#include "ai_blackboard.h"
#include "asset_manager.h"
#include "asset_register.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "ecs.h"
#include "ecs_utils.h"
#include "scene_brain.h"
#include "scene_register.h"

static const AssetMemRecord g_testBrainAssets[] = {
    {
        .id   = string_lit("success.bt"),
        .data = string_static("{ \"$type\": \"AssetBehavior_Success\" }"),
    },
};

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }

ecs_module_init(brain_test_module) { ecs_register_view(ManagerView); }

spec(brain) {

  EcsDef*    def    = null;
  EcsWorld*  world  = null;
  EcsRunner* runner = null;

  setup() {
    def = ecs_def_create(g_alloc_heap);
    asset_register(def);
    scene_register(def);
    ecs_register_module(def, brain_test_module);

    world  = ecs_world_create(g_alloc_heap, def);
    runner = ecs_runner_create(g_alloc_heap, world, EcsRunnerFlags_None);

    asset_manager_create_mem(
        world, AssetManagerFlags_None, g_testBrainAssets, array_elems(g_testBrainAssets));

    ecs_run_sync(runner);
  }

  it("allows updating its blackboard knowledge") {
    AssetManagerComp* manager       = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
    const EcsEntityId behaviorAsset = asset_lookup(world, manager, string_lit("success.bt"));

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
