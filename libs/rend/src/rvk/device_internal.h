#pragma once
#include "core_thread.h"

#include "debug_internal.h"
#include "vulkan_internal.h"

// Internal forward declarations:
typedef struct sRvkDescPool   RvkDescPool;
typedef struct sRvkMemPool    RvkMemPool;
typedef struct sRvkRepository RvkRepository;
typedef struct sRvkTransferer RvkTransferer;

typedef enum {
  RvkDeviceFlags_Validation = 1 << 0,
} RvkDeviceFlags;

typedef struct sRvkDevice {
  RvkDeviceFlags                   flags;
  RvkDebug*                        debug;
  VkInstance                       vkInst;
  VkAllocationCallbacks            vkAlloc;
  VkPhysicalDevice                 vkPhysDev;
  VkPhysicalDeviceProperties       vkProperties;
  VkPhysicalDeviceFeatures         vkSupportedFeatures;
  VkPhysicalDeviceMemoryProperties vkMemProperties;
  VkDevice                         vkDev;
  VkFormat                         vkDepthFormat;
  ThreadMutex                      queueSubmitMutex;
  u32                              graphicsQueueIndex, transferQueueIndex;
  VkQueue                          vkGraphicsQueue, vkTransferQueue;
  VkPipelineCache                  vkPipelineCache;
  RvkMemPool*                      memPool;
  RvkDescPool*                     descPool;
  RvkTransferer*                   transferer;
  RvkRepository*                   repository;
} RvkDevice;

RvkDevice* rvk_device_create();
void       rvk_device_destroy(RvkDevice*);
void       rvk_device_update(RvkDevice*);
void       rvk_device_wait_idle(const RvkDevice*);
