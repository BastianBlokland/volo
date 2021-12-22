#pragma once
#include "core_types.h"
#include "rend_color.h"
#include "rend_size.h"

#include "vulkan_internal.h"

// Internal forward declarations:
typedef struct sRvkDevice RvkDevice;
typedef struct sRvkImage  RvkImage;

typedef struct sRvkPass RvkPass;

RvkPass*     rvk_pass_create(RvkDevice*);
void         rvk_pass_destroy(RvkPass*);
VkRenderPass rvk_pass_vkrendpass(const RvkPass*);
RvkImage*    rvk_pass_output(RvkPass*);
void         rvk_pass_output_barrier(RvkPass*, VkCommandBuffer);
void         rvk_pass_begin(RvkPass*, VkCommandBuffer, RendSize size, RendColor clearColor);
void         rvk_pass_end(RvkPass*, VkCommandBuffer);
