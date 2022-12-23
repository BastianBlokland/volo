#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_math.h"
#include "core_thread.h"
#include "log_logger.h"

#include "buffer_internal.h"
#include "device_internal.h"
#include "image_internal.h"
#include "transfer_internal.h"

// #define VOLO_RVK_TRANSFER_LOGGING

#define rvk_transfer_buffer_size_min (4 * usize_mebibyte)

#define rvk_transfer_index(_TRANSFER_ID_) ((u32)((_TRANSFER_ID_) >> 0))
#define rvk_transfer_serial(_TRANSFER_ID_) ((u32)((_TRANSFER_ID_) >> 32))

typedef enum {
  RvkTransferState_Idle,
  RvkTransferState_Rec,
  RvkTransferState_Busy,
} RvkTransferState;

typedef struct {
  RvkBuffer        hostBuffer;
  VkCommandBuffer  vkCmdBufferTransfer, vkCmdBufferGraphics;
  VkSemaphore      releaseSemaphore; // Used for the queue ownership transfer.
  VkFence          finishedFence;
  u64              offset;
  RvkTransferState state;
  u32              serial;
} RvkTransferBuffer;

struct sRvkTransferer {
  RvkDevice*    dev;
  ThreadMutex   mutex;
  VkCommandPool vkCmdPoolTransfer, vkCmdPoolGraphics;
  DynArray      buffers; // RvkTransferBuffer[]
};

static VkCommandPool rvk_commandpool_create(RvkDevice* dev, const u32 queueIndex) {
  const VkCommandPoolCreateInfo createInfo = {
      .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .queueFamilyIndex = queueIndex,
      .flags =
          VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
  };
  VkCommandPool result;
  rvk_call(vkCreateCommandPool, dev->vkDev, &createInfo, &dev->vkAlloc, &result);
  return result;
}

static VkCommandBuffer rvk_commandbuffer_create(RvkDevice* dev, VkCommandPool vkCmdPool) {
  const VkCommandBufferAllocateInfo allocInfo = {
      .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool        = vkCmdPool,
      .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1,
  };
  VkCommandBuffer result;
  rvk_call(vkAllocateCommandBuffers, dev->vkDev, &allocInfo, &result);
  return result;
}

static bool rvk_fence_signaled(RvkDevice* dev, VkFence fence) {
  return vkGetFenceStatus(dev->vkDev, fence) == VK_SUCCESS;
}

static VkFence rvk_fence_create(RvkDevice* dev, const bool initialState) {
  const VkFenceCreateInfo fenceInfo = {
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      .flags = initialState ? VK_FENCE_CREATE_SIGNALED_BIT : 0,
  };
  VkFence result;
  rvk_call(vkCreateFence, dev->vkDev, &fenceInfo, &dev->vkAlloc, &result);
  return result;
}

static VkSemaphore rvk_semaphore_create(RvkDevice* dev) {
  VkSemaphoreCreateInfo semaphoreInfo = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  VkSemaphore           result;
  rvk_call(vkCreateSemaphore, dev->vkDev, &semaphoreInfo, &dev->vkAlloc, &result);
  return result;
}

static bool rvk_has_separate_transfer_queue(RvkTransferer* trans) {
  return trans->dev->transferQueueIndex != trans->dev->graphicsQueueIndex;
}

static bool rvk_transfer_fits(const RvkTransferBuffer* buffer, const u64 size, const u64 align) {
  return (bits_align(buffer->offset, align) + size) <= buffer->hostBuffer.size;
}

static RvkTransferBuffer* rvk_transfer_buffer_create(RvkTransferer* trans, const u64 size) {
  RvkTransferBuffer* buffer = dynarray_push_t(&trans->buffers, RvkTransferBuffer);

  *buffer = (RvkTransferBuffer){
      .hostBuffer          = rvk_buffer_create(trans->dev, size, RvkBufferType_HostTransfer),
      .vkCmdBufferTransfer = rvk_commandbuffer_create(trans->dev, trans->vkCmdPoolTransfer),
      .vkCmdBufferGraphics = rvk_commandbuffer_create(trans->dev, trans->vkCmdPoolGraphics),
      .releaseSemaphore    = rvk_semaphore_create(trans->dev),
      .finishedFence       = rvk_fence_create(trans->dev, true),
  };

#if defined(VOLO_RVK_TRANSFER_LOGGING)
  log_d("Vulkan transfer buffer created", log_param("size", fmt_size(size)));
#endif
  return buffer;
}

static RvkTransferBuffer* rvk_transfer_get(RvkTransferer* trans, const u64 size, const u64 align) {
  // Prefer a buffer that is already being recorded.
  dynarray_for_t(&trans->buffers, RvkTransferBuffer, buffer) {
    if (buffer->state == RvkTransferState_Rec && rvk_transfer_fits(buffer, size, align)) {
      return buffer;
    }
  }
  // Find the smallest buffer that would fit this transfer.
  RvkTransferBuffer* bestBuffer = null;
  u64                bestSize   = u64_max;
  dynarray_for_t(&trans->buffers, RvkTransferBuffer, buffer) {
    const bool busy = buffer->state == RvkTransferState_Busy;
    if (!busy && buffer->hostBuffer.size < bestSize && rvk_transfer_fits(buffer, size, align)) {
      bestBuffer = buffer;
      bestSize   = buffer->hostBuffer.size;
    }
  }
  if (bestBuffer) {
    return bestBuffer;
  }
  // Create a new buffer.
  return rvk_transfer_buffer_create(trans, math_max(rvk_transfer_buffer_size_min, size));
}

static RvkTransferId rvk_transfer_id(RvkTransferer* trans, RvkTransferBuffer* buffer) {
  const u64 idx = buffer - dynarray_begin_t(&trans->buffers, RvkTransferBuffer);
  return (RvkTransferId)(idx | ((u64)buffer->serial << u32_lit(32)));
}

static void rvk_transfer_begin(RvkTransferer* trans, RvkTransferBuffer* buffer) {
  diag_assert(buffer->state == RvkTransferState_Idle);
  diag_assert(rvk_fence_signaled(trans->dev, buffer->finishedFence));

  buffer->state  = RvkTransferState_Rec;
  buffer->offset = 0;
  ++buffer->serial;

  rvk_call(vkResetFences, trans->dev->vkDev, 1, &buffer->finishedFence);

  const VkCommandBufferBeginInfo beginInfo = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
  };
  rvk_call(vkBeginCommandBuffer, buffer->vkCmdBufferTransfer, &beginInfo);
  rvk_debug_label_begin(
      trans->dev->debug, buffer->vkCmdBufferTransfer, geo_color_olive, "transfer_src");

  if (rvk_has_separate_transfer_queue(trans)) {
    rvk_call(vkBeginCommandBuffer, buffer->vkCmdBufferGraphics, &beginInfo);
    rvk_debug_label_begin(
        trans->dev->debug, buffer->vkCmdBufferGraphics, geo_color_olive, "transfer_dst");
  }
}

static void rvk_transfer_submit(RvkTransferer* trans, RvkTransferBuffer* buffer) {
  diag_assert(buffer->state == RvkTransferState_Rec);
  const bool hasSeparateTransferQueue = rvk_has_separate_transfer_queue(trans);

  rvk_debug_label_end(trans->dev->debug, buffer->vkCmdBufferTransfer);
  vkEndCommandBuffer(buffer->vkCmdBufferTransfer);

  if (hasSeparateTransferQueue) {
    rvk_debug_label_end(trans->dev->debug, buffer->vkCmdBufferGraphics);
    vkEndCommandBuffer(buffer->vkCmdBufferGraphics);
  }

  buffer->state  = RvkTransferState_Busy;
  buffer->offset = 0;

  const VkSemaphore          releaseSemaphores[] = {buffer->releaseSemaphore};
  const VkPipelineStageFlags releaseWaitStages[] = {VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT};

  const VkSubmitInfo transferSubmitInfos[] = {
      {
          .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
          .commandBufferCount   = 1,
          .pCommandBuffers      = &buffer->vkCmdBufferTransfer,
          .signalSemaphoreCount = hasSeparateTransferQueue ? array_elems(releaseSemaphores) : 0,
          .pSignalSemaphores    = hasSeparateTransferQueue ? releaseSemaphores : null,
      },
  };

  thread_mutex_lock(trans->dev->queueSubmitMutex);
  rvk_call(
      vkQueueSubmit,
      trans->dev->vkTransferQueue,
      array_elems(transferSubmitInfos),
      transferSubmitInfos,
      hasSeparateTransferQueue ? null : buffer->finishedFence);

  if (hasSeparateTransferQueue) {
    const VkSubmitInfo graphicsSubmitInfos[] = {
        {
            .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers    = &buffer->vkCmdBufferGraphics,
            .waitSemaphoreCount = array_elems(releaseSemaphores),
            .pWaitSemaphores    = releaseSemaphores,
            .pWaitDstStageMask  = releaseWaitStages,
        },
    };
    rvk_call(
        vkQueueSubmit,
        trans->dev->vkGraphicsQueue,
        array_elems(graphicsSubmitInfos),
        graphicsSubmitInfos,
        buffer->finishedFence);
  }

  thread_mutex_unlock(trans->dev->queueSubmitMutex);
}

RvkTransferer* rvk_transferer_create(RvkDevice* dev) {
  RvkTransferer* transferer = alloc_alloc_t(g_alloc_heap, RvkTransferer);

  *transferer = (RvkTransferer){
      .dev               = dev,
      .mutex             = thread_mutex_create(g_alloc_heap),
      .vkCmdPoolTransfer = rvk_commandpool_create(dev, dev->transferQueueIndex),
      .vkCmdPoolGraphics = rvk_commandpool_create(dev, dev->graphicsQueueIndex),
      .buffers           = dynarray_create_t(g_alloc_heap, RvkTransferBuffer, 8),
  };

  rvk_debug_name_cmdpool(dev->debug, transferer->vkCmdPoolTransfer, "transferer");
  return transferer;
}

void rvk_transferer_destroy(RvkTransferer* transferer) {

  dynarray_for_t(&transferer->buffers, RvkTransferBuffer, buffer) {
    rvk_buffer_destroy(&buffer->hostBuffer, transferer->dev);
    vkDestroySemaphore(transferer->dev->vkDev, buffer->releaseSemaphore, &transferer->dev->vkAlloc);
    vkDestroyFence(transferer->dev->vkDev, buffer->finishedFence, &transferer->dev->vkAlloc);
  }

  vkDestroyCommandPool(
      transferer->dev->vkDev, transferer->vkCmdPoolTransfer, &transferer->dev->vkAlloc);
  vkDestroyCommandPool(
      transferer->dev->vkDev, transferer->vkCmdPoolGraphics, &transferer->dev->vkAlloc);
  thread_mutex_destroy(transferer->mutex);
  dynarray_destroy(&transferer->buffers);

  alloc_free_t(g_alloc_heap, transferer);
}

RvkTransferId rvk_transfer_buffer(RvkTransferer* trans, RvkBuffer* dest, const Mem data) {
  diag_assert(dest->mem.size >= data.size);

  thread_mutex_lock(trans->mutex);

  const u64 reqAlign = trans->dev->vkProperties.limits.optimalBufferCopyOffsetAlignment;

  RvkTransferBuffer* buffer = rvk_transfer_get(trans, data.size, reqAlign);
  if (buffer->state == RvkTransferState_Idle) {
    rvk_transfer_begin(trans, buffer);
  }
  buffer->offset = bits_align(buffer->offset, reqAlign);
  rvk_buffer_upload(&buffer->hostBuffer, data, buffer->offset);

  const VkBufferCopy copyRegions[] = {
      {
          .srcOffset = buffer->offset,
          .dstOffset = 0,
          .size      = data.size,
      },
  };
  vkCmdCopyBuffer(
      buffer->vkCmdBufferTransfer,
      buffer->hostBuffer.vkBuffer,
      dest->vkBuffer,
      array_elems(copyRegions),
      copyRegions);

  buffer->offset += data.size;
  const RvkTransferId id = rvk_transfer_id(trans, buffer);

#if defined(VOLO_RVK_TRANSFER_LOGGING)
  log_d(
      "Vulkan transfer queued",
      log_param("id", fmt_int(id)),
      log_param("buffer-idx", fmt_int(rvk_transfer_index(id))),
      log_param("type", fmt_text_lit("buffer")),
      log_param("size", fmt_size(data.size)));
#endif

  thread_mutex_unlock(trans->mutex);
  return id;
}

RvkTransferId rvk_transfer_image(RvkTransferer* trans, RvkImage* dest, const Mem data) {
  diag_assert(dest->mem.size >= data.size);

  thread_mutex_lock(trans->mutex);

  const u64 reqAlign = math_max(
      rvk_format_info(dest->vkFormat).size,
      trans->dev->vkProperties.limits.optimalBufferCopyOffsetAlignment);

  RvkTransferBuffer* buffer = rvk_transfer_get(trans, data.size, reqAlign);
  if (buffer->state == RvkTransferState_Idle) {
    rvk_transfer_begin(trans, buffer);
  }
  buffer->offset = bits_align(buffer->offset, reqAlign);
  rvk_buffer_upload(&buffer->hostBuffer, data, buffer->offset);

  rvk_image_transition(dest, buffer->vkCmdBufferTransfer, RvkImagePhase_TransferDest);

  const VkBufferImageCopy regions[] = {
      {
          .bufferOffset                = buffer->offset,
          .imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
          .imageSubresource.layerCount = dest->layers,
          .imageExtent.width           = dest->size.width,
          .imageExtent.height          = dest->size.height,
          .imageExtent.depth           = 1,
      },
  };
  vkCmdCopyBufferToImage(
      buffer->vkCmdBufferTransfer,
      buffer->hostBuffer.vkBuffer,
      dest->vkImage,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      array_elems(regions),
      regions);

  if (rvk_has_separate_transfer_queue(trans)) {
    rvk_image_transfer_ownership(
        dest,
        buffer->vkCmdBufferTransfer,
        buffer->vkCmdBufferGraphics,
        trans->dev->transferQueueIndex,
        trans->dev->graphicsQueueIndex);
  }

  buffer->offset += data.size;
  const RvkTransferId id = rvk_transfer_id(trans, buffer);

#if defined(VOLO_RVK_TRANSFER_LOGGING)
  log_d(
      "Vulkan transfer queued",
      log_param("id", fmt_int(id)),
      log_param("buffer-idx", fmt_int(rvk_transfer_index(id))),
      log_param("type", fmt_text_lit("image")),
      log_param("size", fmt_size(data.size)));
#endif

  thread_mutex_unlock(trans->mutex);
  return id;
}

RvkTransferStatus rvk_transfer_poll(RvkTransferer* trans, const RvkTransferId id) {
  thread_mutex_lock(trans->mutex);

  const u32                idx    = rvk_transfer_index(id);
  const RvkTransferBuffer* buffer = dynarray_at_t(&trans->buffers, idx, RvkTransferBuffer);

  RvkTransferStatus result = RvkTransferStatus_Busy;
  if (buffer->serial != rvk_transfer_serial(id)) {
    result = RvkTransferStatus_Finished;
    goto Done;
  }
  if (buffer->state == RvkTransferState_Idle) {
    result = RvkTransferStatus_Finished;
    goto Done;
  }
  if (rvk_fence_signaled(trans->dev, buffer->finishedFence)) {
    result = RvkTransferStatus_Finished;
    goto Done;
  }
Done:
  thread_mutex_unlock(trans->mutex);
  return result;
}

void rvk_transfer_flush(RvkTransferer* trans) {
  thread_mutex_lock(trans->mutex);
  dynarray_for_t(&trans->buffers, RvkTransferBuffer, buffer) {
    switch (buffer->state) {
    case RvkTransferState_Idle:
      break;
    case RvkTransferState_Busy:
      if (rvk_fence_signaled(trans->dev, buffer->finishedFence)) {
        buffer->state = RvkTransferState_Idle;

#if defined(VOLO_RVK_TRANSFER_LOGGING)
        log_d("Vulkan transfer finished", log_param("id", fmt_int(rvk_transfer_id(trans, buffer))));
#endif
      }
      break;
    case RvkTransferState_Rec:
      rvk_transfer_submit(trans, buffer);

#if defined(VOLO_RVK_TRANSFER_LOGGING)
      log_d("Vulkan transfer submitted", log_param("id", fmt_int(rvk_transfer_id(trans, buffer))));
#endif
      break;
    }
  }
  thread_mutex_unlock(trans->mutex);
}
