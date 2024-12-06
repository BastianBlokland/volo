#pragma once
#include "forward_internal.h"
#include "vulkan_internal.h"

typedef enum {
  RvkSamplerFlags_None           = 0,
  RvkSamplerFlags_MipBlending    = 1 << 0, // Aka 'Trilinear' filtering.
  RvkSamplerFlags_SupportCompare = 1 << 1, // Enable support for comparisons using sampler2DShadow.
} RvkSamplerFlags;

typedef enum {
  RvkSamplerWrap_Clamp,
  RvkSamplerWrap_Repeat,
  RvkSamplerWrap_Zero,

  RvkSamplerWrap_Count,
} RvkSamplerWrap;

typedef enum {
  RvkSamplerFilter_Linear,
  RvkSamplerFilter_Nearest,

  RvkSamplerFilter_Count,
} RvkSamplerFilter;

typedef enum {
  RvkSamplerAniso_None,
  RvkSamplerAniso_x2,
  RvkSamplerAniso_x4,
  RvkSamplerAniso_x8,
  RvkSamplerAniso_x16,

  RvkSamplerAniso_Count,
} RvkSamplerAniso;

typedef struct sRvkSamplerSpec {
  RvkSamplerFlags  flags : 8;
  RvkSamplerWrap   wrap : 8;
  RvkSamplerFilter filter : 8;
  RvkSamplerAniso  aniso : 8;
} RvkSamplerSpec;

/**
 * Sampler pool.
 * Manages sampler lifetime; caller is not responsible for releasing / destroying the samplers.
 *
 * NOTE: Api is thread-safe.
 */
typedef struct sRvkSamplerPool RvkSamplerPool;

RvkSamplerPool* rvk_sampler_pool_create(RvkDevice*);
void            rvk_sampler_pool_destroy(RvkSamplerPool*);
u16             rvk_sampler_pool_count(const RvkSamplerPool*);

VkSampler rvk_sampler_get(RvkSamplerPool*, RvkSamplerSpec);
