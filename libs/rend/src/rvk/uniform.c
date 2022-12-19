#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_math.h"
#include "log_logger.h"

#include "buffer_internal.h"
#include "desc_internal.h"
#include "device_internal.h"
#include "uniform_internal.h"

/**
 * Maximum amount of data that we can bind to a single uniform.
 * NOTE: might be lower if the 'maxUniformBufferRange' device limit is lower.
 */
#define rvk_uniform_desired_size_max (256 * usize_kibibyte)

/**
 * Size of the backing buffers to allocate.
 */
#define rvk_uniform_buffer_size (16 * usize_mebibyte)

typedef struct {
  RvkBuffer  buffer;
  RvkDescSet desc;
  u32        offset;
} RvkUniformChunk;

struct sRvkUniformPool {
  RvkDevice*  device;
  RvkDescMeta descMeta;
  u32         alignMin, dataSizeMax;
  DynArray    chunks; // RvkUniformChunk[]
};

static RvkUniformChunk* rvk_uniform_chunk(RvkUniformPool* uni, const u32 chunkIdx) {
  return dynarray_at_t(&uni->chunks, chunkIdx, RvkUniformChunk);
}

RvkUniformPool* rvk_uniform_pool_create(RvkDevice* dev) {
  RvkUniformPool* debug = alloc_alloc_t(g_alloc_heap, RvkUniformPool);

  *debug = (RvkUniformPool){
      .device               = dev,
      .descMeta.bindings[0] = RvkDescKind_UniformBufferDynamic,
      .alignMin             = (u32)dev->vkProperties.limits.minUniformBufferOffsetAlignment,
      .dataSizeMax          = (u32)math_min(
          dev->vkProperties.limits.maxUniformBufferRange, rvk_uniform_desired_size_max),
      .chunks = dynarray_create_t(g_alloc_heap, RvkUniformChunk, 16),
  };

  return debug;
}

void rvk_uniform_pool_destroy(RvkUniformPool* uni) {
  dynarray_for_t(&uni->chunks, RvkUniformChunk, chunk) {
    rvk_buffer_destroy(&chunk->buffer, uni->device);
    rvk_desc_free(chunk->desc);
  }
  dynarray_destroy(&uni->chunks);
  alloc_free_t(g_alloc_heap, uni);
}

u32 rvk_uniform_size_max(RvkUniformPool* uni) { return uni->dataSizeMax; }

VkDescriptorSetLayout rvk_uniform_vkdesclayout(RvkUniformPool* uni) {
  return rvk_desc_vklayout(uni->device->descPool, &uni->descMeta);
}

void rvk_uniform_reset(RvkUniformPool* uni) {
  dynarray_for_t(&uni->chunks, RvkUniformChunk, chunk) { chunk->offset = 0; }
}

RvkUniformHandle rvk_uniform_upload(RvkUniformPool* uni, const Mem data) {
  const u32 padding    = bits_padding((u32)data.size, uni->alignMin);
  const u32 paddedSize = (u32)data.size + padding;
  diag_assert_msg(paddedSize <= uni->dataSizeMax, "Uniform data exceeds maximum");

  // Find space in an existing chunk.
  for (u32 chunkIdx = 0; chunkIdx != uni->chunks.size; ++chunkIdx) {
    RvkUniformChunk* chunk = rvk_uniform_chunk(uni, chunkIdx);
    /**
     * Check if this chunk still has enough space left.
     * NOTE: Even though there is only 'paddedSize' amount of space requested we still need to
     * ensure that at least up to dataSizeMax is available, reason is we told Vulkan to bind up
     * to that much data and it cannot know that we will not be using all of it.
     */
    if (chunk->buffer.mem.size - chunk->offset >= uni->dataSizeMax) {
      u32 offset = chunk->offset;
      rvk_buffer_upload(&chunk->buffer, data, offset);
      chunk->offset += paddedSize;
      return (RvkUniformHandle){.chunkIdx = chunkIdx, .offset = offset};
    }
  }

  // If no chunk has enough space; create a new one.
  const u32        newChunkIdx = (u32)uni->chunks.size;
  RvkUniformChunk* newChunk    = dynarray_push_t(&uni->chunks, RvkUniformChunk);

  *newChunk = (RvkUniformChunk){
      .buffer = rvk_buffer_create(uni->device, rvk_uniform_buffer_size, RvkBufferType_HostUniform),
      .desc   = rvk_desc_alloc(uni->device->descPool, &uni->descMeta),
      .offset = (u32)paddedSize,
  };

  rvk_debug_name_buffer(uni->device->debug, newChunk->buffer.vkBuffer, "uniform");
  rvk_desc_set_attach_buffer(newChunk->desc, 0, &newChunk->buffer, uni->dataSizeMax);
  rvk_buffer_upload(&newChunk->buffer, data, 0);

  log_d(
      "Vulkan uniform chunk created",
      log_param("size", fmt_size(rvk_uniform_buffer_size)),
      log_param("data-size-max", fmt_size(uni->dataSizeMax)),
      log_param("align-min", fmt_size(uni->alignMin)));

  return (RvkUniformHandle){.chunkIdx = newChunkIdx, .offset = 0};
}

const RvkBuffer* rvk_uniform_buffer(RvkUniformPool* uni, const RvkUniformHandle handle) {
  return &rvk_uniform_chunk(uni, handle.chunkIdx)->buffer;
}

void rvk_uniform_bind(
    RvkUniformPool*  uni,
    Mem              data,
    VkCommandBuffer  vkCmdBuf,
    VkPipelineLayout vkPipelineLayout,
    const u32        set) {

  const RvkUniformHandle entry     = rvk_uniform_upload(uni, data);
  const RvkUniformChunk* chunk     = rvk_uniform_chunk(uni, entry.chunkIdx);
  VkDescriptorSet        vkDescSet = rvk_desc_set_vkset(chunk->desc);
  vkCmdBindDescriptorSets(
      vkCmdBuf,
      VK_PIPELINE_BIND_POINT_GRAPHICS,
      vkPipelineLayout,
      set,
      1,
      &vkDescSet,
      1,
      &entry.offset);
}
