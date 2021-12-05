#pragma once
#include "debug_internal.h"
#include "vulkan_internal.h"

typedef enum {
  RvkDeviceFlags_Validation = 1 << 0,
} RvkDeviceFlags;

typedef struct {
  RvkDeviceFlags                   flags;
  RvkDebug*                        debug;
  VkInstance                       vkInstance;
  VkAllocationCallbacks            vkAlloc;
  VkPhysicalDevice                 vkPhysicalDevice;
  VkPhysicalDeviceProperties       vkProperties;
  VkPhysicalDeviceFeatures         vkSupportedFeatures;
  VkPhysicalDeviceMemoryProperties vkMemProperties;
  VkDevice                         vkDevice;
  VkFormat                         vkDepthFormat;
  u32                              mainQueueIndex;
  VkQueue                          vkMainQueue;
  VkCommandPool                    vkMainCommandPool;
} RvkDevice;

RvkDevice* rvk_device_create();
void       rvk_device_destroy(RvkDevice*);
