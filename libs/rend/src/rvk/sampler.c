#include "core_diag.h"

#include "sampler_internal.h"

static VkFilter rvk_sampler_vkfilter(const RvkSamplerFilter filter) {
  switch (filter) {
  case RvkSamplerFilter_Nearest:
    return VK_FILTER_NEAREST;
  case RvkSamplerFilter_Linear:
    return VK_FILTER_LINEAR;
  }
  diag_crash();
}

static VkSamplerAddressMode rvk_sampler_vkaddress(const RvkSamplerWrap wrap) {
  switch (wrap) {
  case RvkSamplerWrap_Repeat:
    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
  case RvkSamplerWrap_Clamp:
    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
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
  }
  diag_crash();
}

static VkSampler rvk_vksampler_create(
    const RvkDevice*       dev,
    const RvkSamplerWrap   wrap,
    const RvkSamplerFilter filter,
    const RvkSamplerAniso  aniso,
    const u32              mipLevels) {

  VkSamplerCreateInfo samplerInfo = {
      .sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
      .magFilter               = rvk_sampler_vkfilter(filter),
      .minFilter               = rvk_sampler_vkfilter(filter),
      .addressModeU            = rvk_sampler_vkaddress(wrap),
      .addressModeV            = rvk_sampler_vkaddress(wrap),
      .addressModeW            = rvk_sampler_vkaddress(wrap),
      .maxAnisotropy           = 1,
      .borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
      .unnormalizedCoordinates = false,
      .compareEnable           = false,
      .compareOp               = VK_COMPARE_OP_ALWAYS,
      .mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR,
      .mipLodBias              = 0,
      .minLod                  = 0,
      .maxLod                  = mipLevels,
  };

  if (dev->vkSupportedFeatures.samplerAnisotropy) {
    samplerInfo.anisotropyEnable = aniso != RvkSamplerAniso_None;
    samplerInfo.maxAnisotropy    = rvk_sampler_aniso_level(aniso);
  }

  VkSampler result;
  rvk_call(vkCreateSampler, dev->vkDev, &samplerInfo, &dev->vkAlloc, &result);
  return result;
}

RvkSampler rvk_sampler_create(
    RvkDevice*             dev,
    const RvkSamplerWrap   wrap,
    const RvkSamplerFilter filter,
    const RvkSamplerAniso  aniso,
    const u32              mipLevels) {

  return (RvkSampler){
      .dev       = dev,
      .vkSampler = rvk_vksampler_create(dev, wrap, filter, aniso, mipLevels),
  };
}

void rvk_sampler_destroy(RvkSampler* sampler) {
  vkDestroySampler(sampler->dev->vkDev, sampler->vkSampler, &sampler->dev->vkAlloc);
}
