#include "asset_register.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "ecs.h"
#include "ecs_utils.h"
#include "scene_register.h"
#include "scene_set.h"

ecs_view_define(SetEnvView) { ecs_access_write(SceneSetEnvComp); }
ecs_view_define(SetMemberView) { ecs_access_read(SceneSetMemberComp); }

ecs_module_init(set_test_module) {
  ecs_register_view(SetEnvView);
  ecs_register_view(SetMemberView);
}

spec(set) {

  EcsDef*     def    = null;
  EcsWorld*   world  = null;
  EcsRunner*  runner = null;
  EcsEntityId g      = 0;

  setup() {
    def = ecs_def_create(g_alloc_heap);
    asset_register(def);
    scene_register(def);
    ecs_register_module(def, set_test_module);

    world  = ecs_world_create(g_alloc_heap, def);
    runner = ecs_runner_create(g_alloc_heap, world, EcsRunnerFlags_None);
    g      = ecs_world_global(world);

    ecs_run_sync(runner);
  }

  it("can add entities") {
    const StringHash set = string_hash_lit("test");
    SceneSetEnvComp* env = ecs_utils_write_t(world, SetEnvView, g, SceneSetEnvComp);

    check_eq_int(scene_set_count(env, set), 0);

    const EcsEntityId e1 = ecs_world_entity_create(world);
    scene_set_add(env, string_hash_lit("test"), e1);

    ecs_run_sync(runner);

    check_eq_int(scene_set_count(env, set), 1);
    check_eq_int(scene_set_main(env, set), e1);
    check_eq_int(*scene_set_begin(env, set), e1);
  }

  teardown() {
    ecs_runner_destroy(runner);
    ecs_world_destroy(world);
    ecs_def_destroy(def);
  }
}
