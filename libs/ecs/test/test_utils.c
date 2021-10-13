#include "check_spec.h"
#include "core_alloc.h"
#include "ecs_def.h"
#include "ecs_utils.h"
#include "ecs_view.h"
#include "ecs_world.h"

ecs_comp_define(UtilsCompA) {
  u32 f1;
  u64 f2;
};

ecs_comp_define(UtilsCompB) { u64 f1; };

ecs_view_define(MaybeReadA) { ecs_access_maybe_read(UtilsCompA); }
ecs_view_define(MaybeWriteA) { ecs_access_maybe_write(UtilsCompA); }

ecs_module_init(utils_test_module) {
  ecs_register_comp(UtilsCompA);
  ecs_register_comp(UtilsCompB);

  ecs_register_view(MaybeReadA);
  ecs_register_view(MaybeWriteA);
}

spec(utils) {

  EcsDef*   def   = null;
  EcsWorld* world = null;

  setup() {
    def = ecs_def_create(g_alloc_heap);
    ecs_register_module(def, utils_test_module);

    world = ecs_world_create(g_alloc_heap, def);
  }

  it("can read or add a component from a maybe-read iterator") {
    const EcsEntityId entity1 = ecs_world_entity_create(world);
    const EcsEntityId entity2 = ecs_world_entity_create(world);

    ecs_world_add_t(world, entity1, UtilsCompA, .f1 = 42, .f2 = 1337);
    ecs_world_add_t(world, entity2, UtilsCompB, .f1 = 1337);

    ecs_world_flush(world);

    EcsIterator* itr = ecs_view_itr(ecs_world_view_t(world, MaybeReadA));

    ecs_view_jump(itr, entity1);
    const UtilsCompA* comp1 = ecs_utils_read_or_add_t(world, itr, UtilsCompA);
    check_eq_int(comp1->f1, 42);
    check_eq_int(comp1->f2, 1337);

    ecs_view_jump(itr, entity2);
    const UtilsCompA* comp2 = ecs_utils_read_or_add_t(world, itr, UtilsCompA);
    check_eq_int(comp2->f1, 0);
    check_eq_int(comp2->f2, 0);
  }

  it("can write or add a component from a maybe-write iterator") {
    const EcsEntityId entity1 = ecs_world_entity_create(world);
    const EcsEntityId entity2 = ecs_world_entity_create(world);

    ecs_world_add_t(world, entity1, UtilsCompA, .f1 = 42, .f2 = 1337);
    ecs_world_add_t(world, entity2, UtilsCompB, .f1 = 1337);

    ecs_world_flush(world);

    EcsIterator* itr = ecs_view_itr(ecs_world_view_t(world, MaybeWriteA));

    ecs_view_jump(itr, entity1);
    UtilsCompA* comp1 = ecs_utils_write_or_add_t(world, itr, UtilsCompA);
    check_eq_int(comp1->f1, 42);
    check_eq_int(comp1->f2, 1337);

    ecs_view_jump(itr, entity2);
    UtilsCompA* comp2 = ecs_utils_write_or_add_t(world, itr, UtilsCompA);
    check_eq_int(comp2->f1, 0);
    check_eq_int(comp2->f2, 0);
  }

  it("can optionally add a component") {
    const EcsEntityId entity1 = ecs_world_entity_create(world);
    const EcsEntityId entity2 = ecs_world_entity_create(world);

    ecs_world_add_t(world, entity1, UtilsCompA);

    ecs_world_flush(world);

    check(!ecs_utils_maybe_add_t(world, entity1, UtilsCompA));
    check(ecs_utils_maybe_add_t(world, entity2, UtilsCompA));

    ecs_world_flush(world);

    check(ecs_world_has_t(world, entity1, UtilsCompA));
    check(ecs_world_has_t(world, entity2, UtilsCompA));
  }

  it("can optionally remove a component") {
    const EcsEntityId entity1 = ecs_world_entity_create(world);
    const EcsEntityId entity2 = ecs_world_entity_create(world);

    ecs_world_add_t(world, entity1, UtilsCompA);

    ecs_world_flush(world);

    check(ecs_utils_maybe_remove_t(world, entity1, UtilsCompA));
    check(!ecs_utils_maybe_remove_t(world, entity2, UtilsCompA));

    ecs_world_flush(world);

    check(!ecs_world_has_t(world, entity1, UtilsCompA));
    check(!ecs_world_has_t(world, entity2, UtilsCompA));
  }

  teardown() {
    ecs_world_destroy(world);
    ecs_def_destroy(def);
  }
}
