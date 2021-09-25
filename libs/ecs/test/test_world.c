#include "check_spec.h"
#include "core_alloc.h"
#include "ecs_def.h"
#include "ecs_world.h"

ecs_module_init(world_test_module) {}

spec(world) {

  EcsDef*   def   = null;
  EcsWorld* world = null;

  setup() {
    def = ecs_def_create(g_alloc_heap);
    ecs_register_module(def, def_test_module);

    world = ecs_world_create(g_alloc_heap, def);
  }

  it("stores the definition") { check(ecs_world_def(world) == def); }

  it("can create a new entity") {
    const EcsEntityId entity = ecs_world_entity_create(world);
    check(ecs_world_entity_exists(world, entity));
  }

  teardown() {
    ecs_world_destroy(world);
    ecs_def_destroy(def);
  }
}
