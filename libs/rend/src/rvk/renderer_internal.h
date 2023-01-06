#pragma once
#include "core_time.h"
#include "rend_settings.h"
#include "rend_stats.h"

#include "image_internal.h"
#include "vulkan_internal.h"

// Internal forward declarations:
typedef struct sRvkAttachPool RvkAttachPool;
typedef struct sRvkDevice     RvkDevice;
typedef struct sRvkPass       RvkPass;

typedef struct sRvkRenderer RvkRenderer;

typedef enum {
  RvkRenderPass_Geometry,
  RvkRenderPass_Forward,
  RvkRenderPass_Shadow,
  RvkRenderPass_AmbientOcclusion,

  RvkRenderPass_Count,
} RvkRenderPass;

typedef struct {
  RvkSize      resolution;
  TimeDuration renderDur, waitForRenderDur;
  RendStatPass passes[RvkRenderPass_Count];
} RvkRenderStats;

RvkRenderer*   rvk_renderer_create(RvkDevice*, RvkAttachPool*, u32 rendererId);
void           rvk_renderer_destroy(RvkRenderer*);
VkSemaphore    rvk_renderer_semaphore_done(RvkRenderer*);
void           rvk_renderer_wait_for_done(const RvkRenderer*);
RvkRenderStats rvk_renderer_stats(const RvkRenderer*);

void rvk_renderer_begin(
    RvkRenderer*, const RendSettingsComp*, RvkImage* target, RvkImagePhase targetPhase);
RvkPass* rvk_renderer_pass(RvkRenderer*, RvkRenderPass);
void     rvk_renderer_end(RvkRenderer*, VkSemaphore waitForDeps, VkSemaphore waitForTarget);
