#pragma once
#include "gap_window.h"
#include "rend_settings.h"

#include "image_internal.h"

// Internal forward declarations:
typedef struct sRvkDevice RvkDevice;
typedef struct sRvkImage  RvkImage;

typedef u32 RvkSwapchainIdx;

typedef struct sRvkSwapchain RvkSwapchain;

typedef struct {
  TimeDuration acquireDur;
  TimeDuration presentEnqueueDur, presentWaitDur;
} RvkSwapchainStats;

RvkSwapchain*     rvk_swapchain_create(RvkDevice*, const GapWindowComp*);
void              rvk_swapchain_destroy(RvkSwapchain*);
RvkSwapchainStats rvk_swapchain_stats(const RvkSwapchain*);
RvkSize           rvk_swapchain_size(const RvkSwapchain*);
RvkImage*         rvk_swapchain_image(const RvkSwapchain*, RvkSwapchainIdx);

/**
 * Acquire a new image to render into.
 * The provided semaphore will be signaled when the image is available.
 * NOTE: Returns sentinel_u32 on failure (for example because the window was minimized).
 */
RvkSwapchainIdx rvk_swapchain_acquire(RvkSwapchain*, const RendSettingsComp*, VkSemaphore, RvkSize);

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
void rvk_swapchain_wait_for_present(RvkSwapchain*, u32 numBehind);
