#pragma once
#include "core_time.h"
#include "rend_stats.h"

#include "image_internal.h"
#include "vulkan_internal.h"

// Internal forward declarations:
typedef struct sRvkDevice RvkDevice;
typedef struct sRvkPass   RvkPass;

typedef struct sRvkRenderer RvkRenderer;

typedef enum {
  RvkRenderPass_Geometry,
  RvkRenderPass_Forward,
  RvkRenderPass_Shadow,
  RvkRenderPass_AmbientOcclusion,

  RvkRenderPass_Count,
} RvkRenderPass;

typedef struct {
  TimeDuration renderDur, waitForRenderDur;
  RendStatPass passes[RvkRenderPass_Count];
} RvkRenderStats;

RvkRenderer*   rvk_renderer_create(RvkDevice*, u32 rendererId);
void           rvk_renderer_destroy(RvkRenderer*);
void           rvk_renderer_wait_for_done(const RvkRenderer*);
RvkRenderStats rvk_renderer_stats(const RvkRenderer*);
String         rvk_renderer_pass_name(RvkRenderPass);

void rvk_renderer_begin(RvkRenderer*);

RvkPass* rvk_renderer_pass(RvkRenderer*, RvkRenderPass);
void     rvk_renderer_copy(RvkRenderer*, RvkImage* src, RvkImage* dst);
void     rvk_renderer_blit(RvkRenderer*, RvkImage* src, RvkImage* dst);
void     rvk_renderer_transition(RvkRenderer*, RvkImage* img, RvkImagePhase targetPhase);

void rvk_renderer_end(
    RvkRenderer*,
    VkSemaphore        waitForDeps,
    VkSemaphore        waitForTarget,
    const VkSemaphore* signals,
    u32                signalCount);
