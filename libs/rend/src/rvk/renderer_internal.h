#pragma once
#include "core_time.h"

#include "vulkan_internal.h"

// Internal forward declarations:
typedef enum eRvkCanvasPass    RvkCanvasPass;
typedef enum eRvkImagePhase    RvkImagePhase;
typedef enum eRvkPassFlags     RvkPassFlags;
typedef struct sRvkCanvasStats RvkCanvasStats;
typedef struct sRvkDevice      RvkDevice;
typedef struct sRvkImage       RvkImage;
typedef struct sRvkPass        RvkPass;

typedef struct sRvkRenderer RvkRenderer;

RvkRenderer* rvk_renderer_create(
    RvkDevice*, u32 rendererId, const RvkPassFlags* passConfig /* [ RvkCanvasPass_Count ] */);
void           rvk_renderer_destroy(RvkRenderer*);
void           rvk_renderer_wait_for_done(const RvkRenderer*);
RvkCanvasStats rvk_renderer_stats(const RvkRenderer*);

void rvk_renderer_begin(RvkRenderer*);

RvkPass* rvk_renderer_pass(RvkRenderer*, RvkCanvasPass);
void     rvk_renderer_copy(RvkRenderer*, RvkImage* src, RvkImage* dst);
void     rvk_renderer_blit(RvkRenderer*, RvkImage* src, RvkImage* dst);
void     rvk_renderer_transition(RvkRenderer*, RvkImage* img, RvkImagePhase targetPhase);

void rvk_renderer_end(
    RvkRenderer*,
    VkSemaphore        waitForDeps,
    VkSemaphore        waitForTarget,
    const VkSemaphore* signals,
    u32                signalCount);
