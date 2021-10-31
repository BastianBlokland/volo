#pragma once
#include "core_types.h"

#include "device_internal.h"
#include "swapchain_internal.h"
#include "technique_internal.h"

typedef struct sRendVkRenderer RendVkRenderer;

RendVkRenderer* rend_vk_renderer_create(RendVkDevice*, RendVkSwapchain*);
void            rend_vk_renderer_destroy(RendVkRenderer*);
VkSemaphore     rend_vk_renderer_image_available(RendVkRenderer*);
VkSemaphore     rend_vk_renderer_image_ready(RendVkRenderer*);
void            rend_vk_renderer_wait_for_done(const RendVkRenderer*);
void            rend_vk_renderer_draw_begin(RendVkRenderer*, RendVkTechnique*, RendSwapchainIdx);
void            rend_vk_renderer_draw_end(RendVkRenderer*, RendVkTechnique*);
