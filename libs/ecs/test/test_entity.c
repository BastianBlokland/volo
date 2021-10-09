#include "check_spec.h"
#include "core_alloc.h"
#include "ecs_def.h"
#include "ecs_world.h"

spec(entity) {

  EcsDef*   def   = null;
  EcsWorld* world = null;

  setup() {
    def   = ecs_def_create(g_alloc_heap);
    world = ecs_world_create(g_alloc_heap, def);
  }

  it("returns 0 when comparing the same entity") {
    const EcsEntityId entity = ecs_world_entity_create(world);
    check_eq_int(ecs_compare_entity(&entity, &entity), 0);
  }

  it("returns -1 when comparing to an older entity") {
    const EcsEntityId entityA = ecs_world_entity_create(world);
    const EcsEntityId entityB = ecs_world_entity_create(world);
    check_eq_int(ecs_compare_entity(&entityA, &entityB), -1);
  }

  it("returns 1 when comparing to an newer entity") {
    const EcsEntityId entityA = ecs_world_entity_create(world);
    const EcsEntityId entityB = ecs_world_entity_create(world);
    check_eq_int(ecs_compare_entity(&entityB, &entityA), 1);
  }

  teardown() {
    ecs_world_destroy(world);
    ecs_def_destroy(def);
  }
}
