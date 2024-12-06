#pragma once
#include "core_string.h"

#include "forward_internal.h"
#include "mem_internal.h"

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
Mem rvk_buffer_map(RvkBuffer*, u64 offset);

typedef struct {
  const RvkBuffer* buffer;
  u64              offset, size;
} RvkBufferFlush;

void rvk_buffer_flush(const RvkBuffer*, u64 offset, u64 size);
void rvk_buffer_flush_batch(const RvkBufferFlush[], u32 count);

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
