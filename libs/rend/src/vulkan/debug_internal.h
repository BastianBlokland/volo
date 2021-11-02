#pragma once
#include "rend_color.h"

#include "vulkan_internal.h"

typedef enum {
  RendVkDebugFlags_None    = 0,
  RendVkDebugFlags_Verbose = 1 << 0,
} RendVkDebugFlags;

typedef struct sRendVkDebug RendVkDebug;

RendVkDebug* rend_vk_debug_create(VkInstance, VkDevice, VkAllocationCallbacks*, RendVkDebugFlags);
void         rend_vk_debug_destroy(RendVkDebug*);
void         rend_vk_debug_name(RendVkDebug*, VkObjectType, u64 vkHandle, String name);
void         rend_vk_debug_label_begin(RendVkDebug*, VkCommandBuffer, String name, RendColor);
void         rend_vk_debug_label_end(RendVkDebug*, VkCommandBuffer);

#define dbg_name_queue(_DBG_, _OBJ_, _NAME_)                                                       \
  rend_vk_debug_name((_DBG_), VK_OBJECT_TYPE_QUEUE, (u64)(_OBJ_), string_lit(_NAME_ "_queue"))

#define dbg_name_commandpool(_DBG_, _OBJ_, _NAME_)                                                 \
  rend_vk_debug_name(                                                                              \
      (_DBG_), VK_OBJECT_TYPE_COMMAND_POOL, (u64)(_OBJ_), string_lit(_NAME_ "_commandpool"))
