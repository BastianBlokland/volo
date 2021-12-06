#pragma once
#include "core_string.h"

#include "device_internal.h"
#include "mem_internal.h"

typedef enum {
  RvkBufferKind_DeviceIndex,
  RvkBufferKind_DeviceStorage,
  RvkBufferKind_HostUniform,
  RvkBufferKind_HostTransfer,

  RvkBufferKind_Count,
} RvkBufferKind;

typedef struct sRvkBuffer {
  RvkDevice*    dev;
  RvkMem        mem;
  RvkBufferKind kind;
  VkBuffer      vkBuffer;
} RvkBuffer;

RvkBuffer rvk_buffer_create(RvkDevice*, u64 size, RvkBufferKind);
void      rvk_buffer_destroy(RvkBuffer*);
void      rvk_buffer_upload(RvkBuffer*, Mem data, u64 offset);
String    rvk_buffer_kind_str(RvkBufferKind);
