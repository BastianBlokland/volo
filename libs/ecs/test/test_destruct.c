#include "check_spec.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "core_thread.h"
#include "ecs_def.h"
#include "ecs_world.h"

static THREAD_LOCAL u32 g_destructorCountA, g_destructorCountB;

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

static void ecs_destruct_compA(void* data) {
  DestructCompA* comp = data;
  diag_assert(comp->state == CompDataState_Normal);
  comp->state = CompDataState_Destructed;
  ++g_destructorCountA;
}

static void ecs_destruct_compB(void* data) {
  DestructCompB* comp = data;
  diag_assert(comp->state == CompDataState_Normal);
  comp->state = CompDataState_Destructed;
  ++g_destructorCountB;
}

ecs_module_init(destruct_test_module) {
  ecs_register_comp(DestructCompA, .destructor = ecs_destruct_compA);
  ecs_register_comp(DestructCompB, .destructor = ecs_destruct_compB);
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

    g_destructorCountA = 0;
    g_destructorCountB = 0;

    ecs_world_comp_add_t(world, entity1, DestructCompA, .state = CompDataState_Normal);

    ecs_world_comp_add_t(world, entity2, DestructCompA, .state = CompDataState_Normal);
    ecs_world_comp_add_t(world, entity2, DestructCompB, .state = CompDataState_Normal);

    ecs_world_destroy(world);

    check_eq_int(g_destructorCountA, 2);
    check_eq_int(g_destructorCountB, 1);
  }

  it("destroys stored components when the world is destroyed") {
    EcsWorld*         world   = ecs_world_create(g_alloc_heap, def);
    const EcsEntityId entity1 = ecs_world_entity_create(world);
    const EcsEntityId entity2 = ecs_world_entity_create(world);

    g_destructorCountA = 0;
    g_destructorCountB = 0;

    ecs_world_comp_add_t(world, entity1, DestructCompA, .state = CompDataState_Normal);

    ecs_world_comp_add_t(world, entity2, DestructCompA, .state = CompDataState_Normal);
    ecs_world_comp_add_t(world, entity2, DestructCompB, .state = CompDataState_Normal);

    ecs_world_flush(world); // Move the components into archetypes.

    ecs_world_destroy(world);

    check_eq_int(g_destructorCountA, 2);
    check_eq_int(g_destructorCountB, 1);
  }

  it("destroys stored components from all chunks when the world is destroyed") {
    static const usize entitiesToCreate = 567;
    DynArray           entities = dynarray_create_t(g_alloc_heap, EcsEntityId, entitiesToCreate);
    EcsWorld*          world    = ecs_world_create(g_alloc_heap, def);

    g_destructorCountA = 0;
    g_destructorCountB = 0;

    for (usize i = 0; i != entitiesToCreate; ++i) {
      const EcsEntityId newEntity = ecs_world_entity_create(world);
      ecs_world_comp_add_t(world, newEntity, DestructCompA, .state = CompDataState_Normal);
      *dynarray_push_t(&entities, EcsEntityId) = newEntity;
    }

    ecs_world_flush(world);

    ecs_world_destroy(world);

    check_eq_int(g_destructorCountA, entitiesToCreate);
    check_eq_int(g_destructorCountB, 0);

    dynarray_destroy(&entities);
  }

  it("destroys components when destroying entities") {
    EcsWorld*         world   = ecs_world_create(g_alloc_heap, def);
    const EcsEntityId entity1 = ecs_world_entity_create(world);
    const EcsEntityId entity2 = ecs_world_entity_create(world);

    g_destructorCountA = 0;
    g_destructorCountB = 0;

    ecs_world_comp_add_t(world, entity1, DestructCompA, .state = CompDataState_Normal);

    ecs_world_comp_add_t(world, entity2, DestructCompA, .state = CompDataState_Normal);
    ecs_world_comp_add_t(world, entity2, DestructCompB, .state = CompDataState_Normal);

    ecs_world_flush(world);

    ecs_world_entity_destroy(world, entity1);
    ecs_world_entity_destroy(world, entity2);

    ecs_world_flush(world);

    check_eq_int(g_destructorCountA, 2);
    check_eq_int(g_destructorCountB, 1);

    ecs_world_destroy(world);
  }

  teardown() { ecs_def_destroy(def); }
}
