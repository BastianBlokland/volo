#pragma once
#include "rend_color.h"

#include "swapchain_internal.h"

// Internal forward declarations:
typedef struct sRvkDevice  RvkDevice;
typedef struct sRvkGraphic RvkGraphic;

typedef struct sRvkRenderer RvkRenderer;

RvkRenderer* rvk_renderer_create(RvkDevice*, RvkSwapchain*);
void         rvk_renderer_destroy(RvkRenderer*);
VkSemaphore  rvk_renderer_image_available(RvkRenderer*);
VkSemaphore  rvk_renderer_image_ready(RvkRenderer*);
void         rvk_renderer_wait_for_done(const RvkRenderer*);

void rvk_renderer_draw_begin(RvkRenderer*, RvkSwapchainIdx, RendColor clearColor);
void rvk_renderer_draw_inst(RvkRenderer*, RvkGraphic*);
void rvk_renderer_draw_end(RvkRenderer*, RvkSwapchainIdx);
