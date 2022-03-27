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
  TimeDuration presentDur;
  TimeDuration acquireDur;
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
 * Present an image to the surface.
 * Image is present when the provided semaphore is signaled.
 */
bool rvk_swapchain_present(RvkSwapchain*, VkSemaphore, RvkSwapchainIdx);
