#pragma once
#include "core_types.h"
#include "rend_color.h"

#include "swapchain_internal.h"

// Internal forward declarations:
typedef struct sRvkDevice RvkDevice;

typedef struct sRvkTechnique RvkTechnique;

RvkTechnique* rvk_technique_create(RvkDevice*, RvkSwapchain*);
void          rvk_technique_destroy(RvkTechnique*);
VkRenderPass  rvk_technique_vkrendpass(const RvkTechnique*);

void rvk_technique_begin(RvkTechnique*, VkCommandBuffer, RvkSwapchainIdx, RendColor clearColor);
void rvk_technique_end(RvkTechnique*, VkCommandBuffer, RvkSwapchainIdx);
