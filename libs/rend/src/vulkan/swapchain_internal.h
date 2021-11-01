#pragma once
#include "gap_window.h"

#include "device_internal.h"
#include "image_internal.h"

typedef u32 RendSwapchainIdx;

typedef struct sRendVkSwapchain RendVkSwapchain;

RendVkSwapchain* rend_vk_swapchain_create(RendVkDevice*, const GapWindowComp*);
void             rend_vk_swapchain_destroy(RendVkSwapchain*);
VkFormat         rend_vk_swapchain_format(const RendVkSwapchain*);
u64              rend_vk_swapchain_version(const RendVkSwapchain*);
u32              rend_vk_swapchain_imagecount(const RendVkSwapchain*);
RendVkImage*     rend_vk_swapchain_image(const RendVkSwapchain*, RendSwapchainIdx);

/**
 * Acquire a new image to render into.
 * The provided semaphore will be signaled when the image is available.
 * NOTE: Returns sentinel_u32 on failure (for example because the window was minimized).
 */
RendSwapchainIdx rend_vk_swapchain_acquire(RendVkSwapchain*, VkSemaphore, RendSize);

/**
 * Present an image to the surface.
 * Image is present when the provided semaphore is signaled.
 */
bool rend_vk_swapchain_present(RendVkSwapchain*, VkSemaphore, RendSwapchainIdx);
