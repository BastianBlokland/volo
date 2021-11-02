#pragma once
#include "debug_internal.h"
#include "vulkan_internal.h"

typedef enum {
  RendVkDeviceFlags_Validation = 1 << 0,
} RendVkDeviceFlags;

typedef struct {
  RendVkDeviceFlags                flags;
  RendVkDebug*                     debug;
  VkInstance                       vkInstance;
  VkAllocationCallbacks            vkAllocHost;
  VkPhysicalDevice                 vkPhysicalDevice;
  VkPhysicalDeviceProperties       vkProperties;
  VkPhysicalDeviceFeatures         vkSupportedFeatures;
  VkPhysicalDeviceMemoryProperties vkMemProperties;
  VkDevice                         vkDevice;
  VkFormat                         vkDepthFormat;
  u32                              mainQueueIndex;
  VkQueue                          vkMainQueue;
  VkCommandPool                    vkMainCommandPool;
} RendVkDevice;

RendVkDevice* rend_vk_device_create();
void          rend_vk_device_destroy(RendVkDevice*);
