#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_thread.h"
#include "log_logger.h"

#include "debug_internal.h"
#include "device_internal.h"
#include "sampler_internal.h"

#define VOLO_RVK_SAMPLER_LOGGING 0

#define rvk_samplers_max 64
ASSERT((rvk_samplers_max & (rvk_samplers_max - 1u)) == 0, "Max samplers has to be a power-of-two")

struct sRvkSamplerPool {
  RvkDevice*     dev;
  ThreadSpinLock spinLock;
  u32            specHashes[rvk_samplers_max];
  VkSampler      vkSamplers[rvk_samplers_max];
};

MAYBE_UNUSED static String rvk_sampler_wrap_str(const RvkSamplerWrap wrap) {
  static const String g_names[] = {
      string_static("Clamp"),
      string_static("Repeat"),
      string_static("Zero"),
  };
  ASSERT(array_elems(g_names) == RvkSamplerWrap_Count, "Incorrect number of sampler-wrap names");
  return g_names[wrap];
}

MAYBE_UNUSED static String rvk_sampler_filter_str(const RvkSamplerFilter filter) {
  static const String g_names[] = {
      string_static("Linear"),
      string_static("Nearest"),
  };
  ASSERT(
      array_elems(g_names) == RvkSamplerFilter_Count, "Incorrect number of sampler-filter names");
  return g_names[filter];
}

MAYBE_UNUSED static String rvk_sampler_aniso_str(const RvkSamplerAniso aniso) {
  static const String g_names[] = {
      string_static("None"),
      string_static("x2"),
      string_static("x4"),
      string_static("x8"),
      string_static("x16"),
  };
  ASSERT(array_elems(g_names) == RvkSamplerAniso_Count, "Incorrect number of sampler-aniso names");
  return g_names[aniso];
}

static VkFilter rvk_sampler_vkfilter(const RvkSamplerFilter filter) {
  switch (filter) {
  case RvkSamplerFilter_Linear:
    return VK_FILTER_LINEAR;
  case RvkSamplerFilter_Nearest:
    return VK_FILTER_NEAREST;
  case RvkSamplerFilter_Count:
    break;
  }
  diag_crash();
}

static VkSamplerAddressMode rvk_sampler_vkaddress(const RvkSamplerWrap wrap) {
  switch (wrap) {
  case RvkSamplerWrap_Clamp:
    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  case RvkSamplerWrap_Repeat:
    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
  case RvkSamplerWrap_Zero:
    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
  case RvkSamplerWrap_Count:
    break;
  }
  diag_crash();
}

static f32 rvk_sampler_aniso_level(const RvkSamplerAniso aniso) {
  switch (aniso) {
  case RvkSamplerAniso_None:
    return 1;
  case RvkSamplerAniso_x2:
    return 2;
  case RvkSamplerAniso_x4:
    return 4;
  case RvkSamplerAniso_x8:
    return 8;
  case RvkSamplerAniso_x16:
    return 16;
  case RvkSamplerAniso_Count:
    break;
  }
  diag_crash();
}

static VkSampler rvk_vksampler_create(const RvkDevice* dev, const RvkSamplerSpec spec) {
  VkSamplerCreateInfo samplerInfo = {
      .sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
      .magFilter               = rvk_sampler_vkfilter(spec.filter),
      .minFilter               = rvk_sampler_vkfilter(spec.filter),
      .addressModeU            = rvk_sampler_vkaddress(spec.wrap),
      .addressModeV            = rvk_sampler_vkaddress(spec.wrap),
      .addressModeW            = rvk_sampler_vkaddress(spec.wrap),
      .maxAnisotropy           = 1,
      .borderColor             = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
      .unnormalizedCoordinates = false,
      .compareEnable           = (spec.flags & RvkSamplerFlags_SupportCompare) != 0,
      .compareOp               = VK_COMPARE_OP_LESS,
      .mipmapMode = (spec.flags & RvkSamplerFlags_MipBlending) ? VK_SAMPLER_MIPMAP_MODE_LINEAR
                                                               : VK_SAMPLER_MIPMAP_MODE_NEAREST,
      .mipLodBias = 0,
      .minLod     = 0,
      .maxLod     = VK_LOD_CLAMP_NONE,
  };

  if (dev->flags & RvkDeviceFlags_SupportAnisotropy) {
    samplerInfo.anisotropyEnable = spec.aniso != RvkSamplerAniso_None;
    samplerInfo.maxAnisotropy    = rvk_sampler_aniso_level(spec.aniso);
  }

  VkSampler result;
  rvk_call(dev->api, createSampler, dev->vkDev, &samplerInfo, &dev->vkAlloc, &result);
  return result;
}

static VkSampler rvk_sampler_get_locked(RvkSamplerPool* pool, const RvkSamplerSpec spec) {
  const u32 specHash = bits_hash_32(mem_var(spec));
  diag_assert(specHash); // Hash of 0 is invalid.

  u32 bucket = specHash & (rvk_samplers_max - 1);
  for (usize i = 0; i != rvk_samplers_max; ++i) {
    const u32 slotHash = pool->specHashes[bucket];
    if (slotHash == specHash) {
      // Matching sampler found; return it.
      return pool->vkSamplers[bucket];
    }
    if (!slotHash) {
      // Slot is empty; create a new sampler.
      diag_assert(!pool->vkSamplers[bucket]);
      VkSampler newSampler     = rvk_vksampler_create(pool->dev, spec);
      pool->specHashes[bucket] = specHash;
      pool->vkSamplers[bucket] = newSampler;
      rvk_debug_name_sampler(pool->dev->debug, newSampler, "sampler_{}", fmt_int(bucket));

#if VOLO_RVK_SAMPLER_LOGGING
      log_d(
          "Vulkan sampler created",
          log_param("wrap", fmt_text(rvk_sampler_wrap_str(spec.wrap))),
          log_param("filter", fmt_text(rvk_sampler_filter_str(spec.filter))),
          log_param("anisotropic", fmt_text(rvk_sampler_aniso_str(spec.aniso))));
#endif

      return newSampler;
    }
    // Hash collision, jump to a new place in the table (quadratic probing).
    bucket = (bucket + i + 1) & (rvk_samplers_max - 1);
  }
  diag_crash_msg("Maximum sampler count exceeded");
}

RvkSamplerPool* rvk_sampler_pool_create(RvkDevice* dev) {
  RvkSamplerPool* pool = alloc_alloc_t(g_allocHeap, RvkSamplerPool);
  *pool                = (RvkSamplerPool){.dev = dev};
  return pool;
}

void rvk_sampler_pool_destroy(RvkSamplerPool* pool) {
  for (u32 i = 0; i != rvk_samplers_max; ++i) {
    if (pool->vkSamplers[i]) {
      pool->dev->api.destroySampler(pool->dev->vkDev, pool->vkSamplers[i], &pool->dev->vkAlloc);
    }
  }
  alloc_free_t(g_allocHeap, pool);
}

u16 rvk_sampler_pool_count(const RvkSamplerPool* pool) {
  RvkSamplerPool* poolMutable = (RvkSamplerPool*)pool;
  u16             res         = 0;
  thread_spinlock_lock(&poolMutable->spinLock);
  {
    for (u32 i = 0; i != rvk_samplers_max; ++i) {
      res += pool->specHashes[i] != 0;
    }
  }
  thread_spinlock_unlock(&poolMutable->spinLock);
  return res;
}

VkSampler rvk_sampler_get(RvkSamplerPool* pool, const RvkSamplerSpec spec) {
  VkSampler res;
  thread_spinlock_lock(&pool->spinLock);
  res = rvk_sampler_get_locked(pool, spec);
  thread_spinlock_unlock(&pool->spinLock);
  return res;
}
