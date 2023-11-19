#include "asset_register.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
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

static SceneSetEnvComp* test_env(EcsWorld* world) {
  return ecs_utils_write_t(world, SetEnvView, ecs_world_global(world), SceneSetEnvComp);
}

spec(set) {

  EcsDef*    def    = null;
  EcsWorld*  world  = null;
  EcsRunner* runner = null;

  setup() {
    def = ecs_def_create(g_alloc_heap);
    asset_register(def);
    scene_register(def);
    ecs_register_module(def, set_test_module);

    world  = ecs_world_create(g_alloc_heap, def);
    runner = ecs_runner_create(g_alloc_heap, world, EcsRunnerFlags_None);

    ecs_run_sync(runner);
  }

  it("can initialize set-members") {
    EcsWorld*        w      = world;
    const StringHash sets[] = {string_hash_lit("testA"), string_hash_lit("testB")};

    const EcsEntityId e1 = ecs_world_entity_create(w);
    scene_set_member_create(world, e1, sets, array_elems(sets));

    ecs_run_sync(runner); // 1 run to flush the components adds.
    ecs_run_sync(runner); // 1 run to update the sets.

    array_for_t(sets, StringHash, setPtr) {
      check_eq_int(scene_set_count(test_env(w), *setPtr), 1);
      check_eq_int(scene_set_main(test_env(w), *setPtr), e1);
      check(scene_set_contains(test_env(w), *setPtr, e1));
    }
  }

  it("can add entities") {
    EcsWorld*        w   = world;
    const StringHash set = string_hash_lit("test");

    check_eq_int(scene_set_count(test_env(w), set), 0);

    const EcsEntityId e1 = ecs_world_entity_create(w);
    {
      scene_set_add(test_env(w), set, e1);
      ecs_run_sync(runner);

      check_eq_int(scene_set_count(test_env(w), set), 1);
      check_eq_int(scene_set_main(test_env(w), set), e1);
      check_eq_int(*scene_set_begin(test_env(w), set), e1);
      check(scene_set_contains(test_env(w), set, e1));
    }

    const EcsEntityId e2 = ecs_world_entity_create(w);
    const EcsEntityId e3 = ecs_world_entity_create(w);
    {
      scene_set_add(test_env(w), set, e2);
      scene_set_add(test_env(w), set, e3);
      ecs_run_sync(runner);

      check_eq_int(scene_set_count(test_env(w), set), 3);
      check_eq_int(scene_set_main(test_env(w), set), e1);
      check(scene_set_contains(test_env(w), set, e1));
      check(scene_set_contains(test_env(w), set, e2));
      check(scene_set_contains(test_env(w), set, e3));
    }
  }

  it("can remove entities") {
    EcsWorld*         w   = world;
    const StringHash  set = string_hash_lit("test");
    const EcsEntityId e1  = ecs_world_entity_create(w);
    const EcsEntityId e2  = ecs_world_entity_create(w);
    const EcsEntityId e3  = ecs_world_entity_create(w);

    {
      scene_set_add(test_env(w), set, e1);
      scene_set_add(test_env(w), set, e2);
      scene_set_add(test_env(w), set, e3);
      ecs_run_sync(runner);

      check_eq_int(scene_set_count(test_env(w), set), 3);
      check_eq_int(scene_set_main(test_env(w), set), e1);
      check(scene_set_contains(test_env(w), set, e1));
      check(scene_set_contains(test_env(w), set, e2));
      check(scene_set_contains(test_env(w), set, e3));
    }

    {
      scene_set_remove(test_env(w), set, e3);
      ecs_run_sync(runner);

      check_eq_int(scene_set_count(test_env(w), set), 2);
      check_eq_int(scene_set_main(test_env(w), set), e1);
      check(scene_set_contains(test_env(w), set, e1));
      check(scene_set_contains(test_env(w), set, e2));
      check(!scene_set_contains(test_env(w), set, e3));
    }

    {
      scene_set_remove(test_env(w), set, e1);
      ecs_run_sync(runner);

      check_eq_int(scene_set_count(test_env(w), set), 1);
      check_eq_int(scene_set_main(test_env(w), set), e2);
      check(!scene_set_contains(test_env(w), set, e1));
      check(scene_set_contains(test_env(w), set, e2));
      check(!scene_set_contains(test_env(w), set, e3));
    }

    {
      scene_set_remove(test_env(w), set, e2);
      ecs_run_sync(runner);

      check_eq_int(scene_set_count(test_env(w), set), 0);
      check_eq_int(scene_set_main(test_env(w), set), 0);
      check(!scene_set_contains(test_env(w), set, e1));
      check(!scene_set_contains(test_env(w), set, e2));
      check(!scene_set_contains(test_env(w), set, e3));
    }
  }

  it("can clear sets") {
    EcsWorld*         w   = world;
    const StringHash  set = string_hash_lit("test");
    const EcsEntityId e1  = ecs_world_entity_create(w);
    const EcsEntityId e2  = ecs_world_entity_create(w);
    const EcsEntityId e3  = ecs_world_entity_create(w);

    {
      scene_set_add(test_env(w), set, e1);
      scene_set_add(test_env(w), set, e2);
      scene_set_add(test_env(w), set, e3);
      ecs_run_sync(runner);

      check_eq_int(scene_set_count(test_env(w), set), 3);
      check_eq_int(scene_set_main(test_env(w), set), e1);
      check(scene_set_contains(test_env(w), set, e1));
      check(scene_set_contains(test_env(w), set, e2));
      check(scene_set_contains(test_env(w), set, e3));
    }

    {
      scene_set_clear(test_env(w), set);
      ecs_run_sync(runner);

      check_eq_int(scene_set_count(test_env(w), set), 0);
      check_eq_int(scene_set_main(test_env(w), set), 0);
      check(!scene_set_contains(test_env(w), set, e1));
      check(!scene_set_contains(test_env(w), set, e2));
      check(!scene_set_contains(test_env(w), set, e3));
    }
  }

  it("can add an entity to multiple sets") {
    EcsWorld*        w      = world;
    const StringHash sets[] = {
        string_hash_lit("testA"),
        string_hash_lit("testB"),
        string_hash_lit("testC"),
    };

    const EcsEntityId e1 = ecs_world_entity_create(w);

    array_for_t(sets, StringHash, setPtr) { scene_set_add(test_env(w), *setPtr, e1); }
    ecs_run_sync(runner);

    array_for_t(sets, StringHash, setPtr) {
      check_eq_int(scene_set_count(test_env(w), *setPtr), 1);
      check_eq_int(scene_set_main(test_env(w), *setPtr), e1);
      check_eq_int(*scene_set_begin(test_env(w), *setPtr), e1);
      check(scene_set_contains(test_env(w), *setPtr, e1));
    }
  }

  it("removes deleted entities from sets") {
    EcsWorld*        w   = world;
    const StringHash set = string_hash_lit("test");

    const EcsEntityId e1 = ecs_world_entity_create(w);
    {
      scene_set_add(test_env(w), set, e1);

      ecs_run_sync(runner);
      check_eq_int(scene_set_count(test_env(w), set), 1);
      check_eq_int(scene_set_main(test_env(w), set), e1);
      check(scene_set_contains(test_env(w), set, e1));
    }
    {
      ecs_world_entity_destroy(world, e1);
      ecs_run_sync(runner); // 1 run to flush the destroy.
      ecs_run_sync(runner); // 1 run to update the sets.

      check_eq_int(scene_set_count(test_env(w), set), 0);
      check_eq_int(scene_set_main(test_env(w), set), 0);
      check(!scene_set_contains(test_env(w), set, e1));
    }
  }

  it("removes entities from sets when removing the SetMember component") {
    EcsWorld*        w   = world;
    const StringHash set = string_hash_lit("test");

    const EcsEntityId e1 = ecs_world_entity_create(w);
    {
      scene_set_add(test_env(w), set, e1);

      ecs_run_sync(runner);
      check_eq_int(scene_set_count(test_env(w), set), 1);
      check_eq_int(scene_set_main(test_env(w), set), e1);
      check(scene_set_contains(test_env(w), set, e1));
    }
    {
      ecs_world_remove_t(world, e1, SceneSetMemberComp);
      ecs_run_sync(runner); // 1 run to flush the remove.
      ecs_run_sync(runner); // 1 run to update the sets.

      check_eq_int(scene_set_count(test_env(w), set), 0);
      check_eq_int(scene_set_main(test_env(w), set), 0);
      check(!scene_set_contains(test_env(w), set, e1));
    }
  }

  teardown() {
    ecs_runner_destroy(runner);
    ecs_world_destroy(world);
    ecs_def_destroy(def);
  }
}
