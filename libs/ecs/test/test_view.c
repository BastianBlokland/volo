#include "check_spec.h"
#include "core_alloc.h"
#include "ecs_def.h"
#include "ecs_world.h"

ecs_comp_define(ViewCompA) { u32 f1; };
ecs_comp_define(ViewCompB) { String f1; };
ecs_comp_define(ViewCompC) { ALIGNAS(64) u32 f1; };

ecs_view_define(ReadAB) {
  ecs_access_read(ViewCompA);
  ecs_access_read(ViewCompB);
}

ecs_view_define(WriteC) { ecs_access_write(ViewCompC); }

ecs_view_define(ReadAMaybeC) {
  ecs_access_read(ViewCompA);
  ecs_access_maybe_read(ViewCompC);
}

ecs_view_define(ReadMaybeAMaybeBMaybeC) {
  ecs_access_maybe_read(ViewCompA);
  ecs_access_maybe_read(ViewCompB);
  ecs_access_maybe_read(ViewCompC);
}

ecs_module_init(view_test_module) {
  ecs_register_comp(ViewCompA);
  ecs_register_comp(ViewCompB);
  ecs_register_comp(ViewCompC);

  ecs_register_view(ReadAB);
  ecs_register_view(WriteC);
  ecs_register_view(ReadAMaybeC);
  ecs_register_view(ReadMaybeAMaybeBMaybeC);
}

spec(view) {

  EcsDef*   def   = null;
  EcsWorld* world = null;

  setup() {
    def = ecs_def_create(g_alloc_heap);
    ecs_register_module(def, view_test_module);

    world = ecs_world_create(g_alloc_heap, def);
  }

  it("can return the count of components it can read") {
    EcsView* view = null;

    view = ecs_world_view_t(world, ReadAB);
    check_eq_int(ecs_view_comp_count(view), 2);

    view = ecs_world_view_t(world, WriteC);
    check_eq_int(ecs_view_comp_count(view), 1);

    view = ecs_world_view_t(world, ReadAMaybeC);
    check_eq_int(ecs_view_comp_count(view), 2);

    check_eq_int(ecs_view_chunks(view), 0);
  }

  it("can check if an entity is contained in the view") {
    const EcsEntityId entity1 = ecs_world_entity_create(world);
    const EcsEntityId entity2 = ecs_world_entity_create(world);
    const EcsEntityId entity3 = ecs_world_entity_create(world);

    ecs_world_add_t(world, entity1, ViewCompA);
    ecs_world_add_t(world, entity1, ViewCompB);

    ecs_world_add_t(world, entity2, ViewCompA);
    ecs_world_add_t(world, entity2, ViewCompC);

    ecs_world_add_t(world, entity3, ViewCompA);
    ecs_world_add_t(world, entity3, ViewCompB);
    ecs_world_add_t(world, entity3, ViewCompC);

    ecs_world_flush(world);

    EcsView* view = ecs_world_view_t(world, ReadAB);
    check(ecs_view_contains(view, entity1));
    check(!ecs_view_contains(view, entity2));
    check(ecs_view_contains(view, entity3));
    check_eq_int(ecs_view_chunks(view), 2);
  }

  it("can read component values on entities") {
    const EcsEntityId entity = ecs_world_entity_create(world);

    ecs_world_add_t(world, entity, ViewCompA, .f1 = 42);
    ecs_world_add_t(world, entity, ViewCompB, .f1 = string_lit("Hello World"));
    ecs_world_add_t(world, entity, ViewCompC, .f1 = 1337);

    ecs_world_flush(world);

    EcsIterator* itr = ecs_view_at(ecs_world_view_t(world, ReadAB), entity);

    check(ecs_view_entity(itr) == entity);
    check_eq_int(ecs_view_read_t(itr, ViewCompA)->f1, 42);
    check_eq_string(ecs_view_read_t(itr, ViewCompB)->f1, string_lit("Hello World"));
  }

  it("can optionally read component values on entities using maybe-read") {
    const EcsEntityId entityA = ecs_world_entity_create(world);
    const EcsEntityId entityB = ecs_world_entity_create(world);

    ecs_world_add_t(world, entityA, ViewCompA, .f1 = 42);
    ecs_world_add_t(world, entityA, ViewCompC, .f1 = 1337);

    ecs_world_add_t(world, entityB, ViewCompA, .f1 = 42);
    ecs_world_add_t(world, entityB, ViewCompB, .f1 = string_lit("Hello World"));

    ecs_world_flush(world);

    EcsIterator* itr = ecs_view_itr(ecs_world_view_t(world, ReadAMaybeC));

    ecs_view_jump(itr, entityA);
    check(ecs_view_entity(itr) == entityA);
    check_eq_int(ecs_view_read_t(itr, ViewCompA)->f1, 42);

    ecs_view_jump(itr, entityB);
    check(ecs_view_entity(itr) == entityB);
    check(ecs_view_read_t(itr, ViewCompC) == null);
  }

  it("can optionally jump to entities that exist in the view") {
    const EcsEntityId entityA = ecs_world_entity_create(world);
    const EcsEntityId entityB = ecs_world_entity_create(world);

    ecs_world_add_t(world, entityA, ViewCompA, .f1 = 42);
    ecs_world_add_t(world, entityA, ViewCompC, .f1 = 1337);

    ecs_world_add_t(world, entityB, ViewCompA, .f1 = 42);
    ecs_world_add_t(world, entityB, ViewCompB, .f1 = string_lit("Hello World"));

    ecs_world_flush(world);

    EcsIterator* itrA = ecs_view_maybe_at(ecs_world_view_t(world, ReadAB), entityA);
    EcsIterator* itrB = ecs_view_maybe_at(ecs_world_view_t(world, ReadAB), entityB);

    check_require(itrA == null);
    check_require(itrB != null);
    check_eq_int(ecs_view_read_t(itrB, ViewCompA)->f1, 42);
    check_eq_string(ecs_view_read_t(itrB, ViewCompB)->f1, string_lit("Hello World"));
  }

  it("matches all entities that are in an archetype when defining only maybe-reads") {
    ecs_world_entity_create(world); // No component on it, so not in an archetype.
    const EcsEntityId entityA = ecs_world_entity_create(world);
    const EcsEntityId entityB = ecs_world_entity_create(world);
    const EcsEntityId entityC = ecs_world_entity_create(world);
    ecs_world_entity_create(world); // No component on it, so not in an archetype.

    ecs_world_add_t(world, entityA, ViewCompA, .f1 = 1337);
    ecs_world_add_t(world, entityA, ViewCompB, .f1 = string_lit("Hello World"));
    ecs_world_add_t(world, entityA, ViewCompC, .f1 = 42);

    ecs_world_add_t(world, entityB, ViewCompA, .f1 = 1337);
    ecs_world_add_t(world, entityB, ViewCompB, .f1 = string_lit("Hello World"));

    ecs_world_add_t(world, entityC, ViewCompC, .f1 = 42);

    ecs_world_flush(world);

    EcsIterator* itr = ecs_view_itr(ecs_world_view_t(world, ReadMaybeAMaybeBMaybeC));
    check_require(ecs_view_walk(itr) && ecs_view_entity(itr) == entityA);
    check(ecs_view_read_t(itr, ViewCompA) != null);
    check(ecs_view_read_t(itr, ViewCompB) != null);
    check(ecs_view_read_t(itr, ViewCompC) != null);

    check_require(ecs_view_walk(itr) && ecs_view_entity(itr) == entityB);
    check(ecs_view_read_t(itr, ViewCompA) != null);
    check(ecs_view_read_t(itr, ViewCompB) != null);
    check(ecs_view_read_t(itr, ViewCompC) == null);

    check_require(ecs_view_walk(itr) && ecs_view_entity(itr) == entityC);
    check(ecs_view_read_t(itr, ViewCompA) == null);
    check(ecs_view_read_t(itr, ViewCompB) == null);
    check(ecs_view_read_t(itr, ViewCompC) != null);

    check(!ecs_view_walk(itr));
  }

  it("can write component values on entities") {
    const EcsEntityId entity = ecs_world_entity_create(world);

    ecs_world_add_t(world, entity, ViewCompC, .f1 = 1337);

    ecs_world_flush(world);

    EcsIterator* itr = ecs_view_at(ecs_world_view_t(world, WriteC), entity);

    ViewCompC* comp = ecs_view_write_t(itr, ViewCompC);

    check(ecs_view_entity(itr) == entity);
    check_eq_int(comp->f1, 1337);
    comp->f1 = 42;
  }

  it("can iterate over entities with required components from different archetypes") {
    const EcsEntityId entityA = ecs_world_entity_create(world);
    const EcsEntityId entityB = ecs_world_entity_create(world);
    const EcsEntityId entityC = ecs_world_entity_create(world);
    const EcsEntityId entityD = ecs_world_entity_create(world);
    const EcsEntityId entityE = ecs_world_entity_create(world);
    const EcsEntityId entityF = ecs_world_entity_create(world);

    ecs_world_add_t(world, entityA, ViewCompA, .f1 = 1337);
    ecs_world_add_t(world, entityA, ViewCompB, .f1 = string_lit("Hello World"));
    ecs_world_add_t(world, entityA, ViewCompC, .f1 = 1337);

    ecs_world_add_t(world, entityB, ViewCompA, .f1 = 1337);
    ecs_world_add_t(world, entityB, ViewCompB, .f1 = string_lit("Hello World"));

    ecs_world_add_t(world, entityC, ViewCompA, .f1 = 1337);
    ecs_world_add_t(world, entityC, ViewCompC, .f1 = 1337);

    ecs_world_add_t(world, entityD, ViewCompA, .f1 = 1337);
    ecs_world_add_t(world, entityD, ViewCompB, .f1 = string_lit("Hello World"));

    ecs_world_add_t(world, entityE, ViewCompA, .f1 = 1337);
    ecs_world_add_t(world, entityE, ViewCompB, .f1 = string_lit("Hello World"));

    ecs_world_add_t(world, entityF, ViewCompB, .f1 = string_lit("Hello World"));
    ecs_world_add_t(world, entityF, ViewCompC, .f1 = 1337);

    ecs_world_flush(world);

    EcsIterator* itr = ecs_view_itr(ecs_world_view_t(world, ReadAB));
    check_require(ecs_view_walk(itr) && ecs_view_entity(itr) == entityA);
    check_require(ecs_view_walk(itr) && ecs_view_entity(itr) == entityB);
    check_require(ecs_view_walk(itr) && ecs_view_entity(itr) == entityD);
    check_require(ecs_view_walk(itr) && ecs_view_entity(itr) == entityE);
    check(!ecs_view_walk(itr));
  }

  it("can iterate over entities from multiple chunks in an archetype") {
    static const usize g_entitiesToCreate = 2000;
    DynArray           entities = dynarray_create_t(g_alloc_heap, EcsEntityId, g_entitiesToCreate);

    for (usize i = 0; i != g_entitiesToCreate; ++i) {
      const EcsEntityId newEntity = ecs_world_entity_create(world);
      ecs_world_add_t(world, newEntity, ViewCompA, .f1 = (u32)i);
      ecs_world_add_t(world, newEntity, ViewCompB, .f1 = string_lit("Hello World"));
      *dynarray_push_t(&entities, EcsEntityId) = newEntity;
    }

    ecs_world_flush(world);

    EcsView* view = ecs_world_view_t(world, ReadAB);
    check(ecs_view_chunks(view) > 1);

    usize count = 0;
    for (EcsIterator* itr = ecs_view_itr(view); ecs_view_walk(itr); ++count) {
      check(ecs_view_entity(itr) == *dynarray_at_t(&entities, count, EcsEntityId));
      check(ecs_view_contains(view, ecs_view_entity(itr)));
      check_eq_int(ecs_view_read_t(itr, ViewCompA)->f1, count);
      check_eq_string(ecs_view_read_t(itr, ViewCompB)->f1, string_lit("Hello World"));
    }
    check_eq_int(count, g_entitiesToCreate);

    dynarray_destroy(&entities);
  }

  it("can iterate over entities which are missing a component using a maybe-read") {
    const EcsEntityId entityA = ecs_world_entity_create(world);
    const EcsEntityId entityB = ecs_world_entity_create(world);
    const EcsEntityId entityC = ecs_world_entity_create(world);
    const EcsEntityId entityD = ecs_world_entity_create(world);

    ecs_world_add_t(world, entityA, ViewCompA, .f1 = 1337);
    ecs_world_add_t(world, entityA, ViewCompC, .f1 = 42);

    ecs_world_add_t(world, entityB, ViewCompA, .f1 = 1338);
    ecs_world_add_t(world, entityB, ViewCompB);

    ecs_world_add_t(world, entityC, ViewCompA, .f1 = 1339);

    ecs_world_add_t(world, entityD, ViewCompC, .f1 = 1340);

    ecs_world_flush(world);

    EcsIterator* itr = ecs_view_itr(ecs_world_view_t(world, ReadAMaybeC));
    check_require(ecs_view_walk(itr) && ecs_view_entity(itr) == entityA);
    check_eq_int(ecs_view_read_t(itr, ViewCompA)->f1, 1337);
    check_eq_int(ecs_view_read_t(itr, ViewCompC)->f1, 42);

    check_require(ecs_view_walk(itr) && ecs_view_entity(itr) == entityB);
    check_eq_int(ecs_view_read_t(itr, ViewCompA)->f1, 1338);
    check(ecs_view_read_t(itr, ViewCompC) == null);

    check_require(ecs_view_walk(itr) && ecs_view_entity(itr) == entityC);
    check_eq_int(ecs_view_read_t(itr, ViewCompA)->f1, 1339);
    check(ecs_view_read_t(itr, ViewCompC) == null);

    check(!ecs_view_walk(itr));
  }

  it("skips empty archetypes") {
    static const usize g_entitiesToCreate = 567;
    DynArray           entities = dynarray_create_t(g_alloc_heap, EcsEntityId, g_entitiesToCreate);

    for (usize i = 0; i != g_entitiesToCreate; ++i) {
      const EcsEntityId newEntity = ecs_world_entity_create(world);
      ecs_world_add_t(world, newEntity, ViewCompA, .f1 = (u32)i);
      ecs_world_add_t(world, newEntity, ViewCompB, .f1 = string_lit("Hello World"));
      *dynarray_push_t(&entities, EcsEntityId) = newEntity;
    }

    ecs_world_flush(world);

    dynarray_for_t(&entities, EcsEntityId, entity) { ecs_world_entity_destroy(world, *entity); }

    ecs_world_flush(world);

    EcsIterator* itr = ecs_view_itr(ecs_world_view_t(world, ReadAB));
    check_eq_int(ecs_view_chunks(ecs_world_view_t(world, ReadAB)), 0);
    check(!ecs_view_walk(itr));

    dynarray_destroy(&entities);
  }

  it("can iterate over entities in multiple steps") {
    static const usize g_entitiesToCreate = 2000;
    static const usize g_steps            = 42;
    DynArray           entities = dynarray_create_t(g_alloc_heap, EcsEntityId, g_entitiesToCreate);

    for (usize i = 0; i != g_entitiesToCreate; ++i) {
      const EcsEntityId newEntity = ecs_world_entity_create(world);
      ecs_world_add_t(world, newEntity, ViewCompA, .f1 = (u32)i);
      ecs_world_add_t(world, newEntity, ViewCompB, .f1 = string_lit("Hello World"));
      *dynarray_push_t(&entities, EcsEntityId) = newEntity;
    }

    ecs_world_flush(world);

    EcsView* view = ecs_world_view_t(world, ReadAB);
    check(ecs_view_chunks(view) > 1);

    usize count = 0;
    for (u32 step = 0; step != g_steps; ++step) {
      for (EcsIterator* itr = ecs_view_itr_step(view, g_steps, step); ecs_view_walk(itr); ++count) {
        check(ecs_view_entity(itr) == *dynarray_at_t(&entities, count, EcsEntityId));
        check(ecs_view_contains(view, ecs_view_entity(itr)));
        check_eq_int(ecs_view_read_t(itr, ViewCompA)->f1, count);
        check_eq_string(ecs_view_read_t(itr, ViewCompB)->f1, string_lit("Hello World"));
      }
    }
    check_eq_int(count, g_entitiesToCreate);

    dynarray_destroy(&entities);
  }

  it("can iterate over all entities with a 1 step stepped iterator") {
    static const usize g_entitiesToCreate = 2000;
    DynArray           entities = dynarray_create_t(g_alloc_heap, EcsEntityId, g_entitiesToCreate);

    for (usize i = 0; i != g_entitiesToCreate; ++i) {
      const EcsEntityId newEntity = ecs_world_entity_create(world);
      ecs_world_add_t(world, newEntity, ViewCompA, .f1 = (u32)i);
      ecs_world_add_t(world, newEntity, ViewCompB, .f1 = string_lit("Hello World"));
      *dynarray_push_t(&entities, EcsEntityId) = newEntity;
    }

    ecs_world_flush(world);

    EcsView* view = ecs_world_view_t(world, ReadAB);
    check(ecs_view_chunks(view) > 1);

    usize count = 0;
    for (EcsIterator* itr = ecs_view_itr_step(view, 1, 0); ecs_view_walk(itr); ++count) {
      check(ecs_view_entity(itr) == *dynarray_at_t(&entities, count, EcsEntityId));
      check(ecs_view_contains(view, ecs_view_entity(itr)));
      check_eq_int(ecs_view_read_t(itr, ViewCompA)->f1, count);
      check_eq_string(ecs_view_read_t(itr, ViewCompB)->f1, string_lit("Hello World"));
    }
    check_eq_int(count, g_entitiesToCreate);

    dynarray_destroy(&entities);
  }

  teardown() {
    ecs_world_destroy(world);
    ecs_def_destroy(def);
  }
}
