#include "check_spec.h"
#include "core_alloc.h"
#include "ecs_def.h"
#include "ecs_world.h"

ecs_comp_define(CombineCompA) { u64 state; };
ecs_comp_define(CombineCompB) { String text; };

ecs_view_define(ReadA) { ecs_access_read(CombineCompA); }
ecs_view_define(ReadB) { ecs_access_read(CombineCompB); }

static void ecs_combine_compA(void* a, void* b) {
  CombineCompA* compA = a;
  CombineCompA* compB = b;
  compA->state += compB->state;
}

static void ecs_combine_compB(void* a, void* b) {
  CombineCompB* compA = a;
  CombineCompB* compB = b;

  String newText = string_combine(g_alloc_heap, compA->text, compB->text);
  string_free(g_alloc_heap, compA->text);
  string_free(g_alloc_heap, compB->text);

  compA->text = newText;
}

static void ecs_destruct_compB(void* data) {
  CombineCompB* comp = data;
  string_free(g_alloc_heap, comp->text);
}

ecs_module_init(combine_test_module) {
  ecs_register_comp(CombineCompA, .combinator = ecs_combine_compA);
  ecs_register_comp(
      CombineCompB, .combinator = ecs_combine_compB, .destructor = ecs_destruct_compB);

  ecs_register_view(ReadA);
  ecs_register_view(ReadB);
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
    const EcsEntityId e = ecs_world_entity_create(world);

    ecs_world_add_t(world, e, CombineCompA, .state = 42);
    ecs_world_add_t(world, e, CombineCompA, .state = 1337);

    ecs_world_flush(world);

    EcsIterator* itr = ecs_view_itr_at(ecs_world_view_t(world, ReadA), e);
    check_eq_int(ecs_view_read_t(itr, CombineCompA)->state, 1379);
  }

  it("supports combining many pending components") {
    const EcsEntityId e = ecs_world_entity_create(world);

    static const usize compCount = 1337;
    for (usize i = 0; i != compCount; ++i) {
      ecs_world_add_t(world, e, CombineCompA, .state = 2);
    }

    ecs_world_flush(world);

    EcsIterator* itr = ecs_view_itr_at(ecs_world_view_t(world, ReadA), e);
    check_eq_int(ecs_view_read_t(itr, CombineCompA)->state, compCount * 2);
  }

  it("supports combining a pending component with an existing component") {
    const EcsEntityId e = ecs_world_entity_create(world);

    ecs_world_add_t(world, e, CombineCompA, .state = 42);

    ecs_world_flush(world);
    EcsIterator* itrA = ecs_view_itr_at(ecs_world_view_t(world, ReadA), e);
    check_eq_int(ecs_view_read_t(itrA, CombineCompA)->state, 42);

    ecs_world_add_t(world, e, CombineCompA, .state = 1337);

    ecs_world_flush(world);
    EcsIterator* itrB = ecs_view_itr_at(ecs_world_view_t(world, ReadA), e);
    check_eq_int(ecs_view_read_t(itrB, CombineCompA)->state, 1379);
  }

  it("supports combining components with destructors") {
    const EcsEntityId e = ecs_world_entity_create(world);

    ecs_world_add_t(world, e, CombineCompB, .text = string_dup(g_alloc_heap, string_lit("Hello")));
    ecs_world_add_t(world, e, CombineCompB, .text = string_dup(g_alloc_heap, string_lit(" ")));
    ecs_world_add_t(world, e, CombineCompB, .text = string_dup(g_alloc_heap, string_lit("World")));

    ecs_world_flush(world);

    EcsIterator* itr = ecs_view_itr_at(ecs_world_view_t(world, ReadB), e);
    check_eq_string(ecs_view_read_t(itr, CombineCompB)->text, string_lit("Hello World"));
  }

  teardown() {
    ecs_world_destroy(world);
    ecs_def_destroy(def);
  }
}
