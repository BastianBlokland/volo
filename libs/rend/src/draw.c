#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "ecs_world.h"
#include "rend_register.h"

#include "draw_internal.h"
#include "reset_internal.h"
#include "resource_internal.h"

#define rend_min_align 16
#define rend_max_res_requests 16

ecs_comp_define(RendDrawComp) {
  EcsEntityId   graphic;
  EcsEntityId   cameraFilter;
  RendDrawFlags flags;
  u32           vertexCountOverride;
  u32           instCount;
  u32           outputInstCount;

  u32 dataSize;     // Size of the 'per draw' data.
  u32 instDataSize; // Size of the 'per instance' data.

  Mem dataMem;
  Mem instDataMem, instTagsMem, instAabbMem;
  Mem instDataOutput;
};

static void ecs_destruct_draw(void* data) {
  RendDrawComp* comp = data;
  if (mem_valid(comp->dataMem)) {
    alloc_free(g_alloc_heap, comp->dataMem);
  }
  if (mem_valid(comp->instDataMem)) {
    alloc_free(g_alloc_heap, comp->instDataMem);
  }
  if (mem_valid(comp->instTagsMem)) {
    alloc_free(g_alloc_heap, comp->instTagsMem);
  }
  if (mem_valid(comp->instAabbMem)) {
    alloc_free(g_alloc_heap, comp->instAabbMem);
  }
  if (mem_valid(comp->instDataOutput)) {
    alloc_free(g_alloc_heap, comp->instDataOutput);
  }
}

static void ecs_combine_draw(void* dataA, void* dataB) {
  RendDrawComp* drawA = dataA;
  RendDrawComp* drawB = dataB;
  diag_assert_msg(
      drawA->instDataSize == drawB->instDataSize,
      "Only draws with the same instance-data stride can be combined");

  for (u32 i = 0; i != drawB->instCount; ++i) {
    const Mem data = mem_slice(drawB->instDataMem, drawB->instDataSize * i, drawB->instDataSize);
    const SceneTags tags = mem_as_t(drawB->instTagsMem, SceneTags)[i];
    const GeoBox    aabb = mem_as_t(drawB->instAabbMem, GeoBox)[i];
    rend_draw_add_instance(drawA, data, tags, aabb);
  }

  ecs_destruct_draw(drawB);
}

INLINE_HINT static void
rend_draw_ensure_storage(Mem* mem, const usize neededSize, const usize align) {
  if (UNLIKELY(mem->size < neededSize)) {
    const Mem newMem = alloc_alloc(g_alloc_heap, bits_nextpow2(neededSize), align);
    if (mem_valid(*mem)) {
      mem_cpy(newMem, *mem);
      alloc_free(g_alloc_heap, *mem);
    }
    *mem = newMem;
  }
}

static Mem rend_draw_inst_data(const RendDrawComp* draw, const u32 instance) {
  const usize offset = instance * draw->instDataSize;
  return mem_create(bits_ptr_offset(draw->instDataMem.ptr, offset), draw->instDataSize);
}

static Mem rend_draw_inst_output_data(const RendDrawComp* draw, const u32 instance) {
  const usize offset = instance * draw->instDataSize;
  return mem_create(bits_ptr_offset(draw->instDataOutput.ptr, offset), draw->instDataSize);
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
    const bool isPersistent = false;
    rend_resource_request(world, entity, isPersistent);
  }
}

ecs_view_define(ResourceView) { ecs_access_write(RendResComp); }
ecs_view_define(DrawReadView) { ecs_access_read(RendDrawComp); }
ecs_view_define(DrawWriteView) { ecs_access_write(RendDrawComp); }

ecs_system_define(RendClearDrawsSys) {
  EcsView* drawView = ecs_world_view_t(world, DrawWriteView);
  for (EcsIterator* itr = ecs_view_itr(drawView); ecs_view_walk(itr);) {
    RendDrawComp* drawComp = ecs_view_write_t(itr, RendDrawComp);
    if (!(drawComp->flags & RendDrawFlags_NoAutoClear)) {
      drawComp->instCount = 0;
    }
  }
}

ecs_system_define(RendDrawRequestGraphicSys) {
  if (rend_will_reset(world)) {
    return;
  }

  u32 numRequests = 0;

  EcsIterator* graphicResItr = ecs_view_itr(ecs_world_view_t(world, ResourceView));

  // Request the graphic resource for all draw's to be loaded.
  EcsView* drawView = ecs_world_view_t(world, DrawReadView);
  for (EcsIterator* itr = ecs_view_itr(drawView); ecs_view_walk(itr);) {
    const RendDrawComp* comp = ecs_view_read_t(itr, RendDrawComp);
    if (comp->instCount && comp->graphic) {
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

u32 rend_draw_instance_count(const RendDrawComp* draw) { return draw->instCount; }

u32 rend_draw_data_size(const RendDrawComp* draw) { return draw->dataSize; }

u32 rend_draw_data_inst_size(const RendDrawComp* draw) { return draw->instDataSize; }

bool rend_draw_gather(RendDrawComp* draw, const RendView* view, const RendSettingsComp* settings) {
  if (draw->cameraFilter && view->camera != draw->cameraFilter) {
    return false;
  }
  if (draw->flags & RendDrawFlags_NoInstanceFiltering) {
    /**
     * If we can skip the instance filtering, we can also skip the memory copy that is needed to
     * keep the instances contiguous in memory.
     */
    return draw->instCount != 0;
  }

  /**
   * Gather the actual draws after filtering.
   * Because we need the output data to be contiguous in memory we have to copy the instances that
   * pass the filter to separate output memory.
   */

  rend_draw_ensure_storage(
      &draw->instDataOutput, draw->instCount * draw->instDataSize, rend_min_align);

  draw->outputInstCount = 0;
  for (u32 i = 0; i != draw->instCount; ++i) {
    const SceneTags instTags = ((SceneTags*)draw->instTagsMem.ptr)[i];
    const GeoBox*   instAabb = &((GeoBox*)draw->instAabbMem.ptr)[i];
    if (!rend_view_visible(view, instTags, instAabb, settings)) {
      continue;
    }
    const Mem outputMem = rend_draw_inst_output_data(draw, draw->outputInstCount++);
    mem_cpy(outputMem, rend_draw_inst_data(draw, i));
  }
  return draw->outputInstCount != 0;
}

RvkPassDraw rend_draw_output(const RendDrawComp* draw, RvkGraphic* graphic) {
  u32 instCount;
  Mem instData;
  if (draw->flags & RendDrawFlags_NoInstanceFiltering) {
    instCount = draw->instCount;
    instData  = mem_slice(draw->instDataMem, 0, instCount * draw->instDataSize);
  } else {
    instCount = draw->outputInstCount;
    instData  = mem_slice(draw->instDataOutput, 0, instCount * draw->instDataSize);
  }
  return (RvkPassDraw){
      .graphic             = graphic,
      .vertexCountOverride = draw->vertexCountOverride,
      .drawData            = mem_slice(draw->dataMem, 0, draw->dataSize),
      .instCount           = instCount,
      .instData            = instData,
      .instDataStride      = draw->instDataSize,
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

void rend_draw_set_data(RendDrawComp* draw, const Mem data) {
  rend_draw_ensure_storage(&draw->dataMem, data.size, rend_min_align);
  mem_cpy(draw->dataMem, data);
  draw->dataSize = (u32)data.size;
}

void rend_draw_add_instance(
    RendDrawComp* draw, const Mem data, const SceneTags tags, const GeoBox aabb) {

  if (UNLIKELY(bits_align(data.size, rend_min_align) != draw->instDataSize)) {
    /**
     * Instance data-size changed; Clear any previously added instances.
     */
    draw->instCount    = 0;
    draw->instDataSize = bits_align((u32)data.size, rend_min_align);
  }

  ++draw->instCount;
  rend_draw_ensure_storage(
      &draw->instDataMem, draw->instCount * draw->instDataSize, rend_min_align);

  mem_cpy(rend_draw_inst_data(draw, draw->instCount - 1), data);

  if (!(draw->flags & RendDrawFlags_NoInstanceFiltering)) {
    rend_draw_ensure_storage(&draw->instTagsMem, draw->instCount * sizeof(SceneTags), 1);
    rend_draw_ensure_storage(&draw->instAabbMem, draw->instCount * sizeof(GeoBox), alignof(GeoBox));

    ((SceneTags*)draw->instTagsMem.ptr)[draw->instCount - 1] = tags;
    ((GeoBox*)draw->instAabbMem.ptr)[draw->instCount - 1]    = aabb;
  }
}
