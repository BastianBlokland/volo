#include "asset_manager.h"
#include "asset_register.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "ecs.h"
#include "ecs_utils.h"
#include "scene_knowledge.h"
#include "scene_register.h"
#include "scene_script.h"

static const AssetMemRecord g_testScriptAssets[] = {
    {
        .id   = string_static("set_knowledge.script"),
        .data = string_static("$test = 42"),
    },
};

static void scene_test_wait(EcsRunner* runner) {
  static const u32 g_numTicks = 5;
  for (u32 i = 0; i != g_numTicks; ++i) {
    ecs_run_sync(runner);
  }
}

ecs_view_define(ScriptView) {
  ecs_access_write(SceneScriptComp);
  ecs_access_write(SceneKnowledgeComp);
}
ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }

ecs_module_init(script_test_module) {
  ecs_register_view(ScriptView);
  ecs_register_view(ManagerView);
}

spec(script) {

  EcsDef*    def    = null;
  EcsWorld*  world  = null;
  EcsRunner* runner = null;

  setup() {
    def = ecs_def_create(g_allocHeap);
    asset_register(def);
    scene_register(def);
    ecs_register_module(def, script_test_module);

    world  = ecs_world_create(g_allocHeap, def);
    runner = ecs_runner_create(g_allocHeap, world, EcsRunnerFlags_None);

    asset_manager_create_mem(
        world, AssetManagerFlags_None, g_testScriptAssets, array_elems(g_testScriptAssets));

    scene_test_wait(runner);
  }

  it("can set knowledge") {
    AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
    const EcsEntityId scriptAssets[] = {
        asset_lookup(world, manager, string_lit("set_knowledge.script")),
    };

    const EcsEntityId e = ecs_world_entity_create(world);
    scene_script_add(world, e, scriptAssets, array_elems(scriptAssets));
    scene_knowledge_add(world, e);

    scene_test_wait(runner);

    const SceneKnowledgeComp* know = ecs_utils_read_t(world, ScriptView, e, SceneKnowledgeComp);

    const ScriptVal value = scene_knowledge_load(know, string_hash_lit("test"));
    check(script_val_equal(value, script_num(42)));
  }

  teardown() {
    ecs_runner_destroy(runner);
    ecs_world_destroy(world);
    ecs_def_destroy(def);
  }
}
