#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "ecs_world.h"
#include "rend_register.h"

#include "draw_internal.h"

#define rend_min_align 16

ecs_comp_define(RendDrawComp) {
  EcsEntityId graphic;
  u32         vertexCountOverride;
  u32         instances;
  u32         outputInstances;

  u32 dataSize; // NOTE: Size per instance.

  Mem dataMem;
  Mem tagsMem;
  Mem outputMem;
};

static void ecs_destruct_draw(void* data) {
  RendDrawComp* comp = data;
  if (mem_valid(comp->dataMem)) {
    alloc_free(g_alloc_heap, comp->dataMem);
  }
  if (mem_valid(comp->tagsMem)) {
    alloc_free(g_alloc_heap, comp->tagsMem);
  }
  if (mem_valid(comp->outputMem)) {
    alloc_free(g_alloc_heap, comp->outputMem);
  }
}

static void ecs_combine_draw(void* dataA, void* dataB) {
  RendDrawComp* drawA = dataA;
  RendDrawComp* drawB = dataB;
  diag_assert_msg(
      drawA->dataSize == drawB->dataSize, "Only draws with the same data-stride can be combined");

  for (u32 i = 0; i != drawB->instances; ++i) {
    const Mem       instanceData = mem_slice(drawB->dataMem, drawB->dataSize * i, drawB->dataSize);
    const SceneTags tags         = mem_as_t(drawB->tagsMem, SceneTags)[i];
    mem_cpy(rend_draw_add_instance(drawA, tags), instanceData);
  }

  ecs_destruct_draw(drawB);
}

static void rend_draw_ensure_storage(Mem* mem, const usize neededSize, const usize align) {
  if (UNLIKELY(mem->size < neededSize)) {
    const Mem newMem = alloc_alloc(g_alloc_heap, bits_nextpow2(neededSize), align);
    if (mem_valid(*mem)) {
      mem_cpy(newMem, *mem);
      alloc_free(g_alloc_heap, *mem);
    }
    *mem = newMem;
  }
}

static Mem rend_draw_data(const RendDrawComp* draw, const u32 instance) {
  return mem_slice(draw->dataMem, instance * draw->dataSize, draw->dataSize);
}

static Mem rend_draw_output_data(const RendDrawComp* draw, const u32 instance) {
  return mem_slice(draw->outputMem, instance * draw->dataSize, draw->dataSize);
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

bool rend_draw_gather(RendDrawComp* draw, const SceneTagFilter filter) {
  /**
   * Gather the actual draws after filtering.
   * Because we need the output data to be contiguous in memory we have to copy the instances that
   * pass the filter to separate output memory.
   */

  rend_draw_ensure_storage(&draw->outputMem, draw->instances * draw->dataSize, rend_min_align);

  draw->outputInstances = 0;
  for (u32 i = 0; i != draw->instances; ++i) {
    const SceneTags instanceTags = mem_as_t(draw->tagsMem, SceneTags)[i];
    if (!scene_tag_filter(filter, instanceTags)) {
      continue;
    }
    mem_cpy(rend_draw_output_data(draw, draw->outputInstances++), rend_draw_data(draw, i));
  }
  return draw->outputInstances != 0;
}

RvkPassDraw rend_draw_output(const RendDrawComp* draw, RvkGraphic* graphic) {
  return (RvkPassDraw){
      .graphic             = graphic,
      .vertexCountOverride = draw->vertexCountOverride,
      .instanceCount       = draw->outputInstances,
      .data                = mem_slice(draw->outputMem, 0, draw->outputInstances * draw->dataSize),
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
  draw->dataSize  = bits_align(size, rend_min_align);
}

Mem rend_draw_add_instance(RendDrawComp* draw, const SceneTags tags) {
  ++draw->instances;
  rend_draw_ensure_storage(&draw->dataMem, draw->instances * draw->dataSize, rend_min_align);
  rend_draw_ensure_storage(&draw->tagsMem, draw->instances * sizeof(SceneTags), 1);

  mem_as_t(draw->tagsMem, SceneTags)[draw->instances - 1] = tags;
  return rend_draw_data(draw, draw->instances - 1);
}
