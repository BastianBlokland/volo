#include "core_diag.h"

#include "draw_internal.h"

ecs_comp_define_public(RendDrawComp);

static void ecs_destruct_draw(void* data) {
  RendDrawComp* comp = data;
  dynarray_destroy(&comp->instances);
}

static void ecs_combine_draw(void* dataA, void* dataB) {
  RendDrawComp* compA = dataA;
  RendDrawComp* compB = dataB;
  diag_assert_msg(
      compA->instances.stride == compB->instances.stride,
      "Only draws with the same data-stride can be combined");

  mem_cpy(
      dynarray_push(&compA->instances, compB->instances.size),
      dynarray_at(&compB->instances, 0, compB->instances.size));
  dynarray_destroy(&compB->instances);
}

ecs_module_init(rend_draw_module) {
  ecs_register_comp(RendDrawComp, .destructor = ecs_destruct_draw, .combinator = ecs_combine_draw);
}
