#pragma once
#include "rend_color.h"

#include "device_internal.h"
#include "swapchain_internal.h"
#include "technique_internal.h"

typedef struct sRvkRenderer RvkRenderer;

RvkRenderer* rvk_renderer_create(RvkDevice*, RvkSwapchain*);
void         rvk_renderer_destroy(RvkRenderer*);
VkSemaphore  rvk_renderer_image_available(RvkRenderer*);
VkSemaphore  rvk_renderer_image_ready(RvkRenderer*);
void         rvk_renderer_wait_for_done(const RvkRenderer*);

void rvk_renderer_draw_begin(RvkRenderer*, RvkTechnique*, RvkSwapchainIdx, RendColor clearColor);
void rvk_renderer_draw_end(RvkRenderer*, RvkTechnique*);
