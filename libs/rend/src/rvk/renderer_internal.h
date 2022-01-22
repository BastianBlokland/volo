#pragma once
#include "core_time.h"

#include "image_internal.h"
#include "vulkan_internal.h"

// Internal forward declarations:
typedef struct sRvkDevice RvkDevice;
typedef struct sRvkPass   RvkPass;

typedef struct sRvkRenderer RvkRenderer;

typedef struct {
  TimeDuration renderTime;
  u64          forwardVertices, forwardPrimitives;
  u64          forwardShadersVert, forwardShadersFrag;
} RvkRenderStats;

RvkRenderer*   rvk_renderer_create(RvkDevice*, u32 rendererId);
void           rvk_renderer_destroy(RvkRenderer*);
VkSemaphore    rvk_renderer_semaphore_begin(RvkRenderer*);
VkSemaphore    rvk_renderer_semaphore_done(RvkRenderer*);
void           rvk_renderer_wait_for_done(const RvkRenderer*);
RvkRenderStats rvk_renderer_stats(const RvkRenderer*);

void     rvk_renderer_begin(RvkRenderer*, RvkImage* target, RvkImagePhase targetPhase);
RvkPass* rvk_renderer_pass_forward(RvkRenderer*);
void     rvk_renderer_end(RvkRenderer*);
