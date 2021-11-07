#include "check_spec.h"
#include "core_alloc.h"
#include "ecs_def.h"
#include "ecs_world.h"

ecs_comp_define(CombineCompA) { u64 state; };

ecs_view_define(ReadA) { ecs_access_read(CombineCompA); }

static void ecs_combine_compA(void* a, void* b) {
  CombineCompA* compA = a;
  CombineCompA* compB = b;
  compA->state += compB->state;
}

ecs_module_init(combine_test_module) {
  ecs_register_comp(CombineCompA, .combinator = ecs_combine_compA);

  ecs_register_view(ReadA);
}

spec(combinator) {

  EcsDef*   def   = null;
  EcsWorld* world = null;

  setup() {
    def = ecs_def_create(g_alloc_heap);
    ecs_register_module(def, combine_test_module);

    world = ecs_world_create(g_alloc_heap, def);
  }

  it("supports combining pending components") {
    const EcsEntityId entity = ecs_world_entity_create(world);

    ecs_world_add_t(world, entity, CombineCompA, .state = 42);
    ecs_world_add_t(world, entity, CombineCompA, .state = 1337);

    ecs_world_flush(world);

    EcsIterator* itr = ecs_view_itr_at(ecs_world_view_t(world, ReadA), entity);
    check_eq_int(ecs_view_read_t(itr, CombineCompA)->state, 1379);
  }

  it("supports combining many pending components") {
    const EcsEntityId entity = ecs_world_entity_create(world);

    static const usize compCount = 1337;
    for (usize i = 0; i != compCount; ++i) {
      ecs_world_add_t(world, entity, CombineCompA, .state = 2);
    }

    ecs_world_flush(world);

    EcsIterator* itr = ecs_view_itr_at(ecs_world_view_t(world, ReadA), entity);
    check_eq_int(ecs_view_read_t(itr, CombineCompA)->state, compCount * 2);
  }

  it("supports combining a pending component with an existing component") {
    const EcsEntityId entity = ecs_world_entity_create(world);

    ecs_world_add_t(world, entity, CombineCompA, .state = 42);

    ecs_world_flush(world);
    EcsIterator* itrA = ecs_view_itr_at(ecs_world_view_t(world, ReadA), entity);
    check_eq_int(ecs_view_read_t(itrA, CombineCompA)->state, 42);

    ecs_world_add_t(world, entity, CombineCompA, .state = 1337);

    ecs_world_flush(world);
    EcsIterator* itrB = ecs_view_itr_at(ecs_world_view_t(world, ReadA), entity);
    check_eq_int(ecs_view_read_t(itrB, CombineCompA)->state, 1379);
  }

  teardown() {
    ecs_world_destroy(world);
    ecs_def_destroy(def);
  }
}
