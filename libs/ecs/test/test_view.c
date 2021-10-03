#include "check_spec.h"
#include "core_alloc.h"
#include "ecs_def.h"
#include "ecs_world.h"

ecs_comp_define(ViewCompA) { u32 f1; };
ecs_comp_define(ViewCompB) { u32 f1; };
ecs_comp_define(ViewCompC) { u32 f1; };

ecs_view_define(ReadAB) {
  ecs_view_read(ViewCompA);
  ecs_view_read(ViewCompB);
}

ecs_module_init(view_test_module) {
  ecs_register_comp(ViewCompA);
  ecs_register_comp(ViewCompB);
  ecs_register_comp(ViewCompC);

  ecs_register_view(ReadAB);
}

spec(view) {

  EcsDef*   def   = null;
  EcsWorld* world = null;

  setup() {
    def = ecs_def_create(g_alloc_heap);
    ecs_register_module(def, view_test_module);

    world = ecs_world_create(g_alloc_heap, def);
  }

  it("can check if an entity is contained in the view") {
    const EcsEntityId entity1 = ecs_world_entity_create(world);
    const EcsEntityId entity2 = ecs_world_entity_create(world);
    const EcsEntityId entity3 = ecs_world_entity_create(world);

    ecs_world_comp_add_t(world, entity1, ViewCompA);
    ecs_world_comp_add_t(world, entity1, ViewCompB);

    ecs_world_comp_add_t(world, entity2, ViewCompA);
    ecs_world_comp_add_t(world, entity2, ViewCompC);

    ecs_world_comp_add_t(world, entity3, ViewCompA);
    ecs_world_comp_add_t(world, entity3, ViewCompB);
    ecs_world_comp_add_t(world, entity3, ViewCompC);

    ecs_world_flush(world);

    EcsView* view = ecs_world_view_t(world, ReadAB);
    check(ecs_view_contains(view, entity1));
    check(!ecs_view_contains(view, entity2));
    check(ecs_view_contains(view, entity3));
  }

  teardown() {
    ecs_world_destroy(world);
    ecs_def_destroy(def);
  }
}
