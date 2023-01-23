#pragma once
#include "core_thread.h"
#include "rend_settings.h"

#include "vulkan_internal.h"

// Internal forward declarations:
typedef struct sRvkDebug       RvkDebug;
typedef struct sRvkDescPool    RvkDescPool;
typedef struct sRvkMemPool     RvkMemPool;
typedef struct sRvkRepository  RvkRepository;
typedef struct sRvkSamplerPool RvkSamplerPool;
typedef struct sRvkTransferer  RvkTransferer;

typedef enum {
  RvkDeviceFlags_Validation               = 1 << 0,
  RvkDeviceFlags_Debug                    = 1 << 1,
  RvkDeviceFlags_SupportPipelineStatQuery = 1 << 2,
  RvkDeviceFlags_SupportAnisotropy        = 1 << 3,
  RvkDeviceFlags_SupportFillNonSolid      = 1 << 4,
  RvkDeviceFlags_SupportWideLines         = 1 << 5,
  RvkDeviceFlags_SupportPresentId         = 1 << 6,
  RvkDeviceFlags_SupportPresentWait       = 1 << 7,
} RvkDeviceFlags;

typedef struct sRvkDevice {
  RvkDeviceFlags                   flags;
  RvkDebug*                        debug;
  VkInstance                       vkInst;
  VkAllocationCallbacks            vkAlloc;
  VkPhysicalDevice                 vkPhysDev;
  VkPhysicalDeviceProperties       vkProperties;
  VkPhysicalDeviceMemoryProperties vkMemProperties;
  VkDevice                         vkDev;
  VkFormat                         vkDepthFormat;
  ThreadMutex                      queueSubmitMutex;
  u32                              graphicsQueueIndex, transferQueueIndex;
  VkQueue                          vkGraphicsQueue, vkTransferQueue;
  VkPipelineCache                  vkPipelineCache;
  RvkMemPool*                      memPool;
  RvkDescPool*                     descPool;
  RvkSamplerPool*                  samplerPool;
  RvkTransferer*                   transferer;
  RvkRepository*                   repository;
} RvkDevice;

RvkDevice* rvk_device_create(const RendSettingsGlobalComp*);
void       rvk_device_destroy(RvkDevice*);
bool       rvk_device_format_supported(const RvkDevice*, VkFormat, VkFormatFeatureFlags);
String     rvk_device_name(const RvkDevice*);
void       rvk_device_update(RvkDevice*);
void       rvk_device_wait_idle(const RvkDevice*);
