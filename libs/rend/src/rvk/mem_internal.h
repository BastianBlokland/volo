#pragma once
#include "core_alloc.h"

#include "vulkan_internal.h"

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

RvkMemPool* rvk_mem_pool_create(VkDevice, VkPhysicalDeviceMemoryProperties, VkPhysicalDeviceLimits);
void        rvk_mem_pool_destroy(RvkMemPool*);

RvkMem rvk_mem_alloc_req(RvkMemPool*, RvkMemLoc, RvkMemAccess, VkMemoryRequirements);
RvkMem rvk_mem_alloc(RvkMemPool*, RvkMemLoc, RvkMemAccess, u32 size, u32 align, u32 mask);
void   rvk_mem_free(RvkMem);
void   rvk_mem_bind_buffer(RvkMem, VkBuffer);
void   rvk_mem_bind_image(RvkMem, VkImage);
Mem    rvk_mem_map(RvkMem);
void   rvk_mem_flush(RvkMem);

u64 rvk_mem_occupied(const RvkMemPool*);
u64 rvk_mem_reserved(const RvkMemPool*);

/**
 * AllocationCallbacks for Vulkan to allocate host memory using the given allocator.
 */
VkAllocationCallbacks rvk_mem_allocator(Allocator*);
