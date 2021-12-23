#pragma once
#include "rend_color.h"

#include "vulkan_internal.h"

#define VOLO_REND_OBJECT_NAMING

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

#ifdef VOLO_REND_OBJECT_NAMING
#define rvk_debug_name_obj(_DBG_, _OBJ_TYPE_, _OBJ_, _LIT_, ...)                                   \
  rvk_debug_name((_DBG_), (_OBJ_TYPE_), (u64)(_OBJ_), fmt_write_scratch(_LIT_, __VA_ARGS__))
#else
#define rvk_debug_name_obj(_DBG_, _OBJ_TYPE_, _OBJ_, _LIT_, ...)
#endif

#define rvk_debug_name_queue(_DBG_, _OBJ_, _LIT_, ...)                                             \
  rvk_debug_name_obj(_DBG_, VK_OBJECT_TYPE_QUEUE, _OBJ_, _LIT_ "_queue", __VA_ARGS__)

#define rvk_debug_name_commandpool(_DBG_, _OBJ_, _LIT_, ...)                                       \
  rvk_debug_name_obj(_DBG_, VK_OBJECT_TYPE_COMMAND_POOL, _OBJ_, _LIT_ "_commandpool", __VA_ARGS__)
