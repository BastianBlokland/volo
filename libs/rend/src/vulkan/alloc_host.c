#include "core_bits.h"
#include "core_math.h"

#include "alloc_host_internal.h"

/**
 * Api for Vulkan to allocate host memory.
 *
 * Unfortunately Vulkan doesn't track memory sizes for allocations so we need to track those
 * ourselves, as the Volo memory allocators expect the callers to track allocation sizes.
 *
 * Allocation memory layout:
 * - [PADDING] (padding to satisfy the requested alignment)
 * - RendVkAllocMeta (8 bytes)
 * - [PAYLOAD]
 *
 * TODO: Currently the same allocator is used for all Vulkan allocation scopes, potentially we could
 *       choose allocators per scope based on the frequency of re-use.
 */

typedef struct {
  u32 size, padding;
} RendVkAllocMeta;

typedef struct {
  Mem              memTotal;
  void*            payloadPtr;
  RendVkAllocMeta* meta;
} RendVkAllocInfo;

#define alloc_meta_size sizeof(RendVkAllocMeta)

static RendVkAllocMeta* vk_alloc_meta_ptr(void* ptr) {
  return (RendVkAllocMeta*)bits_ptr_offset(ptr, -alloc_meta_size);
}

static Mem vk_alloc_mem_total(void* ptr) {
  const RendVkAllocMeta* meta      = vk_alloc_meta_ptr(ptr);
  const usize            totalSize = meta->padding + alloc_meta_size + meta->size;
  return mem_create(bits_ptr_offset(meta, -meta->padding), totalSize);
}

static Mem vk_alloc_mem_payload(void* ptr) {
  const RendVkAllocMeta* meta = vk_alloc_meta_ptr(ptr);
  return mem_create(ptr, meta->size);
}

static RendVkAllocInfo vk_alloc_internal(
    Allocator* alloc, const usize size, const usize align, const VkSystemAllocationScope scope) {

  (void)scope;

  const usize padding   = bits_padding(alloc_meta_size, align);
  const usize totalSize = bits_align(alloc_meta_size + padding + size, align);

  Mem mem = alloc_alloc(alloc, totalSize, align);
  if (UNLIKELY(!mem_valid(mem))) {
    return (RendVkAllocInfo){0};
  }

  RendVkAllocMeta* meta = bits_ptr_offset(mem.ptr, padding);
  *meta                 = (RendVkAllocMeta){.size = (u32)size, .padding = (u32)padding};
  return (RendVkAllocInfo){
      .memTotal   = mem,
      .payloadPtr = bits_ptr_offset(meta, alloc_meta_size),
      .meta       = meta,
  };
}

static void* vk_alloc_func(
    void* userData, const usize size, const usize align, const VkSystemAllocationScope scope) {

  Allocator* alloc = userData;
  return vk_alloc_internal(alloc, size, align, scope).payloadPtr;
}

static void* vk_realloc_func(
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

  RendVkAllocInfo newAlloc = vk_alloc_internal(alloc, size, align, scope);
  if (UNLIKELY(newAlloc.payloadPtr == null)) {
    return null;
  }

  if (LIKELY(original)) {
    Mem         orgPayload  = vk_alloc_mem_payload(original);
    const usize bytesToCopy = math_min(orgPayload.size, size);
    mem_cpy(mem_create(newAlloc.payloadPtr, bytesToCopy), mem_create(orgPayload.ptr, bytesToCopy));

    alloc_free(alloc, vk_alloc_mem_total(original));
  }
  return newAlloc.payloadPtr;
}

static void vk_free_func(void* userData, void* memory) {
  if (UNLIKELY(memory == null)) {
    return;
  }
  Allocator* alloc = userData;
  alloc_free(alloc, vk_alloc_mem_total(memory));
}

VkAllocationCallbacks rend_vk_alloc_host_create(Allocator* alloc) {
  return (VkAllocationCallbacks){
      .pUserData       = alloc,
      .pfnAllocation   = vk_alloc_func,
      .pfnReallocation = vk_realloc_func,
      .pfnFree         = vk_free_func,
  };
}
