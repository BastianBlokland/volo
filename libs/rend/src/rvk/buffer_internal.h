#pragma once
#include "core_string.h"

#include "mem_internal.h"

// Internal forward declarations:
typedef struct sRvkDevice RvkDevice;

typedef enum {
  RvkBufferType_DeviceIndex,
  RvkBufferType_DeviceStorage,
  RvkBufferType_HostUniform,
  RvkBufferType_HostTransfer,

  RvkBufferType_Count,
} RvkBufferType;

typedef struct sRvkBuffer {
  RvkBufferType type;
  u64           size;
  RvkMem        mem;
  VkBuffer      vkBuffer;
} RvkBuffer;

RvkBuffer rvk_buffer_create(RvkDevice*, u64 size, RvkBufferType);
void      rvk_buffer_destroy(RvkBuffer*, RvkDevice*);
String    rvk_buffer_type_str(RvkBufferType);

/**
 * Map the buffer into cpu writable memory.
 * NOTE: Requires the buffer to be host-visible.
 * NOTE: Writes to the mapped memory require a flush to become visible to the driver.
 */
Mem  rvk_buffer_map(RvkBuffer*, const u64 offset);
void rvk_buffer_flush(RvkBuffer*);

/**
 * Copies the given data to the buffer.
 * NOTE: Requires the buffer to be host-visible.
 * NOTE: Automatically performs a buffer flush.
 */
void rvk_buffer_upload(RvkBuffer*, Mem data, u64 offset);

void rvk_buffer_transfer_ownership(
    const RvkBuffer*,
    VkCommandBuffer srcCmdBuf,
    VkCommandBuffer dstCmdBuf,
    u32             srcQueueFamIdx,
    u32             dstQueueFamIdx);
