#pragma once
#include "device_internal.h"

typedef enum {
  RvkSamplerWrap_Repeat,
  RvkSamplerWrap_Clamp,
} RvkSamplerWrap;

typedef enum {
  RvkSamplerFilter_Nearest,
  RvkSamplerFilter_Linear,
} RvkSamplerFilter;

typedef enum {
  RvkSamplerAniso_None,
  RvkSamplerAniso_x2,
  RvkSamplerAniso_x4,
  RvkSamplerAniso_x8,
  RvkSamplerAniso_x16,
} RvkSamplerAniso;

typedef struct sRvkSampler {
  RvkDevice* dev;
  VkSampler  vkSampler;
} RvkSampler;

RvkSampler
rvk_sampler_create(RvkDevice*, RvkSamplerWrap, RvkSamplerFilter, RvkSamplerAniso, u32 mipLevels);

void rvk_sampler_destroy(RvkSampler*);
