#include "core_diag.h"
#include "ecs_world.h"
#include "rend_register.h"

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

ecs_view_define(DrawView) { ecs_access_write(RendDrawComp); }

ecs_system_define(RendClearDrawsSys) {
  EcsView* drawView = ecs_world_view_t(world, DrawView);
  for (EcsIterator* itr = ecs_view_itr(drawView); ecs_view_walk(itr);) {
    dynarray_clear(&ecs_view_write_t(itr, RendDrawComp)->instances);
  }
}

ecs_module_init(rend_draw_module) {
  ecs_register_comp(RendDrawComp, .destructor = ecs_destruct_draw, .combinator = ecs_combine_draw);

  ecs_register_view(DrawView);

  ecs_register_system(RendClearDrawsSys, ecs_view_id(DrawView));

  ecs_order(RendClearDrawsSys, RendOrder_DrawCollect - 1);
}
