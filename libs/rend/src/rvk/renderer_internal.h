#pragma once
#include "rend_color.h"

#include "vulkan_internal.h"

// Internal forward declarations:
typedef struct sRvkDevice  RvkDevice;
typedef struct sRvkGraphic RvkGraphic;
typedef struct sRvkImage   RvkImage;

typedef struct sRvkRenderer RvkRenderer;

RvkRenderer* rvk_renderer_create(RvkDevice*);
void         rvk_renderer_destroy(RvkRenderer*);
VkSemaphore  rvk_renderer_semaphore_begin(RvkRenderer*);
VkSemaphore  rvk_renderer_semaphore_done(RvkRenderer*);
void         rvk_renderer_wait_for_done(const RvkRenderer*);
void         rvk_renderer_begin(RvkRenderer*, RvkImage* target, RendColor clearColor);
void         rvk_renderer_draw(RvkRenderer*, RvkGraphic*);
void         rvk_renderer_end(RvkRenderer*);
