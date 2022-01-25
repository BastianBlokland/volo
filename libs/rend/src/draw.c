#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "ecs_world.h"
#include "rend_register.h"

#include "draw_internal.h"

#define rend_draw_min_align 16

ecs_comp_define(RendDrawComp) {
  EcsEntityId graphic;
  u32         vertexCountOverride;
  u32         dataSize; // NOTE: Size per instance.
  Mem         dataMemory;
  u32         instances;
};

static void ecs_destruct_draw(void* data) {
  RendDrawComp* comp = data;
  if (mem_valid(comp->dataMemory)) {
    alloc_free(g_alloc_heap, comp->dataMemory);
  }
}

static void ecs_combine_draw(void* dataA, void* dataB) {
  RendDrawComp* drawA = dataA;
  RendDrawComp* drawB = dataB;
  diag_assert_msg(
      drawA->dataSize == drawB->dataSize, "Only draws with the same data-stride can be combined");

  for (u32 i = 0; i != drawB->instances; ++i) {
    const Mem instanceData = mem_slice(drawB->dataMemory, drawB->dataSize * i, drawB->dataSize);
    mem_cpy(rend_draw_add_instance(drawA), instanceData);
  }

  if (mem_valid(drawB->dataMemory)) {
    alloc_free(g_alloc_heap, drawB->dataMemory);
  }
}

static void rend_draw_ensure_data_storage(RendDrawComp* draw, const u32 instances) {
  const u32 neededSize = instances * draw->dataSize;
  if (UNLIKELY(draw->dataMemory.size < neededSize)) {
    const Mem newMem = alloc_alloc(g_alloc_heap, bits_nextpow2_32(neededSize), rend_draw_min_align);
    if (mem_valid(draw->dataMemory)) {
      mem_cpy(newMem, draw->dataMemory);
      alloc_free(g_alloc_heap, draw->dataMemory);
    }
    draw->dataMemory = newMem;
  }
}

static Mem rend_draw_data(const RendDrawComp* draw, const u32 instance) {
  return mem_slice(draw->dataMemory, instance * draw->dataSize, draw->dataSize);
}

ecs_view_define(DrawView) { ecs_access_write(RendDrawComp); }

ecs_system_define(RendClearDrawsSys) {
  EcsView* drawView = ecs_world_view_t(world, DrawView);
  for (EcsIterator* itr = ecs_view_itr(drawView); ecs_view_walk(itr);) {
    ecs_view_write_t(itr, RendDrawComp)->instances = 0;
  }
}

ecs_module_init(rend_draw_module) {
  ecs_register_comp(RendDrawComp, .destructor = ecs_destruct_draw, .combinator = ecs_combine_draw);

  ecs_register_view(DrawView);

  ecs_register_system(RendClearDrawsSys, ecs_view_id(DrawView));

  ecs_order(RendClearDrawsSys, RendOrder_DrawCollect - 1);
}

RendDrawComp* rend_draw_create(EcsWorld* world, const EcsEntityId entity) {
  return ecs_world_add_t(world, entity, RendDrawComp);
}

EcsEntityId rend_draw_graphic(const RendDrawComp* draw) { return draw->graphic; }

bool rend_draw_gather(RendDrawComp* draw) {
  // TODO: Perform culling etc.
  return draw->instances != 0;
}

RvkPassDraw rend_draw_output(const RendDrawComp* draw, RvkGraphic* graphic) {
  return (RvkPassDraw){
      .graphic             = graphic,
      .vertexCountOverride = draw->vertexCountOverride,
      .instanceCount       = draw->instances,
      .data                = mem_slice(draw->dataMemory, 0, draw->instances * draw->dataSize),
      .dataStride          = draw->dataSize,
  };
}

void rend_draw_set_graphic(RendDrawComp* comp, const EcsEntityId graphic) {
  comp->graphic = graphic;
}

void rend_draw_set_vertex_count(RendDrawComp* comp, const u32 vertexCount) {
  comp->vertexCountOverride = vertexCount;
}

void rend_draw_set_data_size(RendDrawComp* draw, const u32 size) {
  draw->instances = 0;
  draw->dataSize  = bits_align(size, rend_draw_min_align);
}

Mem rend_draw_add_instance(RendDrawComp* draw) {
  ++draw->instances;
  rend_draw_ensure_data_storage(draw, draw->instances);
  return rend_draw_data(draw, draw->instances - 1);
}
