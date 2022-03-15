#include "check_spec.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "core_thread.h"
#include "ecs_def.h"
#include "ecs_world.h"

static THREAD_LOCAL EcsCompId g_destructs[1024];
static THREAD_LOCAL u32       g_destructCount;

typedef enum {
  CompDataState_Normal     = 1,
  CompDataState_Destructed = 2,
} CompDataState;

ecs_comp_define(DestructCompA) {
  u32           other;
  CompDataState state;
};
ecs_comp_define(DestructCompB) {
  u64           other;
  CompDataState state;
};
ecs_comp_define(DestructCompC) {
  u64           other;
  CompDataState state;
};

static void ecs_destruct_compA(void* data) {
  DestructCompA* comp = data;
  diag_assert(comp->state == CompDataState_Normal);
  comp->state                    = CompDataState_Destructed;
  g_destructs[g_destructCount++] = ecs_comp_id(DestructCompA);
}

static void ecs_destruct_compB(void* data) {
  DestructCompB* comp = data;
  diag_assert(comp->state == CompDataState_Normal);
  comp->state                    = CompDataState_Destructed;
  g_destructs[g_destructCount++] = ecs_comp_id(DestructCompB);
}

static void ecs_destruct_compC(void* data) {
  DestructCompC* comp = data;
  diag_assert(comp->state == CompDataState_Normal);
  comp->state                    = CompDataState_Destructed;
  g_destructs[g_destructCount++] = ecs_comp_id(DestructCompC);
}

ecs_module_init(destruct_test_module) {
  ecs_register_comp(DestructCompA, .destructor = ecs_destruct_compA, .destructOrder = 1);
  ecs_register_comp(DestructCompB, .destructor = ecs_destruct_compB, .destructOrder = -1);
  ecs_register_comp(DestructCompC, .destructor = ecs_destruct_compC, .destructOrder = 2);
}

spec(destruct) {

  EcsDef* def = null;

  setup() {
    def = ecs_def_create(g_alloc_heap);
    ecs_register_module(def, destruct_test_module);
  }

  it("destroys components that are still in the world buffer waiting to be flushed") {
    EcsWorld*         world   = ecs_world_create(g_alloc_heap, def);
    const EcsEntityId entity1 = ecs_world_entity_create(world);
    const EcsEntityId entity2 = ecs_world_entity_create(world);

    g_destructCount = 0;

    ecs_world_add_t(world, entity1, DestructCompA, .state = CompDataState_Normal);

    ecs_world_add_t(world, entity2, DestructCompA, .state = CompDataState_Normal);
    ecs_world_add_t(world, entity2, DestructCompB, .state = CompDataState_Normal);
    ecs_world_add_t(world, entity2, DestructCompC, .state = CompDataState_Normal);

    ecs_world_destroy(world);

    check_require(g_destructCount == 4);
    /**
     * Destruction order is respected globally on shutdown.
     */
    check_eq_int(g_destructs[0], ecs_comp_id(DestructCompB));
    check_eq_int(g_destructs[1], ecs_comp_id(DestructCompA));
    check_eq_int(g_destructs[2], ecs_comp_id(DestructCompA));
    check_eq_int(g_destructs[3], ecs_comp_id(DestructCompC));
  }

  it("destroys pending component additions for a destroyed entity") {
    EcsWorld*         world   = ecs_world_create(g_alloc_heap, def);
    const EcsEntityId entity1 = ecs_world_entity_create(world);
    const EcsEntityId entity2 = ecs_world_entity_create(world);

    g_destructCount = 0;

    ecs_world_add_t(world, entity1, DestructCompA, .state = CompDataState_Normal);

    ecs_world_add_t(world, entity2, DestructCompA, .state = CompDataState_Normal);
    ecs_world_add_t(world, entity2, DestructCompB, .state = CompDataState_Normal);
    ecs_world_add_t(world, entity2, DestructCompC, .state = CompDataState_Normal);

    ecs_world_entity_destroy(world, entity1);
    ecs_world_entity_destroy(world, entity2);

    ecs_world_flush(world);

    check_require(g_destructCount == 4);
    /**
     * Verify that destruction order is respected globally.
     */
    check_eq_int(g_destructs[0], ecs_comp_id(DestructCompB));
    check_eq_int(g_destructs[1], ecs_comp_id(DestructCompA));
    check_eq_int(g_destructs[2], ecs_comp_id(DestructCompA));
    check_eq_int(g_destructs[3], ecs_comp_id(DestructCompC));

    ecs_world_destroy(world);

    check_require(g_destructCount == 4);
  }

  it("destroys stored components when the world is destroyed") {
    EcsWorld*         world   = ecs_world_create(g_alloc_heap, def);
    const EcsEntityId entity1 = ecs_world_entity_create(world);
    const EcsEntityId entity2 = ecs_world_entity_create(world);

    g_destructCount = 0;

    ecs_world_add_t(world, entity1, DestructCompA, .state = CompDataState_Normal);

    ecs_world_add_t(world, entity2, DestructCompA, .state = CompDataState_Normal);
    ecs_world_add_t(world, entity2, DestructCompB, .state = CompDataState_Normal);
    ecs_world_add_t(world, entity2, DestructCompC, .state = CompDataState_Normal);

    ecs_world_flush(world); // Move the components into archetypes.

    ecs_world_destroy(world);

    check_require(g_destructCount == 4);
    /**
     * Verify that destruction order is respected globally.
     */
    check_eq_int(g_destructs[0], ecs_comp_id(DestructCompB));
    check_eq_int(g_destructs[1], ecs_comp_id(DestructCompA));
    check_eq_int(g_destructs[2], ecs_comp_id(DestructCompA));
    check_eq_int(g_destructs[3], ecs_comp_id(DestructCompC));
  }

  it("destroys stored components from all chunks when the world is destroyed") {
    static const usize g_entitiesToCreate = 567;
    DynArray           entities = dynarray_create_t(g_alloc_heap, EcsEntityId, g_entitiesToCreate);
    EcsWorld*          world    = ecs_world_create(g_alloc_heap, def);

    g_destructCount = 0;

    for (usize i = 0; i != g_entitiesToCreate; ++i) {
      const EcsEntityId newEntity = ecs_world_entity_create(world);
      ecs_world_add_t(world, newEntity, DestructCompA, .state = CompDataState_Normal);
      *dynarray_push_t(&entities, EcsEntityId) = newEntity;
    }

    ecs_world_flush(world);

    ecs_world_destroy(world);

    check_require(g_destructCount == g_entitiesToCreate);

    dynarray_destroy(&entities);
  }

  it("destroys components when destroying entities") {
    EcsWorld*         world   = ecs_world_create(g_alloc_heap, def);
    const EcsEntityId entity1 = ecs_world_entity_create(world);
    const EcsEntityId entity2 = ecs_world_entity_create(world);

    g_destructCount = 0;

    ecs_world_add_t(world, entity1, DestructCompA, .state = CompDataState_Normal);

    ecs_world_add_t(world, entity2, DestructCompA, .state = CompDataState_Normal);
    ecs_world_add_t(world, entity2, DestructCompB, .state = CompDataState_Normal);
    ecs_world_add_t(world, entity2, DestructCompC, .state = CompDataState_Normal);

    ecs_world_flush(world);

    ecs_world_entity_destroy(world, entity1);
    ecs_world_entity_destroy(world, entity2);

    ecs_world_flush(world);

    check_require(g_destructCount == 4);
    /**
     * Verify that destruction order is respected globally.
     */
    check_eq_int(g_destructs[0], ecs_comp_id(DestructCompB));
    check_eq_int(g_destructs[1], ecs_comp_id(DestructCompA));
    check_eq_int(g_destructs[2], ecs_comp_id(DestructCompA));
    check_eq_int(g_destructs[3], ecs_comp_id(DestructCompC));

    ecs_world_destroy(world);
  }

  it("destroys components when removing them from entities") {
    EcsWorld*         world   = ecs_world_create(g_alloc_heap, def);
    const EcsEntityId entity1 = ecs_world_entity_create(world);
    const EcsEntityId entity2 = ecs_world_entity_create(world);

    g_destructCount = 0;

    ecs_world_add_t(world, entity1, DestructCompA, .state = CompDataState_Normal);

    ecs_world_add_t(world, entity2, DestructCompA, .state = CompDataState_Normal);
    ecs_world_add_t(world, entity2, DestructCompB, .state = CompDataState_Normal);
    ecs_world_add_t(world, entity2, DestructCompC, .state = CompDataState_Normal);

    ecs_world_flush(world);

    ecs_world_remove_t(world, entity1, DestructCompA);
    ecs_world_add_t(world, entity1, DestructCompB, .state = CompDataState_Normal);

    ecs_world_remove_t(world, entity2, DestructCompA);
    ecs_world_remove_t(world, entity2, DestructCompB);
    ecs_world_remove_t(world, entity2, DestructCompC);

    ecs_world_flush(world);

    check_require(g_destructCount == 4);
    /**
     * Verify that destruction order is respected globally.
     */
    check_eq_int(g_destructs[0], ecs_comp_id(DestructCompB));
    check_eq_int(g_destructs[1], ecs_comp_id(DestructCompA));
    check_eq_int(g_destructs[2], ecs_comp_id(DestructCompA));
    check_eq_int(g_destructs[3], ecs_comp_id(DestructCompC));

    ecs_world_destroy(world);
  }

  teardown() { ecs_def_destroy(def); }
}
