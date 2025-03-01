#pragma once
#include "core_alloc.h"
#include "vulkan_api.h"

#include "forward_internal.h"

typedef struct sRvkMemPool  RvkMemPool;
typedef struct sRvkMemChunk RvkMemChunk;

typedef enum {
  RvkMemLoc_Host, // Can be written to from the cpu side.
  RvkMemLoc_Dev,  // Memory on the gpu itself, memory need to be explicitly transferred.
} RvkMemLoc;

typedef enum {
  RvkMemAccess_Linear,    // Normal memory (for example buffers).
  RvkMemAccess_NonLinear, // Images that use a tiling mode different then 'VK_IMAGE_TILING_LINEAR'.
} RvkMemAccess;

typedef struct {
  RvkMemChunk* chunk;
  u32          offset, size;
} RvkMem;

#define rvk_mem_valid(_MEM_) ((_MEM_).chunk != null)

// clang-format off

RvkMemPool* rvk_mem_pool_create(RvkDevice*, VkPhysicalDeviceMemoryProperties, VkPhysicalDeviceLimits);
void        rvk_mem_pool_destroy(RvkMemPool*);

// clang-format on

RvkMem rvk_mem_alloc_req(RvkMemPool*, RvkMemLoc, RvkMemAccess, VkMemoryRequirements);
RvkMem rvk_mem_alloc(RvkMemPool*, RvkMemLoc, RvkMemAccess, u32 size, u32 align, u32 mask);
void   rvk_mem_free(RvkMem);
void   rvk_mem_bind_buffer(RvkMem, VkBuffer);
void   rvk_mem_bind_image(RvkMem, VkImage);
Mem    rvk_mem_map(RvkMem);

typedef struct {
  RvkMem mem;
  u32    offset, size;
} RvkMemFlush;

void rvk_mem_flush(RvkMem mem, u32 offset, u32 size);
void rvk_mem_flush_batch(const RvkMemFlush[], u32 count); // NOTE: Have to be from the same pool.

u64 rvk_mem_occupied(const RvkMemPool*, RvkMemLoc);
u64 rvk_mem_reserved(const RvkMemPool*, RvkMemLoc);
u16 rvk_mem_chunks(const RvkMemPool*);

/**
 * AllocationCallbacks for Vulkan to allocate host memory using the given allocator.
 */
VkAllocationCallbacks rvk_mem_allocator(Allocator*);
