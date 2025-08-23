#include "check/spec.h"
#include "core/alloc.h"
#include "core/bits.h"
#include "core/dynarray.h"
#include "ecs/def.h"
#include "ecs/view.h"
#include "ecs/world.h"

ecs_comp_define(StorageCompA) { u32 f1; };
ecs_comp_define(StorageCompB) { u32 f1, f2; };
ecs_comp_define(StorageCompC) { u32 f1, f2, f3; };
ecs_comp_define(StorageCompD) { u32 f1, f2, f3, f4; };
ecs_comp_define(StorageCompE) { ALIGNAS(64) u32 f1; };
ecs_comp_define(StorageCompF);
ecs_comp_define(StorageCompG);
ecs_comp_define(StorageCompH);

ecs_view_define(ReadABC) {
  ecs_access_read(StorageCompA);
  ecs_access_read(StorageCompB);
  ecs_access_read(StorageCompC);
}

ecs_view_define(ReadABCD) {
  ecs_access_read(StorageCompA);
  ecs_access_read(StorageCompB);
  ecs_access_read(StorageCompC);
  ecs_access_read(StorageCompD);
}

ecs_view_define(ReadABE) {
  ecs_access_read(StorageCompA);
  ecs_access_read(StorageCompB);
  ecs_access_read(StorageCompE);
}

ecs_view_define(ReadFG) {
  ecs_access_read(StorageCompF);
  ecs_access_read(StorageCompG);
}

ecs_module_init(storage_test_module) {
  ecs_register_comp(StorageCompA);
  ecs_register_comp(StorageCompB);
  ecs_register_comp(StorageCompC);
  ecs_register_comp(StorageCompD);
  ecs_register_comp(StorageCompE);
  ecs_register_comp_empty(StorageCompF);
  ecs_register_comp_empty(StorageCompG);
  ecs_register_comp_empty(StorageCompH);

  ecs_register_view(ReadABC);
  ecs_register_view(ReadABCD);
  ecs_register_view(ReadABE);
  ecs_register_view(ReadFG);
}

spec(storage) {

  EcsDef*   def   = null;
  EcsWorld* world = null;

  setup() {
    def = ecs_def_create(g_allocHeap);
    ecs_register_module(def, storage_test_module);

    world = ecs_world_create(g_allocHeap, def);
  }

  it("copies added components into the entities archetype") {
    const EcsEntityId entity = ecs_world_entity_create(world);

    ecs_world_add_t(world, entity, StorageCompA, .f1 = 1);
    ecs_world_add_t(world, entity, StorageCompB, .f1 = 2, .f2 = 3);
    ecs_world_add_t(world, entity, StorageCompC, .f1 = 4, .f2 = 5, .f3 = 6);

    ecs_world_flush(world);

    EcsIterator* itr = ecs_view_at(ecs_world_view_t(world, ReadABC), entity);

    check_eq_int(ecs_view_read_t(itr, StorageCompA)->f1, 1);

    check_eq_int(ecs_view_read_t(itr, StorageCompB)->f1, 2);
    check_eq_int(ecs_view_read_t(itr, StorageCompB)->f2, 3);

    check_eq_int(ecs_view_read_t(itr, StorageCompC)->f1, 4);
    check_eq_int(ecs_view_read_t(itr, StorageCompC)->f2, 5);
    check_eq_int(ecs_view_read_t(itr, StorageCompC)->f3, 6);
  }

  it("respects component alignment") {
    const EcsEntityId entity = ecs_world_entity_create(world);

    ecs_world_add_t(world, entity, StorageCompA, .f1 = 1);
    ecs_world_add_t(world, entity, StorageCompE, .f1 = 2);
    ecs_world_add_t(world, entity, StorageCompB, .f1 = 3, .f2 = 4);

    ecs_world_flush(world);

    EcsIterator* itr = ecs_view_at(ecs_world_view_t(world, ReadABE), entity);

    check(bits_aligned_ptr(ecs_view_read_t(itr, StorageCompA), alignof(StorageCompA)));
    check(bits_aligned_ptr(ecs_view_read_t(itr, StorageCompE), alignof(StorageCompE)));
    check(bits_aligned_ptr(ecs_view_read_t(itr, StorageCompB), alignof(StorageCompB)));
  }

  it("moves component data when moving entities between archetypes") {
    const EcsEntityId entity = ecs_world_entity_create(world);

    ecs_world_add_t(world, entity, StorageCompB, .f1 = 1, .f2 = 2);
    ecs_world_add_t(world, entity, StorageCompC, .f1 = 3, .f2 = 4, .f3 = 5);

    ecs_world_flush(world);

    ecs_world_add_t(world, entity, StorageCompA, .f1 = 6);
    ecs_world_add_t(world, entity, StorageCompD, .f1 = 7, .f2 = 8, .f3 = 9, .f4 = 10);

    ecs_world_flush(world);

    EcsIterator* itr = ecs_view_at(ecs_world_view_t(world, ReadABCD), entity);

    check(ecs_view_entity(itr) == entity);

    check_eq_int(ecs_view_read_t(itr, StorageCompA)->f1, 6);

    check_eq_int(ecs_view_read_t(itr, StorageCompB)->f1, 1);
    check_eq_int(ecs_view_read_t(itr, StorageCompB)->f2, 2);

    check_eq_int(ecs_view_read_t(itr, StorageCompC)->f1, 3);
    check_eq_int(ecs_view_read_t(itr, StorageCompC)->f2, 4);
    check_eq_int(ecs_view_read_t(itr, StorageCompC)->f3, 5);

    check_eq_int(ecs_view_read_t(itr, StorageCompD)->f1, 7);
    check_eq_int(ecs_view_read_t(itr, StorageCompD)->f2, 8);
    check_eq_int(ecs_view_read_t(itr, StorageCompD)->f3, 9);
    check_eq_int(ecs_view_read_t(itr, StorageCompD)->f4, 10);
  }

  it("moves entity metadata when moving entities between archetypes") {
    const EcsEntityId entity = ecs_world_entity_create(world);

    ecs_world_add_t(world, entity, StorageCompA, .f1 = 1);
    ecs_world_add_t(world, entity, StorageCompB, .f1 = 2, .f2 = 3);
    ecs_world_add_t(world, entity, StorageCompC, .f1 = 4, .f2 = 5, .f3 = 6);

    ecs_world_flush(world);

    ecs_world_add_t(world, entity, StorageCompD, .f1 = 7, .f2 = 8, .f3 = 9, .f4 = 10);

    ecs_world_flush(world);

    EcsIterator* itr = ecs_view_itr(ecs_world_view_t(world, ReadABC));
    check(ecs_view_walk(itr));

    check(ecs_view_entity(itr) == entity);

    check_eq_int(ecs_view_read_t(itr, StorageCompA)->f1, 1);

    check_eq_int(ecs_view_read_t(itr, StorageCompB)->f1, 2);
    check_eq_int(ecs_view_read_t(itr, StorageCompB)->f2, 3);

    check_eq_int(ecs_view_read_t(itr, StorageCompC)->f1, 4);
    check_eq_int(ecs_view_read_t(itr, StorageCompC)->f2, 5);
    check_eq_int(ecs_view_read_t(itr, StorageCompC)->f3, 6);

    check(!ecs_view_walk(itr));
  }

  it("can move entities out of an archetype") {
    const EcsEntityId entity = ecs_world_entity_create(world);

    check(!ecs_world_has_t(world, entity, StorageCompA));

    ecs_world_add_t(world, entity, StorageCompA, .f1 = 1);
    ecs_world_add_t(world, entity, StorageCompB, .f1 = 2, .f2 = 3);
    ecs_world_add_t(world, entity, StorageCompC, .f1 = 4, .f2 = 5, .f3 = 6);

    ecs_world_flush(world);

    check(ecs_world_has_t(world, entity, StorageCompA));

    EcsIterator* itr = ecs_view_itr(ecs_world_view_t(world, ReadABC));
    check(ecs_view_walk(itr));

    ecs_world_remove_t(world, entity, StorageCompA);
    ecs_world_remove_t(world, entity, StorageCompB);
    ecs_world_remove_t(world, entity, StorageCompC);

    ecs_world_flush(world);

    check(!ecs_world_has_t(world, entity, StorageCompA));

    ecs_view_itr_reset(itr);
    check(!ecs_view_walk(itr));
  }

  it("can move new entities into an existing archetype") {
    const EcsEntityId entityA = ecs_world_entity_create(world);

    ecs_world_add_t(world, entityA, StorageCompA, .f1 = 1);
    ecs_world_add_t(world, entityA, StorageCompB, .f1 = 2, .f2 = 3);
    ecs_world_add_t(world, entityA, StorageCompC, .f1 = 4, .f2 = 5, .f3 = 6);

    ecs_world_flush(world);

    const EcsEntityId entityB = ecs_world_entity_create(world);

    ecs_world_add_t(world, entityB, StorageCompA, .f1 = 7);
    ecs_world_add_t(world, entityB, StorageCompB, .f1 = 8, .f2 = 9);
    ecs_world_add_t(world, entityB, StorageCompC, .f1 = 10, .f2 = 11, .f3 = 12);

    ecs_world_flush(world);

    EcsIterator* itr = ecs_view_itr(ecs_world_view_t(world, ReadABC));
    check(ecs_view_walk(itr));
    check(ecs_view_entity(itr) == entityA);
    check_eq_int(ecs_view_read_t(itr, StorageCompA)->f1, 1);
    check_eq_int(ecs_view_read_t(itr, StorageCompB)->f1, 2);
    check_eq_int(ecs_view_read_t(itr, StorageCompB)->f2, 3);
    check_eq_int(ecs_view_read_t(itr, StorageCompC)->f1, 4);
    check_eq_int(ecs_view_read_t(itr, StorageCompC)->f2, 5);
    check_eq_int(ecs_view_read_t(itr, StorageCompC)->f3, 6);

    check(ecs_view_walk(itr));
    check(ecs_view_entity(itr) == entityB);
    check_eq_int(ecs_view_read_t(itr, StorageCompA)->f1, 7);
    check_eq_int(ecs_view_read_t(itr, StorageCompB)->f1, 8);
    check_eq_int(ecs_view_read_t(itr, StorageCompB)->f2, 9);
    check_eq_int(ecs_view_read_t(itr, StorageCompC)->f1, 10);
    check_eq_int(ecs_view_read_t(itr, StorageCompC)->f2, 11);
    check_eq_int(ecs_view_read_t(itr, StorageCompC)->f3, 12);

    check(!ecs_view_walk(itr));
  }

  it("fills the hole in an archetype when moving the non-last entity out") {
    const EcsEntityId entityA = ecs_world_entity_create(world);
    ecs_world_add_t(world, entityA, StorageCompA, .f1 = 1);
    ecs_world_add_t(world, entityA, StorageCompB, .f1 = 2, .f2 = 3);
    ecs_world_add_t(world, entityA, StorageCompC, .f1 = 4, .f2 = 5, .f3 = 6);

    const EcsEntityId entityB = ecs_world_entity_create(world);
    ecs_world_add_t(world, entityB, StorageCompA, .f1 = 7);
    ecs_world_add_t(world, entityB, StorageCompB, .f1 = 8, .f2 = 9);
    ecs_world_add_t(world, entityB, StorageCompC, .f1 = 10, .f2 = 11, .f3 = 12);

    ecs_world_flush(world);

    EcsIterator* itr = ecs_view_itr(ecs_world_view_t(world, ReadABC));
    check((ecs_view_jump(itr, entityA), ecs_view_read_t(itr, StorageCompC)->f3 == 6));
    check((ecs_view_jump(itr, entityB), ecs_view_read_t(itr, StorageCompC)->f3 == 12));

    ecs_world_remove_t(world, entityA, StorageCompC);

    const EcsEntityId entityC = ecs_world_entity_create(world);
    ecs_world_add_t(world, entityC, StorageCompA, .f1 = 13);
    ecs_world_add_t(world, entityC, StorageCompB, .f1 = 14, .f2 = 15);
    ecs_world_add_t(world, entityC, StorageCompC, .f1 = 16, .f2 = 17, .f3 = 18);

    ecs_world_flush(world);

    check((ecs_view_jump(itr, entityB), ecs_view_read_t(itr, StorageCompC)->f3 == 12));
    check((ecs_view_jump(itr, entityC), ecs_view_read_t(itr, StorageCompC)->f3 == 18));
  }

  it("fills the hole in an archetype when destroying the non-last entity") {
    const EcsEntityId entityA = ecs_world_entity_create(world);
    ecs_world_add_t(world, entityA, StorageCompA, .f1 = 1);
    ecs_world_add_t(world, entityA, StorageCompB, .f1 = 2, .f2 = 3);
    ecs_world_add_t(world, entityA, StorageCompC, .f1 = 4, .f2 = 5, .f3 = 6);

    const EcsEntityId entityB = ecs_world_entity_create(world);
    ecs_world_add_t(world, entityB, StorageCompA, .f1 = 7);
    ecs_world_add_t(world, entityB, StorageCompB, .f1 = 8, .f2 = 9);
    ecs_world_add_t(world, entityB, StorageCompC, .f1 = 10, .f2 = 11, .f3 = 12);

    ecs_world_flush(world);

    EcsIterator* itr = ecs_view_itr(ecs_world_view_t(world, ReadABC));
    check((ecs_view_jump(itr, entityA), ecs_view_read_t(itr, StorageCompC)->f3 == 6));
    check((ecs_view_jump(itr, entityB), ecs_view_read_t(itr, StorageCompC)->f3 == 12));

    ecs_world_entity_destroy(world, entityA);

    const EcsEntityId entityC = ecs_world_entity_create(world);
    ecs_world_add_t(world, entityC, StorageCompA, .f1 = 13);
    ecs_world_add_t(world, entityC, StorageCompB, .f1 = 14, .f2 = 15);
    ecs_world_add_t(world, entityC, StorageCompC, .f1 = 16, .f2 = 17, .f3 = 18);

    ecs_world_flush(world);

    check((ecs_view_jump(itr, entityB), ecs_view_read_t(itr, StorageCompC)->f3 == 12));
    check((ecs_view_jump(itr, entityC), ecs_view_read_t(itr, StorageCompC)->f3 == 18));
  }

  it("keeps component data consistent when destroying many entities in the same archetype") {
    static const usize g_entitiesToCreate = 567;
    DynArray           entities = dynarray_create_t(g_allocHeap, EcsEntityId, g_entitiesToCreate);

    for (usize i = 0; i != g_entitiesToCreate; ++i) {
      const EcsEntityId newEntity = ecs_world_entity_create(world);
      ecs_world_add_t(world, newEntity, StorageCompA, .f1 = (u32)i);
      ecs_world_add_t(world, newEntity, StorageCompB, .f1 = (u32)i * 2, (u32)i / 2);
      ecs_world_add_t(world, newEntity, StorageCompE, .f1 = (u32)i % 123);
      *dynarray_push_t(&entities, EcsEntityId) = newEntity;
    }

    ecs_world_flush(world);

    // Delete all even entities.
    for (usize i = 0; i != entities.size; ++i) {
      EcsEntityId* entity = dynarray_at_t(&entities, i, EcsEntityId);
      if ((i % 2) == 0) {
        ecs_world_entity_destroy(world, *entity);
      }
    }

    ecs_world_flush(world);

    EcsIterator* itr = ecs_view_itr(ecs_world_view_t(world, ReadABE));
    for (usize i = 0; i != entities.size; ++i) {
      EcsEntityId* entity = dynarray_at_t(&entities, i, EcsEntityId);
      if (i % 2) {
        check_require(ecs_world_exists(world, *entity));
        ecs_view_jump(itr, *entity);
        check_eq_int(ecs_view_read_t(itr, StorageCompA)->f1, i);
        check_eq_int(ecs_view_read_t(itr, StorageCompB)->f1, i * 2);
        check_eq_int(ecs_view_read_t(itr, StorageCompB)->f2, i / 2);
        check_eq_int(ecs_view_read_t(itr, StorageCompE)->f1, i % 123);
      } else {
        check_require(!ecs_world_exists(world, *entity));
      }
    }

    dynarray_destroy(&entities);
  }

  it("keeps component data consistent when splitting an archetype in two") {
    static const usize g_entitiesToCreate = 567;
    DynArray           entities = dynarray_create_t(g_allocHeap, EcsEntityId, g_entitiesToCreate);

    for (usize i = 0; i != g_entitiesToCreate; ++i) {
      const EcsEntityId newEntity = ecs_world_entity_create(world);
      ecs_world_add_t(world, newEntity, StorageCompA, .f1 = (u32)i);
      ecs_world_add_t(world, newEntity, StorageCompB, .f1 = (u32)i * 2, (u32)i / 2);
      ecs_world_add_t(world, newEntity, StorageCompC, .f1 = (u32)i % 123);
      *dynarray_push_t(&entities, EcsEntityId) = newEntity;
    }

    ecs_world_flush(world);

    // Move all even entities to another archetype.
    for (usize i = 0; i != entities.size; ++i) {
      EcsEntityId* entity = dynarray_at_t(&entities, i, EcsEntityId);
      if ((i % 2) == 0) {
        ecs_world_remove_t(world, *entity, StorageCompC);
        ecs_world_add_t(world, *entity, StorageCompE, .f1 = 1337);
      }
    };

    ecs_world_flush(world);

    EcsView*     viewEven   = ecs_world_view_t(world, ReadABE);
    EcsIterator* itrEven    = ecs_view_itr(viewEven);
    EcsView*     viewUneven = ecs_world_view_t(world, ReadABC);
    EcsIterator* itrUneven  = ecs_view_itr(viewUneven);
    for (usize i = 0; i != entities.size; ++i) {
      EcsEntityId* entity = dynarray_at_t(&entities, i, EcsEntityId);
      if (i % 2) {
        check_require(ecs_view_contains(viewUneven, *entity));
        ecs_view_jump(itrUneven, *entity);
        check_eq_int(ecs_view_read_t(itrUneven, StorageCompA)->f1, i);
        check_eq_int(ecs_view_read_t(itrUneven, StorageCompB)->f1, i * 2);
        check_eq_int(ecs_view_read_t(itrUneven, StorageCompB)->f2, i / 2);
        check_eq_int(ecs_view_read_t(itrUneven, StorageCompC)->f1, i % 123);
      } else {
        check_require(ecs_view_contains(viewEven, *entity));
        ecs_view_jump(itrEven, *entity);
        check_eq_int(ecs_view_read_t(itrEven, StorageCompA)->f1, i);
        check_eq_int(ecs_view_read_t(itrEven, StorageCompB)->f1, i * 2);
        check_eq_int(ecs_view_read_t(itrEven, StorageCompB)->f2, i / 2);
        check_eq_int(ecs_view_read_t(itrEven, StorageCompE)->f1, 1337);
      }
    }

    dynarray_destroy(&entities);
  }

  it("can store entities with only empty components") {
    const EcsEntityId entity = ecs_world_entity_create(world);

    ecs_world_add_empty_t(world, entity, StorageCompF);
    ecs_world_add_empty_t(world, entity, StorageCompG);

    ecs_world_flush(world);

    check(ecs_world_has_t(world, entity, StorageCompF));
    check(ecs_world_has_t(world, entity, StorageCompG));
    check(!ecs_world_has_t(world, entity, StorageCompH));

    EcsView* view = ecs_world_view_t(world, ReadFG);
    check_require(ecs_view_contains(view, entity));

    EcsIterator* itr = ecs_view_itr(view);
    check(ecs_view_walk(itr));
    check(ecs_view_read_t(itr, StorageCompF));
    check(ecs_view_read_t(itr, StorageCompG));

    ecs_world_remove_t(world, entity, StorageCompG);
    ecs_world_add_empty_t(world, entity, StorageCompH);

    ecs_world_flush(world);

    check(ecs_world_has_t(world, entity, StorageCompF));
    check(!ecs_world_has_t(world, entity, StorageCompG));
    check(ecs_world_has_t(world, entity, StorageCompH));

    check_require(!ecs_view_contains(view, entity));
  }

  teardown() {
    ecs_world_destroy(world);
    ecs_def_destroy(def);
  }
}
