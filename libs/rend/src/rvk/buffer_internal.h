#pragma once
#include "core_string.h"

#include "device_internal.h"
#include "mem_internal.h"

typedef enum {
  RvkBufferType_DeviceIndex,
  RvkBufferType_DeviceStorage,
  RvkBufferType_HostUniform,
  RvkBufferType_HostTransfer,

  RvkBufferType_Count,
} RvkBufferType;

typedef struct sRvkBuffer {
  RvkDevice*    dev;
  RvkMem        mem;
  RvkBufferType type;
  VkBuffer      vkBuffer;
} RvkBuffer;

RvkBuffer rvk_buffer_create(RvkDevice*, u64 size, RvkBufferType);
void      rvk_buffer_destroy(RvkBuffer*);
void      rvk_buffer_upload(RvkBuffer*, Mem data, u64 offset);
String    rvk_buffer_type_str(RvkBufferType);
