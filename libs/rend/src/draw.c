#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "ecs_world.h"
#include "rend_register.h"

#include "draw_internal.h"

#define rend_draw_min_align 16

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

RendDrawComp* rend_draw_create(EcsWorld* world, const EcsEntityId entity) {
  return ecs_world_add_t(
      world, entity, RendDrawComp, .instances = dynarray_create(g_alloc_heap, 1, 16, 0));
}

EcsEntityId rend_draw_graphic(const RendDrawComp* draw) { return draw->graphic; }

bool rend_draw_gather(RendDrawComp* draw) {
  // TODO: Perform culling etc.
  return draw->instances.size != 0;
}

RvkPassDraw rend_draw_output(const RendDrawComp* draw, RvkGraphic* graphic) {
  return (RvkPassDraw){
      .graphic             = graphic,
      .vertexCountOverride = draw->vertexCountOverride,
      .instanceCount       = (u32)draw->instances.size,
      .data                = dynarray_at(&draw->instances, 0, draw->instances.size),
      .dataStride          = draw->instances.stride,
  };
}

void rend_draw_set_graphic(RendDrawComp* comp, const EcsEntityId graphic) {
  comp->graphic = graphic;
}

void rend_draw_set_vertex_count(RendDrawComp* comp, const u32 vertexCount) {
  comp->vertexCountOverride = vertexCount;
}

void rend_draw_set_data_size(RendDrawComp* draw, const u32 size) {
  // TODO: size 0 should probably be valid, but is not properly handled at the moment.
  dynarray_clear(&draw->instances);
  draw->instances.stride = bits_align(size, rend_draw_min_align);
}

Mem rend_draw_add_instance(RendDrawComp* draw) { return dynarray_push(&draw->instances, 1); }
