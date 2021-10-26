#pragma once
#include "vulkan_internal.h"

typedef struct sRendVkDevice RendVkDevice;

RendVkDevice* rend_vk_device_create(VkInstance, VkAllocationCallbacks*);
void          rend_vk_device_destroy(RendVkDevice*);
