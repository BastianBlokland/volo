#pragma once
#include "debug_internal.h"
#include "vulkan_internal.h"

typedef enum {
  RvkDeviceFlags_Validation = 1 << 0,
} RvkDeviceFlags;

typedef struct {
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
  u32                              mainQueueIndex;
  VkQueue                          vkMainQueue;
  VkCommandPool                    vkMainCommandPool;
} RvkDevice;

RvkDevice* rvk_device_create();
void       rvk_device_destroy(RvkDevice*);
