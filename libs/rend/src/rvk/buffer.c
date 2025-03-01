#include "core_array.h"
#include "core_diag.h"

#include "buffer_internal.h"
#include "device_internal.h"

static RvkMemLoc rvk_buffer_type_loc(const RvkBufferType type) {
  switch (type) {
  case RvkBufferType_DeviceIndex:
  case RvkBufferType_DeviceStorage:
    return RvkMemLoc_Dev;
  case RvkBufferType_HostUniform:
  case RvkBufferType_HostTransfer:
    return RvkMemLoc_Host;
  case RvkBufferType_Count:
    break;
  }
  diag_crash_msg("Unexpected RvkBufferType");
}

static VkBufferUsageFlags rvk_buffer_usage_flags(const RvkBufferType type) {
  switch (type) {
  case RvkBufferType_DeviceIndex:
    return VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
  case RvkBufferType_DeviceStorage:
    return VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  case RvkBufferType_HostUniform:
    return VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
  case RvkBufferType_HostTransfer:
    return VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  case RvkBufferType_Count:
    break;
  }
  diag_crash_msg("Unexpected RvkBufferType");
}

static void rvk_buffer_barrier(
    RvkDevice*                 dev,
    VkCommandBuffer            vkCmdBuf,
    const RvkBuffer*           buffer,
    const u32                  srcQueueFamIdx,
    const u32                  dstQueueFamIdx,
    const VkAccessFlags        srcAccess,
    const VkAccessFlags        dstAccess,
    const VkPipelineStageFlags srcStageFlags,
    const VkPipelineStageFlags dstStageFlags) {

  const VkBufferMemoryBarrier barrier = {
      .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
      .srcAccessMask       = srcAccess,
      .dstAccessMask       = dstAccess,
      .srcQueueFamilyIndex = srcQueueFamIdx,
      .dstQueueFamilyIndex = dstQueueFamIdx,
      .buffer              = buffer->vkBuffer,
      .offset              = 0,
      .size                = buffer->size,
  };
  dev->api.cmdPipelineBarrier(
      vkCmdBuf, srcStageFlags, dstStageFlags, 0, 0, null, 1, &barrier, 0, null);
}

RvkBuffer rvk_buffer_create(RvkDevice* dev, const u64 size, const RvkBufferType type) {
  const VkBufferUsageFlags usageFlags = rvk_buffer_usage_flags(type);
  const VkBufferCreateInfo bufferInfo = {
      .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size        = size,
      .usage       = usageFlags,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
  };
  VkBuffer vkBuffer;
  rvk_call(dev->api, createBuffer, dev->vkDev, &bufferInfo, &dev->vkAlloc, &vkBuffer);

  VkMemoryRequirements memReqs;
  dev->api.getBufferMemoryRequirements(dev->vkDev, vkBuffer, &memReqs);

  const RvkMemLoc memLoc = rvk_buffer_type_loc(type);
  const RvkMem    mem    = rvk_mem_alloc_req(dev->memPool, memLoc, RvkMemAccess_Linear, memReqs);

  rvk_mem_bind_buffer(mem, vkBuffer);

  return (RvkBuffer){
      .type     = type,
      .size     = size,
      .mem      = mem,
      .vkBuffer = vkBuffer,
  };
}

void rvk_buffer_destroy(RvkBuffer* buffer, RvkDevice* dev) {
  dev->api.destroyBuffer(dev->vkDev, buffer->vkBuffer, &dev->vkAlloc);
  rvk_mem_free(buffer->mem);
}

String rvk_buffer_type_str(const RvkBufferType type) {
  static const String g_names[] = {
      string_static("DeviceIndex"),
      string_static("DeviceStorage"),
      string_static("HostUniform"),
      string_static("HostTransfer"),
  };
  ASSERT(array_elems(g_names) == RvkBufferType_Count, "Incorrect number of buffer-type names");
  return g_names[type];
}

Mem rvk_buffer_map(RvkBuffer* buffer, const u64 offset) {
  diag_assert(offset <= buffer->size);
  diag_assert(rvk_buffer_type_loc(buffer->type) == RvkMemLoc_Host);

  return mem_consume(rvk_mem_map(buffer->mem), offset);
}

void rvk_buffer_flush(const RvkBuffer* buffer, const u64 offset, const u64 size) {
  const RvkBufferFlush flushes[] = {
      {.buffer = buffer, .offset = offset, .size = size},
  };
  rvk_buffer_flush_batch(flushes, array_elems(flushes));
}

void rvk_buffer_flush_batch(const RvkBufferFlush flushes[], const u32 count) {
  RvkMemFlush* memFlushes = mem_stack(sizeof(RvkMemFlush) * count).ptr;
  for (u32 i = 0; i != count; ++i) {
    const RvkBuffer* buffer = flushes[i].buffer;
    diag_assert(rvk_buffer_type_loc(buffer->type) == RvkMemLoc_Host);

    // TODO: Refactor the underlying memory-pool to use 64 bit size and offsets.
    memFlushes[i] = (RvkMemFlush){
        .mem    = buffer->mem,
        .offset = (u32)flushes[i].offset,
        .size   = (u32)flushes[i].size,
    };
  }
  rvk_mem_flush_batch(memFlushes, count);
}

void rvk_buffer_upload(RvkBuffer* buffer, const Mem data, const u64 offset) {
  diag_assert(data.size + offset <= buffer->size);
  diag_assert(rvk_buffer_type_loc(buffer->type) == RvkMemLoc_Host);

  mem_cpy(rvk_buffer_map(buffer, offset), data);

  // TODO: Refactor the underlying memory-pool to use 64 bit size and offsets.
  rvk_mem_flush(buffer->mem, (u32)offset, (u32)data.size);
}

void rvk_buffer_transfer_ownership(
    RvkDevice*       dev,
    const RvkBuffer* buffer,
    VkCommandBuffer  srcCmdBuf,
    VkCommandBuffer  dstCmdBuf,
    u32              srcQueueFamIdx,
    u32              dstQueueFamIdx) {
  if (srcQueueFamIdx == dstQueueFamIdx) {
    return;
  }

  // Release the buffer on the source queue.
  rvk_buffer_barrier(
      dev,
      srcCmdBuf,
      buffer,
      srcQueueFamIdx,
      dstQueueFamIdx,
      VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
      0,
      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);

  // Acquire the buffer on the destination queue.
  rvk_buffer_barrier(
      dev,
      dstCmdBuf,
      buffer,
      srcQueueFamIdx,
      dstQueueFamIdx,
      0,
      VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
}
