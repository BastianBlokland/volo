#pragma once
#include "gap/window.h"
#include "rend/settings.h"

#include "forward.h"
#include "vulkan_api.h"

typedef u32 RvkSwapchainIdx;

typedef struct sRvkSwapchain RvkSwapchain;

typedef struct sRvkSwapchainStats {
  TimeDuration acquireDur;
  TimeDuration presentEnqueueDur, presentWaitDur;
  TimeDuration refreshDuration; // 0 if unavailable.
  u16          imageCount;
} RvkSwapchainStats;

typedef struct sRvkSwapchainPresentation {
  u64          frameIdx;
  TimeSteady   dequeueTime; // Time at which the presentation engine dequeue it from the swapchain.
  TimeDuration duration;
} RvkSwapchainPresentation;

RvkSwapchain* rvk_swapchain_create(RvkLib*, RvkDevice*, const GapWindowComp*);
void          rvk_swapchain_destroy(RvkSwapchain*);
VkFormat      rvk_swapchain_format(const RvkSwapchain*);
void          rvk_swapchain_stats(const RvkSwapchain*, RvkSwapchainStats*);
RvkSize       rvk_swapchain_size(const RvkSwapchain*);
void          rvk_swapchain_invalidate(RvkSwapchain*);
RvkImage*     rvk_swapchain_image(RvkSwapchain*, RvkSwapchainIdx);
VkSemaphore   rvk_swapchain_semaphore(RvkSwapchain*, RvkSwapchainIdx);

/**
 * Prepare the swapchain to be acquired.
 * Returns true if an image can be acquired, returns false if not.
 */
bool rvk_swapchain_prepare(RvkSwapchain*, const RendSettingsComp*, RvkSize);

/**
 * Acquire a new image to render into.
 * The provided semaphore will be signaled when the image is available.
 */
RvkSwapchainIdx rvk_swapchain_acquire(RvkSwapchain*, VkSemaphore);

/**
 * Enqueue an image to be presented to the surface.
 * Image is presented when the 'rvk_swapchain_semaphore(idx)' is signaled.
 */
bool rvk_swapchain_enqueue_present(RvkSwapchain*, RvkSwapchainIdx, u64 frameIdx);

/**
 * Query past presentations which have been completed.
 * Returns the amount of completed presentations since the last call.
 * NOTE: Not supported on all platforms, always return 0 when unsupported.
 */
u32 rvk_swapchain_query_presentations(
    const RvkSwapchain*, RvkSwapchainPresentation out[], u32 outMax);

/**
 * Wait for a previously enqueued presentation to be shown to the user.
 * The 'numBehind' argument controls which presentation to wait for:
 * - '0' means the last frame's presentation.
 * - '1' means the previous frame's presentation.
 * etc.
 *
 * NOTE: Is a no-op if the device and/or driver does not support tracking presentations.
 */
void rvk_swapchain_wait_for_present(const RvkSwapchain*, u32 numBehind);
