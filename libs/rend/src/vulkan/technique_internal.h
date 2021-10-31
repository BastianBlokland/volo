#pragma once
#include "core_types.h"

#include "device_internal.h"
#include "swapchain_internal.h"

typedef struct sRendVkTechnique RendVkTechnique;

RendVkTechnique* rend_vk_technique_create(RendVkDevice*, RendVkSwapchain*);
void             rend_vk_technique_destroy(RendVkTechnique*);
void             rend_vk_technique_begin(RendVkTechnique*, VkCommandBuffer, RendSwapchainIdx);
void             rend_vk_technique_end(RendVkTechnique*, VkCommandBuffer);
