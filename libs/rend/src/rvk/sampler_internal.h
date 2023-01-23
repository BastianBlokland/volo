#pragma once
#include "vulkan_internal.h"

// Internal forward declarations:
typedef struct sRvkDevice RvkDevice;

typedef enum {
  RvkSamplerFlags_None           = 0,
  RvkSamplerFlags_SupportCompare = 1 << 0, // Enable support for comparisons using sampler2DShadow.
} RvkSamplerFlags;

typedef enum {
  RvkSamplerWrap_Repeat,
  RvkSamplerWrap_Clamp,
  RvkSamplerWrap_Zero,

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

typedef struct {
  RvkSamplerFlags  flags : 8;
  RvkSamplerWrap   wrap : 8;
  RvkSamplerFilter filter : 8;
  RvkSamplerAniso  aniso : 8;
  u8               mipLevels;
} RvkSamplerSpec;

typedef struct sRvkSampler {
  VkSampler vkSampler;
} RvkSampler;

RvkSampler rvk_sampler_create(RvkDevice*, RvkSamplerSpec);

void rvk_sampler_destroy(RvkSampler*, RvkDevice*);

bool rvk_sampler_initialized(RvkSampler*);

String rvk_sampler_wrap_str(RvkSamplerWrap);
String rvk_sampler_filter_str(RvkSamplerFilter);
String rvk_sampler_aniso_str(RvkSamplerAniso);
