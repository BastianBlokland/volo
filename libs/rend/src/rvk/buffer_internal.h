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
void      rvk_buffer_upload(RvkBuffer*, Mem data, u64 offset);
String    rvk_buffer_type_str(RvkBufferType);
