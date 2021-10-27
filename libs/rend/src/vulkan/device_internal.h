#pragma once
#include "debug_internal.h"
#include "vulkan_internal.h"

typedef struct sRendVkDevice RendVkDevice;

RendVkDevice* rend_vk_device_create(VkInstance, VkAllocationCallbacks*, RendVkDebug*);
void          rend_vk_device_destroy(RendVkDevice*);
void          rend_vk_device_debug_name(RendVkDevice*, VkObjectType, u64 vkHandle, String name);
