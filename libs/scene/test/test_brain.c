#include "asset_manager.h"
#include "asset_register.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "ecs.h"
#include "ecs_utils.h"
#include "scene_brain.h"
#include "scene_knowledge.h"
#include "scene_register.h"

static const AssetMemRecord g_testBrainAssets[] = {
    {
        .id   = string_static("execute.bt"),
        .data = string_static("{\n"
                              "\"$type\": \"AssetAiNode_Execute\",\n"
                              "\"script\": \"$test = true\"\n"
                              "}"),
    },
};

static void scene_test_wait(EcsRunner* runner) {
  static const u32 g_numTicks = 5;
  for (u32 i = 0; i != g_numTicks; ++i) {
    ecs_run_sync(runner);
  }
}

ecs_view_define(BrainView) {
  ecs_access_write(SceneBrainComp);
  ecs_access_write(SceneKnowledgeComp);
}
ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }

ecs_module_init(brain_test_module) {
  ecs_register_view(BrainView);
  ecs_register_view(ManagerView);
}

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

  it("updates its knowledge through its behavior") {
    AssetManagerComp* manager       = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
    const EcsEntityId behaviorAsset = asset_lookup(world, manager, string_lit("execute.bt"));

    const EcsEntityId agent = ecs_world_entity_create(world);
    scene_brain_add(world, agent, behaviorAsset);
    scene_knowledge_add(world, agent);

    scene_test_wait(runner);

    const SceneKnowledgeComp* know = ecs_utils_read_t(world, BrainView, agent, SceneKnowledgeComp);

    const ScriptVal value = scene_knowledge_get(know, string_hash_lit("test"));
    check(script_val_equal(value, script_bool(true)));
  }

  teardown() {
    ecs_runner_destroy(runner);
    ecs_world_destroy(world);
    ecs_def_destroy(def);
  }
}
