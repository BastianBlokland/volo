#include "core_bits.h"
#include "core_math.h"

#include "mem_internal.h"

/**
 * Api for Vulkan to allocate host memory.
 *
 * Unfortunately Vulkan doesn't track memory sizes for allocations so we need to track those
 * ourselves, as the Volo memory allocators expect the callers to track allocation sizes.
 *
 * Allocation memory layout:
 * - [PADDING] (padding to satisfy the requested alignment)
 * - RvkAllocMeta (8 bytes)
 * - [PAYLOAD]
 *
 * TODO: Currently the same allocator is used for all Vulkan allocation scopes, potentially we could
 *       choose allocators per scope based on the frequency of re-use.
 */

typedef struct {
  u32 size, padding;
} RvkAllocMeta;

typedef struct {
  Mem           memTotal;
  void*         payloadPtr;
  RvkAllocMeta* meta;
} RvkAllocInfo;

#define alloc_meta_size sizeof(RvkAllocMeta)

static RvkAllocMeta* rvk_alloc_meta_ptr(void* ptr) {
  return (RvkAllocMeta*)bits_ptr_offset(ptr, -(iptr)alloc_meta_size);
}

static Mem rvk_alloc_mem_total(void* ptr) {
  const RvkAllocMeta* meta      = rvk_alloc_meta_ptr(ptr);
  const usize         totalSize = meta->padding + alloc_meta_size + meta->size;
  return mem_create(bits_ptr_offset(meta, -(iptr)meta->padding), totalSize);
}

static Mem rvk_alloc_mem_payload(void* ptr) {
  const RvkAllocMeta* meta = rvk_alloc_meta_ptr(ptr);
  return mem_create(ptr, meta->size);
}

static RvkAllocInfo rvk_alloc_internal(
    Allocator* alloc, const usize size, const usize align, const VkSystemAllocationScope scope) {

  (void)scope;

  const usize padding   = bits_padding(alloc_meta_size, align);
  const usize totalSize = bits_align(alloc_meta_size + padding + size, align);

  Mem mem = alloc_alloc(alloc, totalSize, align);
  if (UNLIKELY(!mem_valid(mem))) {
    return (RvkAllocInfo){0};
  }

  RvkAllocMeta* meta = bits_ptr_offset(mem.ptr, padding);
  *meta              = (RvkAllocMeta){.size = (u32)size, .padding = (u32)padding};
  return (RvkAllocInfo){
      .memTotal   = mem,
      .payloadPtr = bits_ptr_offset(meta, alloc_meta_size),
      .meta       = meta,
  };
}

static void* rvk_alloc_func(
    void* userData, const usize size, const usize align, const VkSystemAllocationScope scope) {

  Allocator* alloc = userData;
  return rvk_alloc_internal(alloc, size, align, scope).payloadPtr;
}

static void* rvk_realloc_func(
    void*                         userData,
    void*                         original,
    const usize                   size,
    const usize                   align,
    const VkSystemAllocationScope scope) {

  Allocator* alloc = userData;

  /**
   * Create a new allocation, copy the original payload and then free the original allocation.
   *
   * TODO: We could round up allocations and when growing small amounts check if it still fits
   *       within the existing allocation.
   * TODO: When shrinking allocations we could implement this as a no-op.
   */

  RvkAllocInfo newAlloc = rvk_alloc_internal(alloc, size, align, scope);
  if (UNLIKELY(newAlloc.payloadPtr == null)) {
    return null;
  }

  if (LIKELY(original)) {
    Mem         orgPayload  = rvk_alloc_mem_payload(original);
    const usize bytesToCopy = math_min(orgPayload.size, size);
    mem_cpy(mem_create(newAlloc.payloadPtr, bytesToCopy), mem_create(orgPayload.ptr, bytesToCopy));

    alloc_free(alloc, rvk_alloc_mem_total(original));
  }
  return newAlloc.payloadPtr;
}

static void rvk_free_func(void* userData, void* memory) {
  if (UNLIKELY(memory == null)) {
    return;
  }
  Allocator* alloc = userData;
  alloc_free(alloc, rvk_alloc_mem_total(memory));
}

VkAllocationCallbacks rvk_mem_allocator(Allocator* alloc) {
  return (VkAllocationCallbacks){
      .pUserData       = alloc,
      .pfnAllocation   = rvk_alloc_func,
      .pfnReallocation = rvk_realloc_func,
      .pfnFree         = rvk_free_func,
  };
}
