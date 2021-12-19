#include "core_alloc.h"
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

#define VOLO_RVK_TRANSFER_LOGGING

#define rvk_transfer_buffer_size (32 * usize_mebibyte)

#define rvk_transfer_index(_TRANSFER_ID_) ((u32)((_TRANSFER_ID_) >> 0))
#define rvk_transfer_serial(_TRANSFER_ID_) ((u32)((_TRANSFER_ID_) >> 32))

typedef enum {
  RvkTransferState_Idle,
  RvkTransferState_Rec,
  RvkTransferState_Busy,
} RvkTransferState;

typedef struct {
  RvkTransferState state;
  RvkBuffer        hostBuffer;
  VkCommandBuffer  vkCmdBuffer;
  VkFence          vkFinishedFence;
  u64              offset;
  u32              serial;
} RvkTransferBuffer;

struct sRvkTransferer {
  RvkDevice*  dev;
  ThreadMutex mutex;
  DynArray    buffers; // RvkTransferBuffer[]
};

static void rvk_image_transition_layout(
    RvkTransferBuffer*         buffer,
    const RvkImage*            image,
    const VkImageLayout        oldLayout,
    const VkImageLayout        newLayout,
    const VkAccessFlags        srcAccess,
    const VkAccessFlags        dstAccess,
    const VkPipelineStageFlags srcStageFlags,
    const VkPipelineStageFlags dstStageFlags,
    const u32                  baseMipLevel,
    const u32                  mipLevels) {

  const VkImageMemoryBarrier barrier = {
      .sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .oldLayout                       = oldLayout,
      .newLayout                       = newLayout,
      .srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED,
      .image                           = image->vkImage,
      .subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
      .subresourceRange.baseMipLevel   = baseMipLevel,
      .subresourceRange.levelCount     = mipLevels,
      .subresourceRange.baseArrayLayer = 0,
      .subresourceRange.layerCount     = 1,
      .srcAccessMask                   = srcAccess,
      .dstAccessMask                   = dstAccess,
  };
  vkCmdPipelineBarrier(
      buffer->vkCmdBuffer, srcStageFlags, dstStageFlags, 0, 0, null, 0, null, 1, &barrier);
}

static void rvk_image_transition_undef_to_transfer(
    RvkTransferBuffer* buffer, const RvkImage* image, const u32 baseMipLevel, const u32 mipLevels) {
  rvk_image_transition_layout(
      buffer,
      image,
      VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      0,
      VK_ACCESS_TRANSFER_WRITE_BIT,
      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      baseMipLevel,
      mipLevels);
}

static void rvk_image_transition_transfer_to_shader(
    RvkTransferBuffer* buffer, const RvkImage* image, const u32 baseMipLevel, const u32 mipLevels) {
  rvk_image_transition_layout(
      buffer,
      image,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_ACCESS_TRANSFER_WRITE_BIT,
      VK_ACCESS_SHADER_READ_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      baseMipLevel,
      mipLevels);
}

static VkCommandBuffer rvk_commandbuffer_create(RvkDevice* dev) {
  VkCommandBufferAllocateInfo allocInfo = {
      .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool        = dev->vkTransferCommandPool,
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
  VkFenceCreateInfo fenceInfo = {
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      .flags = initialState ? VK_FENCE_CREATE_SIGNALED_BIT : 0,
  };
  VkFence result;
  rvk_call(vkCreateFence, dev->vkDev, &fenceInfo, &dev->vkAlloc, &result);
  return result;
}

static bool rvk_transfer_fits(const RvkTransferBuffer* buffer, const u64 size, const u64 align) {
  return (bits_align(buffer->offset, align) + size) <= rvk_transfer_buffer_size;
}

static RvkTransferBuffer* rvk_transfer_buffer_create(RvkTransferer* trans) {
  RvkTransferBuffer* buffer = dynarray_push_t(&trans->buffers, RvkTransferBuffer);
  *buffer                   = (RvkTransferBuffer){
      .hostBuffer =
          rvk_buffer_create(trans->dev, rvk_transfer_buffer_size, RvkBufferType_HostTransfer),
      .vkCmdBuffer     = rvk_commandbuffer_create(trans->dev),
      .vkFinishedFence = rvk_fence_create(trans->dev, true),
  };

#ifdef VOLO_RVK_MEM_LOGGING
  log_d("Vulkan transfer buffer created", log_param("size", fmt_size(rvk_transfer_buffer_size)));
#endif
  return buffer;
}

static RvkTransferBuffer* rvk_transfer_get(RvkTransferer* trans, const u64 size, const u64 align) {
  if (UNLIKELY(size > rvk_transfer_buffer_size)) {
    diag_crash_msg(
        "Transfer size {} exceeds the maximum of {}",
        fmt_size(size),
        fmt_size(rvk_transfer_buffer_size));
  }
  dynarray_for_t(&trans->buffers, RvkTransferBuffer, buffer) {
    if (buffer->state == RvkTransferState_Rec && rvk_transfer_fits(buffer, size, align)) {
      return buffer;
    }
  }
  dynarray_for_t(&trans->buffers, RvkTransferBuffer, buffer) {
    if (buffer->state != RvkTransferState_Busy && rvk_transfer_fits(buffer, size, align)) {
      return buffer;
    }
  }
  return rvk_transfer_buffer_create(trans);
}

static RvkTransferId rvk_transfer_id(RvkTransferer* trans, RvkTransferBuffer* buffer) {
  const u64 idx = buffer - dynarray_begin_t(&trans->buffers, RvkTransferBuffer);
  return (RvkTransferId)(idx | ((u64)buffer->serial << u32_lit(32)));
}

static void rvk_transfer_begin(RvkTransferer* trans, RvkTransferBuffer* buffer) {
  diag_assert(buffer->state == RvkTransferState_Idle);
  diag_assert(rvk_fence_signaled(trans->dev, buffer->vkFinishedFence));

  buffer->state  = RvkTransferState_Rec;
  buffer->offset = 0;
  ++buffer->serial;

  rvk_call(vkResetFences, trans->dev->vkDev, 1, &buffer->vkFinishedFence);

  VkCommandBufferBeginInfo beginInfo = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
  };
  rvk_call(vkBeginCommandBuffer, buffer->vkCmdBuffer, &beginInfo);
}

static void rvk_transfer_submit(RvkTransferer* trans, RvkTransferBuffer* buffer) {
  (void)trans;

  diag_assert(buffer->state == RvkTransferState_Rec);

  buffer->state = RvkTransferState_Busy;
  vkEndCommandBuffer(buffer->vkCmdBuffer);

  const VkSubmitInfo submitInfo = {
      .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .commandBufferCount = 1,
      .pCommandBuffers    = &buffer->vkCmdBuffer,
  };
  rvk_call(vkQueueSubmit, trans->dev->vkTransferQueue, 1, &submitInfo, buffer->vkFinishedFence);
}

RvkTransferer* rvk_transferer_create(RvkDevice* dev) {
  RvkTransferer* transferer = alloc_alloc_t(g_alloc_heap, RvkTransferer);
  *transferer               = (RvkTransferer){
      .dev     = dev,
      .mutex   = thread_mutex_create(g_alloc_heap),
      .buffers = dynarray_create_t(g_alloc_heap, RvkTransferBuffer, 8),
  };
  return transferer;
}

void rvk_transferer_destroy(RvkTransferer* transferer) {

  dynarray_for_t(&transferer->buffers, RvkTransferBuffer, buffer) {
    rvk_buffer_destroy(&buffer->hostBuffer);
    vkFreeCommandBuffers(
        transferer->dev->vkDev, transferer->dev->vkTransferCommandPool, 1, &buffer->vkCmdBuffer);
    vkDestroyFence(transferer->dev->vkDev, buffer->vkFinishedFence, &transferer->dev->vkAlloc);
  }

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

  const VkBufferCopy copyRegion = {
      .srcOffset = buffer->offset,
      .dstOffset = 0,
      .size      = data.size,
  };
  vkCmdCopyBuffer(buffer->vkCmdBuffer, buffer->hostBuffer.vkBuffer, dest->vkBuffer, 1, &copyRegion);

  buffer->offset += data.size;
  const RvkTransferId id = rvk_transfer_id(trans, buffer);

#ifdef VOLO_RVK_TRANSFER_LOGGING
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

  rvk_image_transition_undef_to_transfer(buffer, dest, 0, dest->mipLevels);

  const VkBufferImageCopy region = {
      .bufferOffset                = buffer->offset,
      .imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
      .imageSubresource.layerCount = 1,
      .imageExtent.width           = dest->size.width,
      .imageExtent.height          = dest->size.height,
      .imageExtent.depth           = 1,
  };
  vkCmdCopyBufferToImage(
      buffer->vkCmdBuffer,
      buffer->hostBuffer.vkBuffer,
      dest->vkImage,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      1,
      &region);

  rvk_image_transition_transfer_to_shader(buffer, dest, 0, dest->mipLevels);

  buffer->offset += data.size;
  const RvkTransferId id = rvk_transfer_id(trans, buffer);

#ifdef VOLO_RVK_TRANSFER_LOGGING
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
  if (rvk_fence_signaled(trans->dev, buffer->vkFinishedFence)) {
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
      if (rvk_fence_signaled(trans->dev, buffer->vkFinishedFence)) {
        buffer->state = RvkTransferState_Idle;

#ifdef VOLO_RVK_TRANSFER_LOGGING
        log_d("Vulkan transfer finished", log_param("id", fmt_int(rvk_transfer_id(trans, buffer))));
#endif
      }
      break;
    case RvkTransferState_Rec:
      rvk_transfer_submit(trans, buffer);

#ifdef VOLO_RVK_TRANSFER_LOGGING
      log_d("Vulkan transfer submitted", log_param("id", fmt_int(rvk_transfer_id(trans, buffer))));
#endif
      break;
    }
  }
  thread_mutex_unlock(trans->mutex);
}
