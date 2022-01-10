#pragma once
#include "geo_color.h"

#include "vulkan_internal.h"

typedef enum {
  RvkDebugFlags_None    = 0,
  RvkDebugFlags_Verbose = 1 << 0,
} RvkDebugFlags;

typedef struct sRvkDebug RvkDebug;

RvkDebug* rvk_debug_create(VkInstance, VkDevice, VkAllocationCallbacks*, RvkDebugFlags);
void      rvk_debug_destroy(RvkDebug*);
void      rvk_debug_name(RvkDebug*, VkObjectType, u64 vkHandle, String name);
void      rvk_debug_label_begin_raw(RvkDebug*, VkCommandBuffer, GeoColor, String name);
void      rvk_debug_label_end(RvkDebug*, VkCommandBuffer);

#define rvk_debug_label_begin(_DBG_, _CMD_BUF_, _COLOR_, _LIT_, ...)                               \
  rvk_debug_label_begin_raw((_DBG_), (_CMD_BUF_), (_COLOR_), fmt_write_scratch(_LIT_, __VA_ARGS__))

#define rvk_debug_name_fmt(_DBG_, _OBJ_TYPE_, _OBJ_, _LIT_, ...)                                   \
  rvk_debug_name((_DBG_), (_OBJ_TYPE_), (u64)(_OBJ_), fmt_write_scratch(_LIT_, __VA_ARGS__))

#define rvk_debug_name_queue(_DBG_, _OBJ_, _LIT_, ...)                                             \
  rvk_debug_name_fmt(_DBG_, VK_OBJECT_TYPE_QUEUE, _OBJ_, "queue_" _LIT_, __VA_ARGS__)

#define rvk_debug_name_cmdpool(_DBG_, _OBJ_, _LIT_, ...)                                           \
  rvk_debug_name_fmt(_DBG_, VK_OBJECT_TYPE_COMMAND_POOL, _OBJ_, "cmdpool_" _LIT_, __VA_ARGS__)

#define rvk_debug_name_img(_DBG_, _OBJ_, _LIT_, ...)                                               \
  rvk_debug_name_fmt(_DBG_, VK_OBJECT_TYPE_IMAGE, _OBJ_, "img_" _LIT_, __VA_ARGS__)

#define rvk_debug_name_img_view(_DBG_, _OBJ_, _LIT_, ...)                                          \
  rvk_debug_name_fmt(_DBG_, VK_OBJECT_TYPE_IMAGE_VIEW, _OBJ_, "img_view_" _LIT_, __VA_ARGS__)

#define rvk_debug_name_sampler(_DBG_, _OBJ_, _LIT_, ...)                                           \
  rvk_debug_name_fmt(_DBG_, VK_OBJECT_TYPE_SAMPLER, _OBJ_, "sampler_" _LIT_, __VA_ARGS__)

#define rvk_debug_name_framebuffer(_DBG_, _OBJ_, _LIT_, ...)                                       \
  rvk_debug_name_fmt(_DBG_, VK_OBJECT_TYPE_FRAMEBUFFER, _OBJ_, "framebuffer_" _LIT_, __VA_ARGS__)

#define rvk_debug_name_shader(_DBG_, _OBJ_, _LIT_, ...)                                            \
  rvk_debug_name_fmt(_DBG_, VK_OBJECT_TYPE_SHADER_MODULE, _OBJ_, "shader_" _LIT_, __VA_ARGS__)

#define rvk_debug_name_buffer(_DBG_, _OBJ_, _LIT_, ...)                                            \
  rvk_debug_name_fmt(_DBG_, VK_OBJECT_TYPE_BUFFER, _OBJ_, "buffer_" _LIT_, __VA_ARGS__)

#define rvk_debug_name_pipeline(_DBG_, _OBJ_, _LIT_, ...)                                          \
  rvk_debug_name_fmt(_DBG_, VK_OBJECT_TYPE_PIPELINE, _OBJ_, "pipeline_" _LIT_, __VA_ARGS__)

#define rvk_debug_name_pipeline_layout(_DBG_, _OBJ_, _LIT_, ...)                                   \
  rvk_debug_name_fmt(                                                                              \
      _DBG_, VK_OBJECT_TYPE_PIPELINE_LAYOUT, _OBJ_, "pipeline_layout_" _LIT_, __VA_ARGS__)
