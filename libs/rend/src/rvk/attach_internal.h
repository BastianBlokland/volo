#pragma once
#include "image_internal.h"
#include "vulkan_internal.h"

/**
 * Render attachment pool.
 *
 * NOTE: The same image can be aliased across frames or even in the same frame, the caller is
 * responsible for making sure that the image wont be used concurrently.
 */
typedef struct sRvkAttachPool RvkAttachPool;

RvkAttachPool* rvk_attach_pool_create(RvkDevice*);
void           rvk_attach_pool_destroy(RvkAttachPool*);
void           rvk_attach_pool_flush(RvkAttachPool*);

RvkImage* rvk_attach_acquire_color(RvkAttachPool*, VkFormat, RvkSize, RvkImageCapability);
RvkImage* rvk_attach_acquire_depth(RvkAttachPool*, VkFormat, RvkSize, RvkImageCapability);
void      rvk_attach_release(RvkAttachPool*, RvkImage*);
