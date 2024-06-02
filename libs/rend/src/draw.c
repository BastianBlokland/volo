#include "asset_manager.h"
#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_sort.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "rend_register.h"
#include "trace_tracer.h"

#include "draw_internal.h"
#include "reset_internal.h"
#include "resource_internal.h"
#include "rvk/texture_internal.h"

#if defined(VOLO_MSVC)
#include <string.h>
#pragma intrinsic(memcpy)
#define intrinsic_memcpy memcpy
#else
#define intrinsic_memcpy __builtin_memcpy
#endif

#define rend_min_align 16
#define rend_max_res_requests 16

typedef struct {
  u32 instIndex;
  f32 distSqr;
} RendDrawSortKey;

ecs_comp_define(RendDrawComp) {
  EcsEntityId resources[RendDrawResource_Count];
  EcsEntityId cameraFilter;

  RendDrawFlags flags;
  u32           vertexCountOverride;
  u32           instCount;
  u32           outputInstCount;

  SceneTags tagMask;

  u32 dataSize;     // Size of the 'per draw' data.
  u32 instDataSize; // Size of the 'per instance' data.

  Mem dataMem;
  Mem instDataMem, instTagsMem, instAabbMem;
  Mem sortKeyMem; // RendDrawSortKey[].
  Mem instDataOutput;
};

static void ecs_destruct_draw(void* data) {
  RendDrawComp* comp = data;
  if (mem_valid(comp->dataMem)) {
    alloc_free(g_allocHeap, comp->dataMem);
  }
  if (mem_valid(comp->instDataMem)) {
    alloc_free(g_allocHeap, comp->instDataMem);
  }
  if (mem_valid(comp->instTagsMem)) {
    alloc_free(g_allocHeap, comp->instTagsMem);
  }
  if (mem_valid(comp->instAabbMem)) {
    alloc_free(g_allocHeap, comp->instAabbMem);
  }
  if (mem_valid(comp->sortKeyMem)) {
    alloc_free(g_allocHeap, comp->sortKeyMem);
  }
  if (mem_valid(comp->instDataOutput)) {
    alloc_free(g_allocHeap, comp->instDataOutput);
  }
}

static void ecs_combine_draw(void* dataA, void* dataB) {
  RendDrawComp* drawA = dataA;
  RendDrawComp* drawB = dataB;
  diag_assert_msg(drawA->flags == drawB->flags, "Only draws with the same flags can be combined");
  diag_assert_msg(
      drawA->instDataSize == drawB->instDataSize,
      "Only draws with the same instance-data stride can be combined");

  for (u32 i = 0; i != drawB->instCount; ++i) {
    const Mem data = mem_slice(drawB->instDataMem, drawB->instDataSize * i, drawB->instDataSize);

    SceneTags tags;
    GeoBox    aabb;
    if (drawB->flags & RendDrawFlags_NoInstanceFiltering) {
      tags = 0;
      aabb = geo_box_inverted3();
    } else {
      tags = mem_as_t(drawB->instTagsMem, SceneTags)[i];
      aabb = mem_as_t(drawB->instAabbMem, GeoBox)[i];
    }

    const Mem newData = rend_draw_add_instance(drawA, data.size, tags, aabb);
    intrinsic_memcpy(newData.ptr, data.ptr, data.size);
  }

  ecs_destruct_draw(drawB);
}

INLINE_HINT static void buf_ensure(Mem* mem, const usize size, const usize align) {
  if (UNLIKELY(mem->size < size)) {
    const Mem newMem = alloc_alloc(g_allocHeap, bits_nextpow2(size), align);
    if (mem_valid(*mem)) {
      mem_cpy(newMem, *mem);
      alloc_free(g_allocHeap, *mem);
    }
    *mem = newMem;
  }
}

INLINE_HINT static u32 rend_draw_align(const u32 val, const u32 align) {
  const u32 rem = val & (align - 1);
  return val + (rem ? align - rem : 0);
}

static Mem rend_draw_inst_data(const RendDrawComp* draw, const u32 instance) {
  const usize offset = instance * draw->instDataSize;
  return mem_create(bits_ptr_offset(draw->instDataMem.ptr, offset), draw->instDataSize);
}

static Mem rend_draw_inst_output_data(const RendDrawComp* draw, const u32 instance) {
  const usize offset = instance * draw->instDataSize;
  return mem_create(bits_ptr_offset(draw->instDataOutput.ptr, offset), draw->instDataSize);
}

static void
rend_draw_copy_to_output(const RendDrawComp* draw, const u32 instIndex, const u32 outputIndex) {
  const Mem outputMem   = rend_draw_inst_output_data(draw, outputIndex);
  const Mem instDataMem = rend_draw_inst_data(draw, instIndex);
  intrinsic_memcpy(outputMem.ptr, instDataMem.ptr, instDataMem.size);
}

static bool rend_resource_asset_valid(EcsWorld* world, const EcsEntityId assetEntity) {
  return ecs_world_exists(world, assetEntity) && ecs_world_has_t(world, assetEntity, AssetComp);
}

/**
 * Request the given resource to be loaded.
 */
static void rend_draw_resource_request(
    EcsWorld* world, const EcsEntityId entity, EcsIterator* resItr, u32* numRequests) {
  /**
   * If the resource is already loaded then tell the resource system we're still using it (so it
   * won't be unloaded). If its not loaded then start loading it.
   */
  if (LIKELY(ecs_view_maybe_jump(resItr, entity))) {
    rend_res_mark_used(ecs_view_write_t(resItr, RendResComp));
    return;
  }

  if (++*numRequests < rend_max_res_requests) {
    if (LIKELY(rend_resource_asset_valid(world, entity))) {
      rend_res_request(world, entity);
    } else {
      log_e("Invalid draw resource asset entity");
    }
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
      rend_draw_clear(drawComp);
    }
  }
}

ecs_system_define(RendDrawResourceRequestSys) {
  if (rend_will_reset(world)) {
    return;
  }

  u32 numRequests = 0;

  EcsIterator* resItr = ecs_view_itr(ecs_world_view_t(world, ResourceView));

  // Request the resources for all draw's to be loaded.
  EcsView* drawView = ecs_world_view_t(world, DrawReadView);
  for (EcsIterator* itr = ecs_view_itr(drawView); ecs_view_walk(itr);) {
    const RendDrawComp* comp = ecs_view_read_t(itr, RendDrawComp);
    if (!comp->instCount && !(comp->flags & RendDrawFlags_Preload)) {
      continue; // Draw unused and not required to be pre-loaded.
    }
    for (u32 i = 0; i != RendDrawResource_Count; ++i) {
      if (comp->resources[i]) {
        rend_draw_resource_request(world, comp->resources[i], resItr, &numRequests);
      }
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
      RendDrawResourceRequestSys, ecs_view_id(DrawReadView), ecs_view_id(ResourceView));

  ecs_order(RendClearDrawsSys, RendOrder_DrawClear);
  ecs_order(RendDrawResourceRequestSys, RendOrder_DrawCollect + 10);
}

RendDrawComp*
rend_draw_create(EcsWorld* world, const EcsEntityId entity, const RendDrawFlags flags) {
  MAYBE_UNUSED const bool noFiltering = (flags & RendDrawFlags_NoInstanceFiltering) != 0;
  MAYBE_UNUSED const bool isSorted    = (flags & RendDrawFlags_Sorted) != 0;
  diag_assert_msg(noFiltering ? !isSorted : true, "NoInstanceFiltering incompatible with sorting");

  return ecs_world_add_t(world, entity, RendDrawComp, .flags = flags);
}

RendDrawFlags rend_draw_flags(const RendDrawComp* draw) { return draw->flags; }

EcsEntityId rend_draw_resource(const RendDrawComp* draw, const RendDrawResource id) {
  return draw->resources[id];
}

u32       rend_draw_instance_count(const RendDrawComp* draw) { return draw->instCount; }
u32       rend_draw_data_size(const RendDrawComp* draw) { return draw->dataSize; }
u32       rend_draw_data_inst_size(const RendDrawComp* draw) { return draw->instDataSize; }
SceneTags rend_draw_tag_mask(const RendDrawComp* draw) { return draw->tagMask; }

static RendDrawSortKey* rend_draw_sort_key(const RendDrawComp* draw, const u32 outputIndex) {
  return bits_ptr_offset(draw->sortKeyMem.ptr, outputIndex * sizeof(RendDrawSortKey));
}

static i8 rend_draw_compare_back_to_front(const void* a, const void* b) {
  const f32 distA = *field_ptr(a, RendDrawSortKey, distSqr);
  const f32 distB = *field_ptr(b, RendDrawSortKey, distSqr);
  return distA > distB ? -1 : distA < distB ? 1 : 0;
}

static i8 rend_draw_compare_front_to_back(const void* a, const void* b) {
  const f32 distA = *field_ptr(a, RendDrawSortKey, distSqr);
  const f32 distB = *field_ptr(b, RendDrawSortKey, distSqr);
  return distA < distB ? -1 : distA > distB ? 1 : 0;
}

static void rend_draw_sort(RendDrawComp* draw) {
  CompareFunc compareFunc;
  if (draw->flags & RendDrawFlags_SortBackToFront) {
    compareFunc = rend_draw_compare_back_to_front;
  } else if (draw->flags & RendDrawFlags_SortFrontToBack) {
    compareFunc = rend_draw_compare_front_to_back;
  } else {
    diag_crash_msg("Unsupported sort mode");
  }

  void* keysBegin = mem_begin(draw->sortKeyMem);
  void* keysEnd   = bits_ptr_offset(keysBegin, sizeof(RendDrawSortKey) * draw->outputInstCount);
  sort_quicksort(keysBegin, keysEnd, sizeof(RendDrawSortKey), compareFunc);
}

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

  buf_ensure(&draw->instDataOutput, draw->instCount * draw->instDataSize, rend_min_align);

  if (draw->flags & RendDrawFlags_Sorted) {
    buf_ensure(
        &draw->sortKeyMem, draw->instCount * sizeof(RendDrawSortKey), alignof(RendDrawSortKey));
  }

  draw->outputInstCount = 0;
  for (u32 i = 0; i != draw->instCount; ++i) {
    const SceneTags instTags = ((SceneTags*)draw->instTagsMem.ptr)[i];
    const GeoBox*   instAabb = &((GeoBox*)draw->instAabbMem.ptr)[i];
    if (!rend_view_visible(view, instTags, instAabb, settings)) {
      continue;
    }
    const u32 outputIndex = draw->outputInstCount++;
    if (draw->flags & RendDrawFlags_Sorted) {
      /**
       * Instead of outputting the instance directly, first create a sort key for it. Then in a
       * separate pass sort the instances and copy them to the output.
       */
      *rend_draw_sort_key(draw, outputIndex) = (RendDrawSortKey){
          .instIndex = i,
          .distSqr   = rend_view_dist_sqr(view, instAabb),
      };
    } else {
      rend_draw_copy_to_output(draw, i, outputIndex);
    }
  }

  if (draw->flags & RendDrawFlags_Sorted) {
    // clang-format off
#ifdef VOLO_TRACE
    const bool trace = draw->outputInstCount > 1000;
    if (trace) { trace_begin("rend_draw_sort", TraceColor_Blue); }
#endif
    rend_draw_sort(draw);
    for (u32 i = 0; i != draw->outputInstCount; ++i) {
      const RendDrawSortKey* sortKey = rend_draw_sort_key(draw, i);
      rend_draw_copy_to_output(draw, sortKey->instIndex, i);
    }
#ifdef VOLO_TRACE
    if (trace) { trace_end(); }
#endif
    // clang-format on
  }
  return draw->outputInstCount != 0;
}

RvkPassDraw rend_draw_output(const RendDrawComp* draw, RvkGraphic* graphic, RvkTexture* texture) {
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
      .drawImage           = texture ? &texture->image : null,
      .drawSampler         = {0}, // TODO: Support customizing per-draw texture sampling.
      .instCount           = instCount,
      .instData            = instData,
      .instDataStride      = draw->instDataSize,
  };
}

void rend_draw_set_resource(
    RendDrawComp* comp, const RendDrawResource id, const EcsEntityId asset) {
  comp->resources[id] = asset;
}

void rend_draw_set_camera_filter(RendDrawComp* comp, const EcsEntityId camera) {
  comp->cameraFilter = camera;
}

void rend_draw_set_vertex_count(RendDrawComp* comp, const u32 vertexCount) {
  comp->vertexCountOverride = vertexCount;
}

void rend_draw_clear(RendDrawComp* draw) {
  draw->instCount    = 0;
  draw->instDataSize = 0;
  draw->tagMask      = 0;
}

Mem rend_draw_set_data(RendDrawComp* draw, const usize size) {
  buf_ensure(&draw->dataMem, size, rend_min_align);
  draw->dataSize = (u32)size;
  return draw->dataMem;
}

Mem rend_draw_add_instance(
    RendDrawComp* draw, const usize size, const SceneTags tags, const GeoBox aabb) {

  if (UNLIKELY(!draw->instDataSize)) {
    draw->instDataSize = rend_draw_align((u32)size, rend_min_align);
  }
  diag_assert_msg(size <= draw->instDataSize, "Draw instance-data size mismatch");

  /**
   * Add a new instance and return instance memory for the caller to write into.
   */

  const u32 drawIndex = draw->instCount++;
  buf_ensure(&draw->instDataMem, draw->instCount * draw->instDataSize, rend_min_align);

  draw->tagMask |= tags;

  if (!(draw->flags & RendDrawFlags_NoInstanceFiltering)) {
    buf_ensure(&draw->instTagsMem, draw->instCount * sizeof(SceneTags), 1);
    buf_ensure(&draw->instAabbMem, draw->instCount * sizeof(GeoBox), alignof(GeoBox));

    ((SceneTags*)draw->instTagsMem.ptr)[drawIndex] = tags;
    ((GeoBox*)draw->instAabbMem.ptr)[drawIndex]    = aabb;
  }

  return rend_draw_inst_data(draw, drawIndex);
}
