#pragma once
#include "core_types.h"
#include "rend_color.h"
#include "rend_size.h"

#include "vulkan_internal.h"

// Internal forward declarations:
typedef struct sRvkDevice RvkDevice;
typedef struct sRvkImage  RvkImage;

typedef struct sRvkTechnique RvkTechnique;

RvkTechnique* rvk_technique_create(RvkDevice*);
void          rvk_technique_destroy(RvkTechnique*);
VkRenderPass  rvk_technique_vkrendpass(const RvkTechnique*);
RvkImage*     rvk_technique_output(RvkTechnique*);

void rvk_technique_begin(RvkTechnique*, VkCommandBuffer, RendSize size, RendColor clearColor);
void rvk_technique_end(RvkTechnique*, VkCommandBuffer);
