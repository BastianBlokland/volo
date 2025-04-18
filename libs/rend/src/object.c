#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_sort.h"
#include "ecs_entity.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "rend_register.h"
#include "trace_tracer.h"

#include "builder_internal.h"
#include "object_internal.h"
#include "reset_internal.h"
#include "resource_internal.h"
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

  RendObjectFlags flags : 16;
  u8              alphaTexIndex; // sentinel_u8 if unused.
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
  alloc_maybe_free(g_allocHeap, comp->dataMem);
  alloc_maybe_free(g_allocHeap, comp->instDataMem);
  alloc_maybe_free(g_allocHeap, comp->instTagsMem);
  alloc_maybe_free(g_allocHeap, comp->instAabbMem);
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

  return ecs_world_add_t(
      world, entity, RendObjectComp, .flags = flags, .alphaTexIndex = sentinel_u8);
}

RendObjectFlags rend_object_flags(const RendObjectComp* obj) { return obj->flags; }

EcsEntityId rend_object_resource(const RendObjectComp* obj, const RendObjectRes id) {
  return obj->resources[id];
}

u32       rend_object_instance_count(const RendObjectComp* obj) { return obj->instCount; }
u32       rend_object_data_size(const RendObjectComp* obj) { return obj->dataSize; }
u32       rend_object_data_inst_size(const RendObjectComp* obj) { return obj->instDataSize; }
SceneTags rend_object_tag_mask(const RendObjectComp* obj) { return obj->tagMask; }
u8        rend_object_alpha_tex_index(const RendObjectComp* obj) { return obj->alphaTexIndex; }

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

static void rend_object_sort(const RendObjectComp* obj, RendObjectSortKey* keys, const u32 count) {
#ifdef VOLO_TRACE
  const bool trace = count > 1000;
  if (trace) {
    trace_begin("rend_object_sort", TraceColor_Blue);
  }
#endif

  CompareFunc compareFunc;
  if (obj->flags & RendObjectFlags_SortBackToFront) {
    compareFunc = rend_object_compare_back_to_front;
  } else if (obj->flags & RendObjectFlags_SortFrontToBack) {
    compareFunc = rend_object_compare_front_to_back;
  } else {
    diag_crash_msg("Unsupported sort mode");
  }
  sort_quicksort_t(keys, keys + count, RendObjectSortKey, compareFunc);

#ifdef VOLO_TRACE
  if (trace) {
    trace_end();
  }
#endif
}

NO_INLINE_HINT static void rend_instances_push_all(const RendObjectComp* obj, RendBuilder* b) {
  const u32 batchSize = rend_builder_draw_instances_batch_size(b, obj->instDataSize);
  for (u32 i = 0; i != obj->instCount;) {
    const u32   count      = math_min(obj->instCount - i, batchSize);
    const usize dataOffset = i * obj->instDataSize;
    const Mem   data       = mem_slice(obj->instDataMem, dataOffset, count * obj->instDataSize);
    mem_cpy(rend_builder_draw_instances(b, obj->instDataSize, count), data);
    i += count;
  }
}

NO_INLINE_HINT static void rend_instances_push_filtered(
    const RendObjectComp* obj, RendBuilder* b, const BitSet filter, const u32 count) {

  const u32 batchMax  = rend_builder_draw_instances_batch_size(b, obj->instDataSize);
  usize     instIndex = bitset_next(filter, 0);
  for (u32 i = 0; i != count;) {
    const u32 batchSize = math_min(count - i, batchMax);
    const u32 batchEnd  = i + batchSize;
    u8*       outputPtr = rend_builder_draw_instances(b, obj->instDataSize, batchSize).ptr;
    for (; i != batchEnd; ++i, outputPtr += obj->instDataSize) {
      const Mem inInstMem = rend_object_inst_data(obj, (u32)instIndex);
      rend_object_memcpy(outputPtr, inInstMem.ptr, inInstMem.size);
      instIndex = bitset_next(filter, instIndex + 1);
    }
  }
  diag_assert(sentinel_check(instIndex));
}

NO_INLINE_HINT static void rend_instances_push_sorted(
    const RendObjectComp* obj, RendBuilder* b, RendObjectSortKey* sortKeys, const u32 count) {

  rend_object_sort(obj, sortKeys, count);

  const u32 batchMax = rend_builder_draw_instances_batch_size(b, obj->instDataSize);
  for (u32 i = 0; i != count;) {
    const u32 batchSize = math_min(count - i, batchMax);
    const u32 batchEnd  = i + batchSize;
    u8*       outputPtr = rend_builder_draw_instances(b, obj->instDataSize, batchSize).ptr;
    for (; i != batchEnd; ++i, outputPtr += obj->instDataSize) {
      const Mem inInstMem = rend_object_inst_data(obj, sortKeys[i].instIndex);
      rend_object_memcpy(outputPtr, inInstMem.ptr, inInstMem.size);
    }
  }
}

void rend_object_draw(
    const RendObjectComp*   obj,
    const RendView*         view,
    const RendSettingsComp* settings,
    RendBuilder*            b) {
  if (!obj->instCount) {
    return;
  }
  if (obj->cameraFilter && view->camera != obj->cameraFilter) {
    return;
  }
  if (obj->dataSize) {
    const Mem dataMem = mem_slice(obj->dataMem, 0, obj->dataSize);
    mem_cpy(rend_builder_draw_data(b, obj->dataSize), dataMem);
  }
  if (obj->vertexCountOverride) {
    rend_builder_draw_vertex_count(b, obj->vertexCountOverride);
  }
  if (obj->flags & RendObjectFlags_NoInstanceFiltering) {
    rend_instances_push_all(obj, b);
    return;
  }

  RendObjectSortKey* sortKeys = null;
  BitSet             filter   = mem_empty;

  if (obj->flags & RendObjectFlags_Sorted) {
    const usize requiredSortMem = obj->instCount * sizeof(RendObjectSortKey);
    if (LIKELY(obj->instCount <= u16_max && requiredSortMem <= alloc_max_size(g_allocScratch))) {
      sortKeys = alloc_array_t(g_allocScratch, RendObjectSortKey, obj->instCount);
    } else {
      log_e(
          "Sorted object instance count exceeds maximum",
          log_param("graphic", ecs_entity_fmt(obj->resources[RendObjectRes_Graphic])),
          log_param("count", fmt_int(obj->instCount)));
    }
  }

  if (!sortKeys) {
    filter = alloc_alloc(g_allocScratch, bits_to_bytes(obj->instCount) + 1, 1);
    bitset_clear_all(filter);
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
      sortKeys[outputIndex] = (RendObjectSortKey){
          .instIndex = (u16)i,
          .viewDist  = rend_view_sort_dist(view, instAabb),
      };
    } else {
      bitset_set(filter, i);
    }
  }

  if (filteredInstCount) {
    if (sortKeys) {
      rend_instances_push_sorted(obj, b, sortKeys, filteredInstCount);
    } else {
      rend_instances_push_filtered(obj, b, filter, filteredInstCount);
    }
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

void rend_object_set_alpha_tex_index(RendObjectComp* obj, const u8 alphaTexIndex) {
  obj->alphaTexIndex = alphaTexIndex;
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
