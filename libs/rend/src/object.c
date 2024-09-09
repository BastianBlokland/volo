#include "asset_manager.h"
#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_sort.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "rend_register.h"
#include "trace_tracer.h"

#include "builder_internal.h"
#include "object_internal.h"
#include "reset_internal.h"
#include "resource_internal.h"
#include "rvk/texture_internal.h"
#include "view_internal.h"

#ifdef VOLO_SIMD
#include "core_simd.h"
#endif

#define rend_min_align 16
#define rend_max_res_requests 16

typedef struct {
  u16 instIndex;
  u16 viewDist; // Not linear.
} RendObjectSortKey;

ecs_comp_define(RendObjectComp) {
  EcsEntityId resources[RendObjectRes_Count];
  EcsEntityId cameraFilter;

  RendObjectFlags flags;
  u32             vertexCountOverride;
  u32             instCount;

  SceneTags tagMask;

  u32 dataSize;     // Size of the 'per draw' data.
  u32 instDataSize; // Size of the 'per instance' data.

  Mem dataMem;
  Mem instDataMem, instTagsMem, instAabbMem;
};

/**
 * Pre-condition: bits_is_aligned(size, 16)
 */
INLINE_HINT static void rend_object_memcpy(u8* dst, const u8* src, const usize size) {
#ifdef VOLO_SIMD
  const void* end = bits_ptr_offset(src, size);
  for (; src != end; src += 16, dst += 16) {
    simd_copy_128(dst, src);
  }
#else
  mem_cpy(mem_create(dst, size), mem_create(src, size));
#endif
}

static void ecs_destruct_object(void* data) {
  RendObjectComp* comp = data;
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
}

static void ecs_combine_object(void* dataA, void* dataB) {
  RendObjectComp* objA = dataA;
  RendObjectComp* objB = dataB;
  diag_assert_msg(objA->flags == objB->flags, "Only objects with the same flags can be combined");
  diag_assert_msg(
      objA->instDataSize == objB->instDataSize,
      "Only objects with the same instance-data stride can be combined");

  for (u32 i = 0; i != objB->instCount; ++i) {
    const Mem data = mem_slice(objB->instDataMem, objB->instDataSize * i, objB->instDataSize);

    SceneTags tags;
    GeoBox    aabb;
    if (objB->flags & RendObjectFlags_NoInstanceFiltering) {
      tags = 0;
      aabb = geo_box_inverted3();
    } else {
      tags = mem_as_t(objB->instTagsMem, SceneTags)[i];
      aabb = mem_as_t(objB->instAabbMem, GeoBox)[i];
    }

    const Mem newData = rend_object_add_instance(objA, data.size, tags, aabb);
    rend_object_memcpy(newData.ptr, data.ptr, data.size);
  }

  ecs_destruct_object(objB);
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

INLINE_HINT static u32 rend_object_align(const u32 val, const u32 align) {
  const u32 rem = val & (align - 1);
  return val + (rem ? align - rem : 0);
}

static Mem rend_object_inst_data(const RendObjectComp* obj, const u32 instance) {
  const usize offset = instance * obj->instDataSize;
  return mem_create(bits_ptr_offset(obj->instDataMem.ptr, offset), obj->instDataSize);
}

static void rend_object_copy_to_output(
    const RendObjectComp* obj, const u32 instIndex, const u32 outIndex, const Mem outMem) {
  const usize outOffset  = outIndex * obj->instDataSize;
  const Mem   outInstMem = mem_create(bits_ptr_offset(outMem.ptr, outOffset), obj->instDataSize);
  const Mem   inInstMem  = rend_object_inst_data(obj, instIndex);
  rend_object_memcpy(outInstMem.ptr, inInstMem.ptr, inInstMem.size);
}

static bool rend_resource_asset_valid(EcsWorld* world, const EcsEntityId assetEntity) {
  return ecs_world_exists(world, assetEntity) && ecs_world_has_t(world, assetEntity, AssetComp);
}

/**
 * Request the given resource to be loaded.
 */
static void rend_object_resource_request(
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
      log_e("Invalid object resource asset entity", log_param("entity", ecs_entity_fmt(entity)));
    }
  }
}

ecs_view_define(ResourceView) { ecs_access_write(RendResComp); }
ecs_view_define(ObjectReadView) { ecs_access_read(RendObjectComp); }
ecs_view_define(ObjectWriteView) { ecs_access_write(RendObjectComp); }

ecs_system_define(RendClearObjectsSys) {
  EcsView* objView = ecs_world_view_t(world, ObjectWriteView);
  for (EcsIterator* itr = ecs_view_itr(objView); ecs_view_walk(itr);) {
    RendObjectComp* obj = ecs_view_write_t(itr, RendObjectComp);
    if (!(obj->flags & RendObjectFlags_NoAutoClear)) {
      rend_object_clear(obj);
    }
  }
}

ecs_system_define(RendObjectResourceRequestSys) {
  if (rend_will_reset(world)) {
    return;
  }

  u32 numRequests = 0;

  EcsIterator* resItr = ecs_view_itr(ecs_world_view_t(world, ResourceView));

  // Request the resources for all objects to be loaded.
  EcsView* objView = ecs_world_view_t(world, ObjectReadView);
  for (EcsIterator* itr = ecs_view_itr(objView); ecs_view_walk(itr);) {
    const RendObjectComp* comp = ecs_view_read_t(itr, RendObjectComp);
    if (!comp->instCount && !(comp->flags & RendObjectFlags_Preload)) {
      continue; // Object unused and not required to be pre-loaded.
    }
    for (u32 i = 0; i != RendObjectRes_Count; ++i) {
      if (comp->resources[i]) {
        rend_object_resource_request(world, comp->resources[i], resItr, &numRequests);
      }
    }
  }
}

ecs_module_init(rend_object_module) {
  ecs_register_comp(
      RendObjectComp, .destructor = ecs_destruct_object, .combinator = ecs_combine_object);

  ecs_register_view(ResourceView);
  ecs_register_view(ObjectReadView);
  ecs_register_view(ObjectWriteView);

  ecs_register_system(RendClearObjectsSys, ecs_view_id(ObjectWriteView));
  ecs_register_system(
      RendObjectResourceRequestSys, ecs_view_id(ObjectReadView), ecs_view_id(ResourceView));

  ecs_order(RendClearObjectsSys, RendOrder_ObjectClear);
  ecs_order(RendObjectResourceRequestSys, RendOrder_ObjectUpdate + 10);
}

RendObjectComp*
rend_object_create(EcsWorld* world, const EcsEntityId entity, const RendObjectFlags flags) {
  MAYBE_UNUSED const bool noFiltering = (flags & RendObjectFlags_NoInstanceFiltering) != 0;
  MAYBE_UNUSED const bool isSorted    = (flags & RendObjectFlags_Sorted) != 0;
  diag_assert_msg(noFiltering ? !isSorted : true, "NoInstanceFiltering incompatible with sorting");

  return ecs_world_add_t(world, entity, RendObjectComp, .flags = flags);
}

RendObjectFlags rend_object_flags(const RendObjectComp* obj) { return obj->flags; }

EcsEntityId rend_object_resource(const RendObjectComp* obj, const RendObjectRes id) {
  return obj->resources[id];
}

u32       rend_object_instance_count(const RendObjectComp* obj) { return obj->instCount; }
u32       rend_object_data_size(const RendObjectComp* obj) { return obj->dataSize; }
u32       rend_object_data_inst_size(const RendObjectComp* obj) { return obj->instDataSize; }
SceneTags rend_object_tag_mask(const RendObjectComp* obj) { return obj->tagMask; }

static i8 rend_object_compare_back_to_front(const void* a, const void* b) {
  const u16 distA = *field_ptr(a, RendObjectSortKey, viewDist);
  const u16 distB = *field_ptr(b, RendObjectSortKey, viewDist);
  return distA > distB ? -1 : distA < distB ? 1 : 0;
}

static i8 rend_object_compare_front_to_back(const void* a, const void* b) {
  const u16 distA = *field_ptr(a, RendObjectSortKey, viewDist);
  const u16 distB = *field_ptr(b, RendObjectSortKey, viewDist);
  return distA < distB ? -1 : distA > distB ? 1 : 0;
}

static void
rend_object_sort(const RendObjectComp* obj, RendObjectSortKey* sortKeys, const u32 count) {
  CompareFunc compareFunc;
  if (obj->flags & RendObjectFlags_SortBackToFront) {
    compareFunc = rend_object_compare_back_to_front;
  } else if (obj->flags & RendObjectFlags_SortFrontToBack) {
    compareFunc = rend_object_compare_front_to_back;
  } else {
    diag_crash_msg("Unsupported sort mode");
  }
  sort_quicksort_t(sortKeys, sortKeys + count, RendObjectSortKey, compareFunc);
}

void rend_object_draw(
    const RendObjectComp*   obj,
    const RendView*         view,
    const RendSettingsComp* settings,
    RendBuilderBuffer*      builder) {
  if (!obj->instCount) {
    return;
  }
  if (obj->cameraFilter && view->camera != obj->cameraFilter) {
    return;
  }
  if (obj->dataSize) {
    const Mem drawMem = mem_slice(obj->dataMem, 0, obj->dataSize);
    rend_builder_draw_data_extern(builder, drawMem);
  }
  if (obj->vertexCountOverride) {
    rend_builder_draw_vertex_count(builder, obj->vertexCountOverride);
  }
  if (obj->flags & RendObjectFlags_NoInstanceFiltering) {
    /**
     * Without instance filtering we can skip the memory copy that is needed to keep the instances
     * contiguous in memory.
     */
    const Mem instMem = mem_slice(obj->instDataMem, 0, obj->instCount * obj->instDataSize);
    rend_builder_draw_instances_extern(builder, obj->instCount, instMem, obj->instDataSize);
    return;
  }

  Mem outputMem;

  RendObjectSortKey* sortKeys = null;
  if (obj->flags & RendObjectFlags_Sorted) {
    const usize requiredSortMem = obj->instCount * sizeof(RendObjectSortKey);
    if (UNLIKELY(obj->instCount > u16_max || requiredSortMem > alloc_max_size(g_allocScratch))) {
      log_e(
          "Sorted object instance count exceeds maximum",
          log_param("graphic", ecs_entity_fmt(obj->resources[RendObjectRes_Graphic])),
          log_param("count", fmt_int(obj->instCount)));
      return;
    }
    sortKeys = alloc_array_t(g_allocScratch, RendObjectSortKey, obj->instCount);
  } else {
    // Not sorted; output in a single pass by allocating the max amount and then trimming.
    outputMem = rend_builder_draw_instances(builder, obj->instCount, obj->instDataSize);
  }

  u32 filteredInstCount = 0;
  for (u32 i = 0; i != obj->instCount; ++i) {
    const SceneTags instTags = ((SceneTags*)obj->instTagsMem.ptr)[i];
    const GeoBox*   instAabb = &((GeoBox*)obj->instAabbMem.ptr)[i];
    if (!rend_view_visible(view, instTags, instAabb, settings)) {
      continue;
    }
    const u32 outputIndex = filteredInstCount++;
    if (sortKeys) {
      /**
       * Instead of outputting the instance directly, first create a sort key for it. Then in a
       * separate pass sort the instances and copy them to the output.
       */
      sortKeys[outputIndex] = (RendObjectSortKey){
          .instIndex = (u16)i,
          .viewDist  = rend_view_sort_dist(view, instAabb),
      };
    } else {
      rend_object_copy_to_output(obj, i, outputIndex, outputMem);
    }
  }

  if (!sortKeys) {
    rend_builder_draw_instances_trim(builder, filteredInstCount);
  }

  if (sortKeys && filteredInstCount) {
    // clang-format off
#ifdef VOLO_TRACE
    const bool trace = filteredInstCount > 1000;
    if (trace) { trace_begin("rend_object_sort", TraceColor_Blue); }
#endif
    outputMem = rend_builder_draw_instances(builder, filteredInstCount, obj->instDataSize);
    rend_object_sort(obj, sortKeys, filteredInstCount);
    for (u32 i = 0; i != filteredInstCount; ++i) {
      rend_object_copy_to_output(obj, sortKeys[i].instIndex, i, outputMem);
    }
#ifdef VOLO_TRACE
    if (trace) { trace_end(); }
#endif
    // clang-format on
  }
}

void rend_object_set_resource(
    RendObjectComp* obj, const RendObjectRes id, const EcsEntityId asset) {
  obj->resources[id] = asset;
}

void rend_object_set_camera_filter(RendObjectComp* obj, const EcsEntityId camera) {
  obj->cameraFilter = camera;
}

void rend_object_set_vertex_count(RendObjectComp* obj, const u32 vertexCount) {
  obj->vertexCountOverride = vertexCount;
}

void rend_object_clear(RendObjectComp* obj) {
  obj->instCount    = 0;
  obj->instDataSize = 0;
  obj->tagMask      = 0;
}

Mem rend_object_set_data(RendObjectComp* obj, const usize size) {
  buf_ensure(&obj->dataMem, size, rend_min_align);
  obj->dataSize = (u32)size;
  return obj->dataMem;
}

Mem rend_object_add_instance(
    RendObjectComp* obj, const usize size, const SceneTags tags, const GeoBox aabb) {

  if (UNLIKELY(!obj->instDataSize)) {
    obj->instDataSize = rend_object_align((u32)size, rend_min_align);
  }
  diag_assert_msg(size <= obj->instDataSize, "Object instance-data size mismatch");

  /**
   * Add a new instance and return instance memory for the caller to write into.
   */

  const u32 instIndex = obj->instCount++;
  buf_ensure(&obj->instDataMem, obj->instCount * obj->instDataSize, rend_min_align);

  obj->tagMask |= tags;

  if (!(obj->flags & RendObjectFlags_NoInstanceFiltering)) {
    buf_ensure(&obj->instTagsMem, obj->instCount * sizeof(SceneTags), 1);
    buf_ensure(&obj->instAabbMem, obj->instCount * sizeof(GeoBox), alignof(GeoBox));

    ((SceneTags*)obj->instTagsMem.ptr)[instIndex] = tags;
    ((GeoBox*)obj->instAabbMem.ptr)[instIndex]    = aabb;
  }

  return rend_object_inst_data(obj, instIndex);
}
