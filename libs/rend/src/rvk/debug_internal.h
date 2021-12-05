#pragma once
#include "rend_color.h"

#include "vulkan_internal.h"

typedef enum {
  RvkDebugFlags_None    = 0,
  RvkDebugFlags_Verbose = 1 << 0,
} RvkDebugFlags;

typedef struct sRvkDebug RvkDebug;

RvkDebug* rvk_debug_create(VkInstance, VkDevice, VkAllocationCallbacks*, RvkDebugFlags);
void      rvk_debug_destroy(RvkDebug*);
void      rvk_debug_name(RvkDebug*, VkObjectType, u64 vkHandle, String name);
void      rvk_debug_label_begin(RvkDebug*, VkCommandBuffer, String name, RendColor);
void      rvk_debug_label_end(RvkDebug*, VkCommandBuffer);

#define rvk_name_queue(_DBG_, _OBJ_, _NAME_)                                                       \
  rvk_debug_name((_DBG_), VK_OBJECT_TYPE_QUEUE, (u64)(_OBJ_), string_lit(_NAME_ "_queue"))

#define rvk_name_commandpool(_DBG_, _OBJ_, _NAME_)                                                 \
  rvk_debug_name(                                                                                  \
      (_DBG_), VK_OBJECT_TYPE_COMMAND_POOL, (u64)(_OBJ_), string_lit(_NAME_ "_commandpool"))
