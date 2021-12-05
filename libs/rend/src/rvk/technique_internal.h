#pragma once
#include "core_types.h"
#include "rend_color.h"

#include "device_internal.h"
#include "swapchain_internal.h"

typedef struct sRvkTechnique RvkTechnique;

RvkTechnique* rvk_technique_create(RvkDevice*, RvkSwapchain*);
void          rvk_technique_destroy(RvkTechnique*);

void rvk_technique_begin(RvkTechnique*, VkCommandBuffer, RvkSwapchainIdx, RendColor clearColor);
void rvk_technique_end(RvkTechnique*, VkCommandBuffer);
