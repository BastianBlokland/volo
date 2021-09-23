#include "check_spec.h"
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

  teardown() {
    ecs_world_destroy(world);
    ecs_def_destroy(def);
  }
}
