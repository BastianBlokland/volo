#pragma once
#include "vulkan_internal.h"

// Internal forward declarations:
typedef struct sRvkDevice RvkDevice;

typedef enum {
  RvkSamplerWrap_Repeat,
  RvkSamplerWrap_Clamp,

  RvkSamplerWrap_Count,
} RvkSamplerWrap;

typedef enum {
  RvkSamplerFilter_Nearest,
  RvkSamplerFilter_Linear,

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

typedef struct sRvkSampler {
  VkSampler vkSampler;
} RvkSampler;

RvkSampler
rvk_sampler_create(RvkDevice*, RvkSamplerWrap, RvkSamplerFilter, RvkSamplerAniso, u8 mipLevels);

void rvk_sampler_destroy(RvkSampler*, RvkDevice*);

String rvk_sampler_wrap_str(RvkSamplerWrap);
String rvk_sampler_filter_str(RvkSamplerFilter);
String rvk_sampler_aniso_str(RvkSamplerAniso);
