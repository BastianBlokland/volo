#pragma once
#include "debug_internal.h"
#include "vulkan_internal.h"

typedef struct {
  RendVkDebug*                     debug;
  VkInstance                       vkInstance;
  VkAllocationCallbacks*           vkAllocHost;
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

RendVkDevice* rend_vk_device_create(VkInstance, VkAllocationCallbacks*, RendVkDebug*);
void          rend_vk_device_destroy(RendVkDevice*);
