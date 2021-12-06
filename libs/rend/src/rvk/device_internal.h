#pragma once
#include "debug_internal.h"
#include "vulkan_internal.h"

// Forward declare from 'mem_internal.h'.
typedef struct sRvkMemPool RvkMemPool;

// Forward declare from 'desc.h'.
typedef struct sRvkDescPool RvkDescPool;

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
  RvkMemPool*                      memPool;
  RvkDescPool*                     descPool;
} RvkDevice;

RvkDevice* rvk_device_create();
void       rvk_device_destroy(RvkDevice*);
