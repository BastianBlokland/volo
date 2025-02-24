#include "check_spec.h"
#include "core_alloc.h"
#include "core_bits.h"
#include "core_dynarray.h"
#include "ecs_def.h"
#include "ecs_world.h"

ecs_comp_define(WorldCompA) {
  u32  f1;
  bool f2;
};

ecs_comp_define(WorldCompB) { u32 f1; };

ecs_comp_define(WorldCompC) { String f1; };

ecs_comp_define(WorldCompAligned) {
  u32             a;
  ALIGNAS(64) u32 b;
};

ecs_comp_define(WorldCompEmpty);

ecs_module_init(world_test_module) {
  ecs_register_comp(WorldCompA);
  ecs_register_comp(WorldCompB);
  ecs_register_comp(WorldCompC);
  ecs_register_comp(WorldCompAligned);
  ecs_register_comp_empty(WorldCompEmpty);
}

spec(world) {

  EcsDef*   def   = null;
  EcsWorld* world = null;

  setup() {
    def = ecs_def_create(g_allocHeap);
    ecs_register_module(def, world_test_module);

    world = ecs_world_create(g_allocHeap, def);
  }

  it("stores the definition") { check(ecs_world_def(world) == def); }

  it("reports created entities as existing") {
    static const usize g_entitiesToCreate = 567;
    DynArray           entities           = dynarray_create_t(g_allocHeap, EcsEntityId, 2048);

    for (usize i = 0; i != g_entitiesToCreate; ++i) {
      *dynarray_push_t(&entities, EcsEntityId) = ecs_world_entity_create(world);
    }

    // Newly created entities exists before the first flush.
    dynarray_for_t(&entities, EcsEntityId, id) { check(ecs_world_exists(world, *id)); }

    ecs_world_flush(world);

    // Newly created entities still exists after the first flush.
    dynarray_for_t(&entities, EcsEntityId, id) { check(ecs_world_exists(world, *id)); }

    dynarray_destroy(&entities);
  }

  it("reports the global entity as existing") {
    check(ecs_world_exists(world, ecs_world_global(world)));
  }

  it("reports destroyed entities as existing until the next flush") {
    const EcsEntityId entity = ecs_world_entity_create(world);

    check(ecs_world_exists(world, entity)); // Exists before destroying,

    ecs_world_entity_destroy(world, entity);

    check(ecs_world_exists(world, entity)); // Still exists until the next flush.

    ecs_world_flush(world);

    check(!ecs_world_exists(world, entity)); // No longer exists.
  }

  it("reports reset entities as existing") {
    const EcsEntityId entity = ecs_world_entity_create(world);

    check(ecs_world_exists(world, entity)); // Exists before resetting,

    ecs_world_entity_reset(world, entity);

    check(ecs_world_exists(world, entity)); // Still exists after resetting.

    ecs_world_flush(world);

    check(ecs_world_exists(world, entity)); // Still exists after resetting and a flush.
  }

  it("zero initializes new components") {
    const EcsEntityId entity = ecs_world_entity_create(world);

    const WorldCompA* comp = ecs_world_add_t(world, entity, WorldCompA);
    check_eq_int(comp->f1, 0);
    check(!comp->f2);
  }

  it("zero initializes new components when providing empty initial mem") {
    const EcsEntityId entity = ecs_world_entity_create(world);

    const WorldCompA* comp = ecs_world_add(world, entity, ecs_comp_id(WorldCompA), mem_empty);
    check_eq_int(comp->f1, 0);
    check(!comp->f2);
  }

  it("respects the alignment for added components") {
    const EcsEntityId entity = ecs_world_entity_create(world);

    const WorldCompAligned* comp = ecs_world_add_t(world, entity, WorldCompAligned);
    check(bits_aligned_ptr(comp, 64));
  }

  it("can override component fields for new components") {
    const EcsEntityId entity = ecs_world_entity_create(world);

    const WorldCompA* comp = ecs_world_add_t(world, entity, WorldCompA, .f1 = 42, .f2 = true);
    check_eq_int(comp->f1, 42);
    check(comp->f2);
  }

  it("can add multiple components for the same entity") {
    const EcsEntityId entity = ecs_world_entity_create(world);

    WorldCompA* a = ecs_world_add_t(world, entity, WorldCompA, .f1 = 42, .f2 = true);
    WorldCompB* b = ecs_world_add_t(world, entity, WorldCompB, .f1 = 1337);
    WorldCompC* c = ecs_world_add_t(world, entity, WorldCompC, .f1 = string_lit("Hello"));

    check_eq_int(a->f1, 42);
    check(a->f2);
    check_eq_int(b->f1, 1337);
    check_eq_string(c->f1, string_lit("Hello"));
  }

  it("can add components for many entities") {
    static const usize g_entitiesToCreate = 567;
    DynArray           entities           = dynarray_create_t(g_allocHeap, EcsEntityId, 2048);

    for (usize i = 0; i != g_entitiesToCreate; ++i) {
      *dynarray_push_t(&entities, EcsEntityId) = ecs_world_entity_create(world);
    }

    dynarray_for_t(&entities, EcsEntityId, id) {
      const WorldCompA* comp = ecs_world_add_t(world, *id, WorldCompA, .f1 = 42, .f2 = true);
      check_eq_int(comp->f1, 42);
      check(comp->f2);
    }

    ecs_world_flush(world);

    dynarray_for_t(&entities, EcsEntityId, id) { check(ecs_world_has_t(world, *id, WorldCompA)); }

    dynarray_destroy(&entities);
  }

  it("can add empty components") {
    const EcsEntityId entity = ecs_world_entity_create(world);

    ecs_world_add_empty_t(world, entity, WorldCompEmpty);

    ecs_world_flush(world);

    check(ecs_world_has_t(world, entity, WorldCompEmpty));
  }

  it("can check for component existence") {
    const EcsEntityId entity = ecs_world_entity_create(world);

    check(!ecs_world_has_t(world, entity, WorldCompA));
    check(!ecs_world_has_t(world, entity, WorldCompB));
    check(!ecs_world_has_t(world, entity, WorldCompC));

    ecs_world_add_t(world, entity, WorldCompA);
    ecs_world_add_t(world, entity, WorldCompB);

    // Component addition is processed at the next flush.
    check(!ecs_world_has_t(world, entity, WorldCompA));
    check(!ecs_world_has_t(world, entity, WorldCompB));
    check(!ecs_world_has_t(world, entity, WorldCompC));

    ecs_world_flush(world);

    check(ecs_world_has_t(world, entity, WorldCompA));
    check(ecs_world_has_t(world, entity, WorldCompB));
    check(!ecs_world_has_t(world, entity, WorldCompC));

    ecs_world_remove_t(world, entity, WorldCompA);
    ecs_world_remove_t(world, entity, WorldCompB);

    ecs_world_flush(world);

    check(!ecs_world_has_t(world, entity, WorldCompA));
    check(!ecs_world_has_t(world, entity, WorldCompB));
    check(!ecs_world_has_t(world, entity, WorldCompC));
  }

  it("removes all components when resetting") {
    const EcsEntityId entity = ecs_world_entity_create(world);

    ecs_world_add_t(world, entity, WorldCompA);
    ecs_world_flush(world);

    check(ecs_world_has_t(world, entity, WorldCompA));

    ecs_world_entity_reset(world, entity);

    ecs_world_flush(world);
    check(!ecs_world_has_t(world, entity, WorldCompA));
  }

  it("removes queued additions when resetting") {
    const EcsEntityId entity = ecs_world_entity_create(world);

    ecs_world_add_t(world, entity, WorldCompA);
    ecs_world_flush(world);

    check(ecs_world_has_t(world, entity, WorldCompA));

    ecs_world_entity_reset(world, entity);
    ecs_world_add_t(world, entity, WorldCompA);
    ecs_world_add_t(world, entity, WorldCompB);

    ecs_world_flush(world);
    check(!ecs_world_has_t(world, entity, WorldCompA));
    check(!ecs_world_has_t(world, entity, WorldCompB));
  }

  it("supports queued removals when resetting") {
    const EcsEntityId entity = ecs_world_entity_create(world);

    ecs_world_add_t(world, entity, WorldCompA);
    ecs_world_flush(world);

    check(ecs_world_has_t(world, entity, WorldCompA));

    ecs_world_remove_t(world, entity, WorldCompA);
    ecs_world_entity_reset(world, entity);

    ecs_world_flush(world);
    check(!ecs_world_has_t(world, entity, WorldCompA));
  }

  it("supports duplicate additions for empty components") {
    const EcsEntityId entity = ecs_world_entity_create(world);

    ecs_world_add_empty_t(world, entity, WorldCompEmpty);
    ecs_world_add_empty_t(world, entity, WorldCompEmpty);

    ecs_world_flush(world);
    check(ecs_world_has_t(world, entity, WorldCompEmpty));

    ecs_world_add_empty_t(world, entity, WorldCompEmpty);
    ecs_world_add_empty_t(world, entity, WorldCompEmpty);

    ecs_world_flush(world);
    check(ecs_world_has_t(world, entity, WorldCompEmpty));
  }

  it("supports cancelling empty component removal") {
    const EcsEntityId entity = ecs_world_entity_create(world);

    ecs_world_add_empty_t(world, entity, WorldCompEmpty);

    ecs_world_flush(world);
    check(ecs_world_has_t(world, entity, WorldCompEmpty));

    // Order of add / remove does not matter, add wins.
    ecs_world_remove_t(world, entity, WorldCompEmpty);
    ecs_world_add_empty_t(world, entity, WorldCompEmpty);

    ecs_world_flush(world);
    check(ecs_world_has_t(world, entity, WorldCompEmpty));

    // Order of add / remove does not matter, add wins.
    ecs_world_add_empty_t(world, entity, WorldCompEmpty);
    ecs_world_remove_t(world, entity, WorldCompEmpty);

    ecs_world_flush(world);
    check(ecs_world_has_t(world, entity, WorldCompEmpty));
  }

  it("supports duplicate component removals") {
    const EcsEntityId entity = ecs_world_entity_create(world);

    ecs_world_add_empty_t(world, entity, WorldCompEmpty);
    ecs_world_flush(world);

    ecs_world_remove_t(world, entity, WorldCompEmpty);
    ecs_world_remove_t(world, entity, WorldCompEmpty);

    ecs_world_flush(world);
    check(!ecs_world_has_t(world, entity, WorldCompEmpty));
  }

  teardown() {
    ecs_world_destroy(world);
    ecs_def_destroy(def);
  }
}
