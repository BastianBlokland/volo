#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "ecs_world.h"
#include "rend_register.h"

#include "draw_internal.h"
#include "resource_internal.h"

#define rend_min_align 16
#define rend_max_res_requests 16

ecs_comp_define(RendDrawComp) {
  EcsEntityId   graphic;
  EcsEntityId   cameraFilter;
  RendDrawFlags flags;
  u32           vertexCountOverride;
  u32           instances;
  u32           outputInstances;

  u32 dataSize; // NOTE: Size per instance.

  Mem dataMem, tagsMem, aabbMem;
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
  if (mem_valid(comp->aabbMem)) {
    alloc_free(g_alloc_heap, comp->aabbMem);
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
    const GeoBox    aabb         = mem_as_t(drawB->aabbMem, GeoBox)[i];
    mem_cpy(rend_draw_add_instance(drawA, tags, aabb), instanceData);
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

/**
 * Request the given graphic entity to be loaded.
 */
static void rend_draw_request_graphic(
    EcsWorld* world, const EcsEntityId entity, EcsIterator* graphicItr, u32* numRequests) {
  /**
   * If the graphic resource is already loaded then tell the resource system we're still using it
   * (so it won't be unloaded). If its not loaded then start loading it.
   */
  if (LIKELY(ecs_view_maybe_jump(graphicItr, entity))) {
    rend_resource_mark_used(ecs_view_write_t(graphicItr, RendResComp));
    return;
  }
  if (++*numRequests < rend_max_res_requests) {
    rend_resource_request(world, entity);
  }
}

ecs_view_define(ResourceView) { ecs_access_write(RendResComp); }
ecs_view_define(DrawReadView) { ecs_access_read(RendDrawComp); }
ecs_view_define(DrawWriteView) { ecs_access_write(RendDrawComp); }

ecs_system_define(RendClearDrawsSys) {
  EcsView* drawView = ecs_world_view_t(world, DrawWriteView);
  for (EcsIterator* itr = ecs_view_itr(drawView); ecs_view_walk(itr);) {
    ecs_view_write_t(itr, RendDrawComp)->instances = 0;
  }
}

ecs_system_define(RendDrawRequestGraphicSys) {
  u32 numRequests = 0;

  EcsIterator* graphicResItr = ecs_view_itr(ecs_world_view_t(world, ResourceView));

  // Request the graphic resource for all draw's to be loaded.
  EcsView* drawView = ecs_world_view_t(world, DrawReadView);
  for (EcsIterator* itr = ecs_view_itr(drawView); ecs_view_walk(itr);) {
    const RendDrawComp* comp = ecs_view_read_t(itr, RendDrawComp);
    if (comp->instances && comp->graphic) {
      rend_draw_request_graphic(world, comp->graphic, graphicResItr, &numRequests);
    }
  }
}

ecs_module_init(rend_draw_module) {
  ecs_register_comp(RendDrawComp, .destructor = ecs_destruct_draw, .combinator = ecs_combine_draw);

  ecs_register_view(ResourceView);
  ecs_register_view(DrawReadView);
  ecs_register_view(DrawWriteView);

  ecs_register_system(RendClearDrawsSys, ecs_view_id(DrawWriteView));
  ecs_register_system(
      RendDrawRequestGraphicSys, ecs_view_id(DrawReadView), ecs_view_id(ResourceView));

  ecs_order(RendClearDrawsSys, RendOrder_DrawClear);
  ecs_order(RendDrawRequestGraphicSys, RendOrder_DrawCollect + 1);
}

RendDrawComp*
rend_draw_create(EcsWorld* world, const EcsEntityId entity, const RendDrawFlags flags) {
  return ecs_world_add_t(world, entity, RendDrawComp, .flags = flags);
}

EcsEntityId rend_draw_graphic(const RendDrawComp* draw) { return draw->graphic; }

bool rend_draw_gather(RendDrawComp* draw, const RendView* view) {
  if (draw->cameraFilter && view->camera != draw->cameraFilter) {
    return false;
  }
  if (draw->flags & RendDrawFlags_NoInstanceFiltering) {
    /**
     * If we can skip the instance filtering, we can also skip the memory copy that is needed to
     * keep the instances contiguous in memory.
     */
    return draw->instances != 0;
  }

  /**
   * Gather the actual draws after filtering.
   * Because we need the output data to be contiguous in memory we have to copy the instances that
   * pass the filter to separate output memory.
   */

  rend_draw_ensure_storage(&draw->outputMem, draw->instances * draw->dataSize, rend_min_align);

  draw->outputInstances = 0;
  for (u32 i = 0; i != draw->instances; ++i) {
    const SceneTags instanceTags = mem_as_t(draw->tagsMem, SceneTags)[i];
    const GeoBox*   instanceAabb = &mem_as_t(draw->aabbMem, GeoBox)[i];
    if (!rend_view_visible(view, instanceTags, instanceAabb)) {
      continue;
    }
    mem_cpy(rend_draw_output_data(draw, draw->outputInstances++), rend_draw_data(draw, i));
  }
  return draw->outputInstances != 0;
}

RvkPassDraw rend_draw_output(const RendDrawComp* draw, RvkGraphic* graphic) {
  u32 instanceCount;
  Mem data;
  if (draw->flags & RendDrawFlags_NoInstanceFiltering) {
    instanceCount = draw->instances;
    data          = mem_slice(draw->dataMem, 0, instanceCount * draw->dataSize);
  } else {
    instanceCount = draw->outputInstances;
    data          = mem_slice(draw->outputMem, 0, instanceCount * draw->dataSize);
  }
  return (RvkPassDraw){
      .graphic             = graphic,
      .vertexCountOverride = draw->vertexCountOverride,
      .instanceCount       = instanceCount,
      .data                = data,
      .dataStride          = draw->dataSize,
  };
}

void rend_draw_set_graphic(RendDrawComp* comp, const EcsEntityId graphic) {
  comp->graphic = graphic;
}

void rend_draw_set_camera_filter(RendDrawComp* comp, const EcsEntityId camera) {
  comp->cameraFilter = camera;
}

void rend_draw_set_vertex_count(RendDrawComp* comp, const u32 vertexCount) {
  comp->vertexCountOverride = vertexCount;
}

void rend_draw_set_data_size(RendDrawComp* draw, const u32 size) {
  draw->instances = 0;
  draw->dataSize  = bits_align(size, rend_min_align);
}

Mem rend_draw_add_instance(RendDrawComp* draw, const SceneTags tags, const GeoBox aabb) {
  ++draw->instances;
  rend_draw_ensure_storage(&draw->dataMem, draw->instances * draw->dataSize, rend_min_align);
  rend_draw_ensure_storage(&draw->tagsMem, draw->instances * sizeof(SceneTags), 1);
  rend_draw_ensure_storage(&draw->aabbMem, draw->instances * sizeof(GeoBox), alignof(GeoBox));

  mem_as_t(draw->tagsMem, SceneTags)[draw->instances - 1] = tags;
  mem_as_t(draw->aabbMem, GeoBox)[draw->instances - 1]    = aabb;
  return rend_draw_data(draw, draw->instances - 1);
}
