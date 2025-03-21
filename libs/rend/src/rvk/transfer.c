#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_math.h"
#include "core_thread.h"
#include "geo_color.h"
#include "log_logger.h"

#include "buffer_internal.h"
#include "device_internal.h"
#include "image_internal.h"
#include "lib_internal.h"
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
  VkCommandBuffer  vkCmdBufferGraphics, vkCmdBufferTransfer;
  VkSemaphore      releaseSemaphore; // Used for the queue ownership transfer.
  VkFence          finishedFence;
  u64              offset;
  RvkTransferState state;
  u32              serial;
} RvkTransferBuffer;

struct sRvkTransferer {
  RvkDevice*    dev;
  ThreadMutex   mutex;
  VkCommandPool vkCmdPoolGraphics, vkCmdPoolTransfer;
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
  rvk_call_checked(dev, createCommandPool, dev->vkDev, &createInfo, &dev->vkAlloc, &result);
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
  rvk_call_checked(dev, allocateCommandBuffers, dev->vkDev, &allocInfo, &result);
  return result;
}

static bool rvk_fence_signaled(RvkDevice* dev, VkFence fence) {
  return rvk_call(dev, getFenceStatus, dev->vkDev, fence) == VK_SUCCESS;
}

static VkFence rvk_fence_create(RvkDevice* dev, const bool initialState) {
  const VkFenceCreateInfo fenceInfo = {
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      .flags = initialState ? VK_FENCE_CREATE_SIGNALED_BIT : 0,
  };
  VkFence result;
  rvk_call_checked(dev, createFence, dev->vkDev, &fenceInfo, &dev->vkAlloc, &result);
  return result;
}

static VkSemaphore rvk_semaphore_create(RvkDevice* dev) {
  VkSemaphoreCreateInfo semaphoreInfo = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  VkSemaphore           result;
  rvk_call_checked(dev, createSemaphore, dev->vkDev, &semaphoreInfo, &dev->vkAlloc, &result);
  return result;
}

static bool rvk_transfer_fits(const RvkTransferBuffer* buffer, const u64 size, const u64 align) {
  return (bits_align(buffer->offset, align) + size) <= buffer->hostBuffer.size;
}

static RvkTransferBuffer* rvk_transfer_buffer_create(RvkTransferer* trans, const u64 size) {
  RvkTransferBuffer* buffer = dynarray_push_t(&trans->buffers, RvkTransferBuffer);

  *buffer = (RvkTransferBuffer){
      .hostBuffer          = rvk_buffer_create(trans->dev, size, RvkBufferType_HostTransfer),
      .vkCmdBufferGraphics = rvk_commandbuffer_create(trans->dev, trans->vkCmdPoolGraphics),
      .releaseSemaphore    = rvk_semaphore_create(trans->dev),
      .finishedFence       = rvk_fence_create(trans->dev, true),
  };

  if (trans->vkCmdPoolTransfer) {
    buffer->vkCmdBufferTransfer = rvk_commandbuffer_create(trans->dev, trans->vkCmdPoolTransfer);
  }

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

  RvkDevice* dev = trans->dev;

  rvk_call_checked(dev, resetFences, dev->vkDev, 1, &buffer->finishedFence);

  const VkCommandBufferBeginInfo beginInfo = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
  };
  rvk_call_checked(dev, beginCommandBuffer, buffer->vkCmdBufferGraphics, &beginInfo);
  rvk_debug_label_begin(dev, buffer->vkCmdBufferGraphics, geo_color_olive, "transfer");

  if (buffer->vkCmdBufferTransfer) {
    rvk_call_checked(dev, beginCommandBuffer, buffer->vkCmdBufferTransfer, &beginInfo);
    rvk_debug_label_begin(dev, buffer->vkCmdBufferTransfer, geo_color_olive, "transfer");
  }
}

static void rvk_transfer_submit(RvkTransferer* trans, RvkTransferBuffer* buffer) {
  diag_assert(buffer->state == RvkTransferState_Rec);

  RvkDevice* dev = trans->dev;

  rvk_debug_label_end(trans->dev, buffer->vkCmdBufferGraphics);
  rvk_call(dev, endCommandBuffer, buffer->vkCmdBufferGraphics);

  if (buffer->vkCmdBufferTransfer) {
    rvk_debug_label_end(trans->dev, buffer->vkCmdBufferTransfer);
    rvk_call(dev, endCommandBuffer, buffer->vkCmdBufferTransfer);
  }

  buffer->state  = RvkTransferState_Busy;
  buffer->offset = 0;

  thread_mutex_lock(trans->dev->queueSubmitMutex);

  const VkSemaphore          releaseSemaphores[] = {buffer->releaseSemaphore};
  const VkPipelineStageFlags releaseWaitStages[] = {VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT};

  if (buffer->vkCmdBufferTransfer) {
    const VkSubmitInfo transferSubmitInfos[] = {
        {
            .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount   = 1,
            .pCommandBuffers      = &buffer->vkCmdBufferTransfer,
            .signalSemaphoreCount = array_elems(releaseSemaphores),
            .pSignalSemaphores    = releaseSemaphores,
        },
    };
    rvk_call_checked(
        dev,
        queueSubmit,
        trans->dev->vkTransferQueue,
        array_elems(transferSubmitInfos),
        transferSubmitInfos,
        null);
  }

  const VkSubmitInfo graphicsSubmitInfos[] = {
      {
          .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
          .commandBufferCount = 1,
          .pCommandBuffers    = &buffer->vkCmdBufferGraphics,
          .waitSemaphoreCount = buffer->vkCmdBufferTransfer ? array_elems(releaseSemaphores) : 0,
          .pWaitSemaphores    = buffer->vkCmdBufferTransfer ? releaseSemaphores : null,
          .pWaitDstStageMask  = releaseWaitStages,
      },
  };
  rvk_call_checked(
      dev,
      queueSubmit,
      trans->dev->vkGraphicsQueue,
      array_elems(graphicsSubmitInfos),
      graphicsSubmitInfos,
      buffer->finishedFence);

  thread_mutex_unlock(trans->dev->queueSubmitMutex);
}

RvkTransferer* rvk_transferer_create(RvkDevice* dev) {
  RvkTransferer* transferer = alloc_alloc_t(g_allocHeap, RvkTransferer);

  *transferer = (RvkTransferer){
      .dev               = dev,
      .mutex             = thread_mutex_create(g_allocHeap),
      .vkCmdPoolGraphics = rvk_commandpool_create(dev, dev->graphicsQueueIndex),
      .buffers           = dynarray_create_t(g_allocHeap, RvkTransferBuffer, 8),
  };
  rvk_debug_name_cmdpool(dev, transferer->vkCmdPoolGraphics, "transferer_graphics");

  if (dev->vkTransferQueue) {
    transferer->vkCmdPoolTransfer = rvk_commandpool_create(dev, dev->transferQueueIndex);
    rvk_debug_name_cmdpool(dev, transferer->vkCmdPoolTransfer, "transferer_transfer");
  }

  return transferer;
}

void rvk_transferer_destroy(RvkTransferer* transferer) {
  RvkDevice* dev = transferer->dev;
  dynarray_for_t(&transferer->buffers, RvkTransferBuffer, buffer) {
    rvk_buffer_destroy(&buffer->hostBuffer, dev);
    rvk_call(dev, destroySemaphore, dev->vkDev, buffer->releaseSemaphore, &dev->vkAlloc);
    rvk_call(dev, destroyFence, dev->vkDev, buffer->finishedFence, &dev->vkAlloc);
  }

  rvk_call(dev, destroyCommandPool, dev->vkDev, transferer->vkCmdPoolGraphics, &dev->vkAlloc);
  if (transferer->vkCmdPoolTransfer) {
    rvk_call(dev, destroyCommandPool, dev->vkDev, transferer->vkCmdPoolTransfer, &dev->vkAlloc);
  }
  thread_mutex_destroy(transferer->mutex);
  dynarray_destroy(&transferer->buffers);

  alloc_free_t(g_allocHeap, transferer);
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
  rvk_call(
      trans->dev,
      cmdCopyBuffer,
      buffer->vkCmdBufferTransfer ? buffer->vkCmdBufferTransfer : buffer->vkCmdBufferGraphics,
      buffer->hostBuffer.vkBuffer,
      dest->vkBuffer,
      array_elems(copyRegions),
      copyRegions);

  if (buffer->vkCmdBufferTransfer) {
    rvk_buffer_transfer_ownership(
        trans->dev,
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
      log_param("type", fmt_text_lit("buffer")),
      log_param("size", fmt_size(data.size)));
#endif

  thread_mutex_unlock(trans->mutex);
  return id;
}

static u32 rvk_transfer_image_src_size_mip(const RvkImage* img, const u32 mipLevel) {
  diag_assert(mipLevel < img->mipLevels);
  const u32 mipWidth  = math_max(img->size.width >> mipLevel, 1);
  const u32 mipHeight = math_max(img->size.height >> mipLevel, 1);
  if (vkFormatCompressed4x4(img->vkFormat)) {
    const u32 blocks = math_max(mipWidth / 4, 1) * math_max(mipHeight / 4, 1);
    return blocks * vkFormatByteSize(img->vkFormat) * img->layers;
  }
  return mipWidth * mipHeight * vkFormatByteSize(img->vkFormat) * img->layers;
}

MAYBE_UNUSED static u32 rvk_transfer_image_src_size(const RvkImage* img, const u32 mipLevels) {
  diag_assert(mipLevels <= img->mipLevels);
  u32 size = 0;
  for (u32 mipLevel = 0; mipLevel != math_max(mipLevels, 1); ++mipLevel) {
    size += rvk_transfer_image_src_size_mip(img, mipLevel);
  }
  return size;
}

RvkTransferId
rvk_transfer_image(RvkTransferer* trans, RvkImage* dest, const Mem data, const u32 mips) {
  diag_assert(mips >= 1);
  diag_assert(data.size == rvk_transfer_image_src_size(dest, mips));

  thread_mutex_lock(trans->mutex);

  const u64 reqAlign = math_max(
      vkFormatByteSize(dest->vkFormat),
      trans->dev->vkProperties.limits.optimalBufferCopyOffsetAlignment);

  RvkTransferBuffer* buffer = rvk_transfer_get(trans, data.size, reqAlign);
  if (buffer->state == RvkTransferState_Idle) {
    rvk_transfer_begin(trans, buffer);
  }
  buffer->offset = bits_align(buffer->offset, reqAlign);
  rvk_buffer_upload(&buffer->hostBuffer, data, buffer->offset);

  if (buffer->vkCmdBufferTransfer) {
    rvk_image_transition(trans->dev, dest, RvkImagePhase_TransferDest, buffer->vkCmdBufferTransfer);
  } else {
    rvk_image_transition(trans->dev, dest, RvkImagePhase_TransferDest, buffer->vkCmdBufferGraphics);
  }

  VkBufferImageCopy regions[16];
  diag_assert(array_elems(regions) >= mips);
  u64 srcBufferOffset = buffer->offset;
  for (u32 mipLevel = 0; mipLevel != mips; ++mipLevel) {
    regions[mipLevel] = (VkBufferImageCopy){
        .bufferOffset                = srcBufferOffset,
        .imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .imageSubresource.mipLevel   = mipLevel,
        .imageSubresource.layerCount = dest->layers,
        .imageExtent.width           = math_max(dest->size.width >> mipLevel, 1),
        .imageExtent.height          = math_max(dest->size.height >> mipLevel, 1),
        .imageExtent.depth           = 1,
    };
    srcBufferOffset += rvk_transfer_image_src_size_mip(dest, mipLevel);
  }
  diag_assert(srcBufferOffset == buffer->offset + data.size);

  rvk_call(
      trans->dev,
      cmdCopyBufferToImage,
      buffer->vkCmdBufferTransfer ? buffer->vkCmdBufferTransfer : buffer->vkCmdBufferGraphics,
      buffer->hostBuffer.vkBuffer,
      dest->vkImage,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      mips,
      regions);

  if (buffer->vkCmdBufferTransfer) {
    rvk_image_transfer_ownership(
        trans->dev,
        dest,
        buffer->vkCmdBufferTransfer,
        buffer->vkCmdBufferGraphics,
        trans->dev->transferQueueIndex,
        trans->dev->graphicsQueueIndex);
  }

  if (dest->flags & RvkImageFlags_GenerateMips) {
    diag_assert(!vkFormatCompressed4x4(dest->vkFormat));
    diag_assert(mips == 1); // Can't both generate mips and provide source mips.

    rvk_image_generate_mipmaps(trans->dev, dest, buffer->vkCmdBufferGraphics);
  }

  rvk_image_transition(trans->dev, dest, RvkImagePhase_ShaderRead, buffer->vkCmdBufferGraphics);
  rvk_image_freeze(dest);

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

RvkTransferStatus rvk_transfer_poll(const RvkTransferer* trans, const RvkTransferId id) {
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
