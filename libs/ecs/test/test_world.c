#include "check_spec.h"
#include "core_alloc.h"
#include "ecs_def.h"
#include "ecs_runner.h"
#include "ecs_world.h"

ecs_module_init(world_test_module) {}

spec(world) {

  EcsDef*    def    = null;
  EcsWorld*  world  = null;
  EcsRunner* runner = null;

  setup() {
    def = ecs_def_create(g_alloc_heap);
    ecs_register_module(def, def_test_module);

    world  = ecs_world_create(g_alloc_heap, def);
    runner = ecs_runner_create(g_alloc_heap, world);
  }

  it("stores the definition") { check(ecs_world_def(world) == def); }

  it("can create entities") {
    static const usize entitiesToAllocate = 1234;
    DynArray           entities           = dynarray_create_t(g_alloc_heap, EcsEntityId, 2048);

    for (usize i = 0; i != entitiesToAllocate; ++i) {
      *dynarray_push_t(&entities, EcsEntityId) = ecs_world_entity_create(world);
    }

    dynarray_for_t(&entities, EcsEntityId, id, { check(ecs_world_entity_exists(world, *id)); });

    ecs_run_sync(runner); // Causes a flush to happen in the world.

    dynarray_for_t(&entities, EcsEntityId, id, { check(ecs_world_entity_exists(world, *id)); });

    dynarray_destroy(&entities);
  }

  teardown() {
    ecs_runner_destroy(runner);
    ecs_world_destroy(world);
    ecs_def_destroy(def);
  }
}
