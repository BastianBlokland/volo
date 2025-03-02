#pragma once
#include "geo.h"
#include "vulkan_api.h"

#include "forward_internal.h"

typedef enum {
  RvkDeviceFlags_SupportNullDescriptor    = 1 << 0,
  RvkDeviceFlags_SupportPipelineStatQuery = 1 << 1,
  RvkDeviceFlags_SupportAnisotropy        = 1 << 2,
  RvkDeviceFlags_SupportFillNonSolid      = 1 << 3,
  RvkDeviceFlags_SupportWideLines         = 1 << 4,
  RvkDeviceFlags_SupportPresentId         = 1 << 5,
  RvkDeviceFlags_SupportPresentWait       = 1 << 6,
  RvkDeviceFlags_SupportDepthClamp        = 1 << 7,
} RvkDeviceFlags;

typedef struct sRvkDevice {
  RvkDeviceFlags                   flags;
  VkInterfaceDevice                api;
  RvkLib*                          lib;
  VkAllocationCallbacks            vkAlloc;
  VkPhysicalDevice                 vkPhysDev;
  VkPhysicalDeviceProperties       vkProperties;
  VkPhysicalDeviceMemoryProperties vkMemProperties;
  VkDevice                         vkDev;
  VkFormat                         depthFormat, preferredSwapchainFormat;
  ThreadMutex                      queueSubmitMutex;
  u32                              graphicsQueueIndex;
  u32                              transferQueueIndex; // sentinel_u32 if unavailable.
  VkQueue                          vkGraphicsQueue, vkTransferQueue;
  VkPipelineCache                  vkPipelineCache;
  RvkMemPool*                      memPool;
  RvkDescPool*                     descPool;
  RvkSamplerPool*                  samplerPool;
  RvkTransferer*                   transferer;
  RvkRepository*                   repository;
} RvkDevice;

RvkDevice* rvk_device_create(RvkLib*);
void       rvk_device_destroy(RvkDevice*);

bool   rvk_device_format_supported(const RvkDevice*, VkFormat, VkFormatFeatureFlags);
String rvk_device_name(const RvkDevice*);
void   rvk_device_update(RvkDevice*);
void   rvk_device_wait_idle(const RvkDevice*);

void rvk_debug_name(RvkDevice*, VkObjectType, u64 vkHandle, String name);
void rvk_debug_label_begin_raw(RvkDevice*, VkCommandBuffer, GeoColor, String name);
void rvk_debug_label_end(RvkDevice*, VkCommandBuffer);

#define rvk_debug_label_begin(_DEV_, _CMD_BUF_, _COLOR_, _LIT_, ...)                               \
  rvk_debug_label_begin_raw((_DEV_), (_CMD_BUF_), (_COLOR_), fmt_write_scratch(_LIT_, __VA_ARGS__))

#define rvk_debug_name_fmt(_DEV_, _OBJ_TYPE_, _OBJ_, _LIT_, ...)                                   \
  rvk_debug_name((_DEV_), (_OBJ_TYPE_), (u64)(_OBJ_), fmt_write_scratch(_LIT_, __VA_ARGS__))

#define rvk_debug_name_queue(_DEV_, _OBJ_, _LIT_, ...)                                             \
  rvk_debug_name_fmt(_DEV_, VK_OBJECT_TYPE_QUEUE, _OBJ_, "queue_" _LIT_, __VA_ARGS__)

#define rvk_debug_name_semaphore(_DEV_, _OBJ_, _LIT_, ...)                                         \
  rvk_debug_name_fmt(_DEV_, VK_OBJECT_TYPE_SEMAPHORE, _OBJ_, "semaphore_" _LIT_, __VA_ARGS__)

#define rvk_debug_name_fence(_DEV_, _OBJ_, _LIT_, ...)                                             \
  rvk_debug_name_fmt(_DEV_, VK_OBJECT_TYPE_FENCE, _OBJ_, "fence_" _LIT_, __VA_ARGS__)

#define rvk_debug_name_cmdpool(_DEV_, _OBJ_, _LIT_, ...)                                           \
  rvk_debug_name_fmt(_DEV_, VK_OBJECT_TYPE_COMMAND_POOL, _OBJ_, "cmdpool_" _LIT_, __VA_ARGS__)

#define rvk_debug_name_img(_DEV_, _OBJ_, _LIT_, ...)                                               \
  rvk_debug_name_fmt(_DEV_, VK_OBJECT_TYPE_IMAGE, _OBJ_, "img_" _LIT_, __VA_ARGS__)

#define rvk_debug_name_img_view(_DEV_, _OBJ_, _LIT_, ...)                                          \
  rvk_debug_name_fmt(_DEV_, VK_OBJECT_TYPE_IMAGE_VIEW, _OBJ_, "img_view_" _LIT_, __VA_ARGS__)

#define rvk_debug_name_sampler(_DEV_, _OBJ_, _LIT_, ...)                                           \
  rvk_debug_name_fmt(_DEV_, VK_OBJECT_TYPE_SAMPLER, _OBJ_, "sampler_" _LIT_, __VA_ARGS__)

#define rvk_debug_name_framebuffer(_DEV_, _OBJ_, _LIT_, ...)                                       \
  rvk_debug_name_fmt(_DEV_, VK_OBJECT_TYPE_FRAMEBUFFER, _OBJ_, "framebuffer_" _LIT_, __VA_ARGS__)

#define rvk_debug_name_shader(_DEV_, _OBJ_, _LIT_, ...)                                            \
  rvk_debug_name_fmt(_DEV_, VK_OBJECT_TYPE_SHADER_MODULE, _OBJ_, "shader_" _LIT_, __VA_ARGS__)

#define rvk_debug_name_buffer(_DEV_, _OBJ_, _LIT_, ...)                                            \
  rvk_debug_name_fmt(_DEV_, VK_OBJECT_TYPE_BUFFER, _OBJ_, "buffer_" _LIT_, __VA_ARGS__)

#define rvk_debug_name_pipeline(_DEV_, _OBJ_, _LIT_, ...)                                          \
  rvk_debug_name_fmt(_DEV_, VK_OBJECT_TYPE_PIPELINE, _OBJ_, "pipeline_" _LIT_, __VA_ARGS__)

#define rvk_debug_name_pipeline_layout(_DEV_, _OBJ_, _LIT_, ...)                                   \
  rvk_debug_name_fmt(                                                                              \
      _DEV_, VK_OBJECT_TYPE_PIPELINE_LAYOUT, _OBJ_, "pipeline_layout_" _LIT_, __VA_ARGS__)

#define rvk_debug_name_pass(_DEV_, _OBJ_, _LIT_, ...)                                              \
  rvk_debug_name_fmt(_DEV_, VK_OBJECT_TYPE_RENDER_PASS, _OBJ_, "pass_" _LIT_, __VA_ARGS__)
