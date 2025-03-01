#pragma once
#include "core_thread.h"
#include "rend_settings.h"
#include "vulkan_api.h"

#include "forward_internal.h"

typedef enum {
  RvkDeviceFlags_SupportPipelineStatQuery = 1 << 0,
  RvkDeviceFlags_SupportAnisotropy        = 1 << 1,
  RvkDeviceFlags_SupportFillNonSolid      = 1 << 2,
  RvkDeviceFlags_SupportWideLines         = 1 << 3,
  RvkDeviceFlags_SupportPresentId         = 1 << 4,
  RvkDeviceFlags_SupportPresentWait       = 1 << 5,
  RvkDeviceFlags_SupportDepthClamp        = 1 << 6,
} RvkDeviceFlags;

typedef struct sRvkDevice {
  RvkDeviceFlags                   flags;
  VkInterfaceDevice                api;
  RvkLib*                          lib;
  RvkDebug*                        debug;
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

RvkDevice* rvk_device_create(RvkLib*, const RendSettingsGlobalComp*);
void       rvk_device_destroy(RvkDevice*);

bool   rvk_device_format_supported(const RvkDevice*, VkFormat, VkFormatFeatureFlags);
String rvk_device_name(const RvkDevice*);
void   rvk_device_update(RvkDevice*);
void   rvk_device_wait_idle(const RvkDevice*);
