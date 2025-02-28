#pragma once
#include "gap_window.h"
#include "rend_settings.h"

#include "forward_internal.h"
#include "vulkan_internal.h"

typedef u32 RvkSwapchainIdx;

typedef struct sRvkSwapchain RvkSwapchain;

typedef struct sRvkSwapchainStats {
  TimeDuration acquireDur;
  TimeDuration presentEnqueueDur, presentWaitDur;
  u64          presentId;
  u16          imageCount;
} RvkSwapchainStats;

RvkSwapchain* rvk_swapchain_create(RvkLib*, RvkDevice*, const GapWindowComp*);
void          rvk_swapchain_destroy(RvkSwapchain*);
VkFormat      rvk_swapchain_format(const RvkSwapchain*);
void          rvk_swapchain_stats(const RvkSwapchain*, RvkSwapchainStats*);
RvkSize       rvk_swapchain_size(const RvkSwapchain*);
void          rvk_swapchain_invalidate(RvkSwapchain*);
RvkImage*     rvk_swapchain_image(RvkSwapchain*, RvkSwapchainIdx);

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
 * Image is presented when the provided semaphore is signaled.
 */
bool rvk_swapchain_enqueue_present(RvkSwapchain*, VkSemaphore, RvkSwapchainIdx);

/**
 * Wait for a previously enqueued presentation to be shown to the user.
 * The 'numBehind' argument controls which presentation to wait for:
 * - '0' means the last enqueued presentation.
 * - '1' means the previous enqueued presentation.
 * etc.
 *
 * NOTE: Is a no-op if the device and/or driver does not support tracking presentations.
 */
void rvk_swapchain_wait_for_present(const RvkSwapchain*, u32 numBehind);
