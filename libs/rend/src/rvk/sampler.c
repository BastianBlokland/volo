#include "core_array.h"
#include "core_diag.h"

#include "device_internal.h"
#include "sampler_internal.h"

static VkFilter rvk_sampler_vkfilter(const RvkSamplerFilter filter) {
  switch (filter) {
  case RvkSamplerFilter_Nearest:
    return VK_FILTER_NEAREST;
  case RvkSamplerFilter_Linear:
    return VK_FILTER_LINEAR;
  case RvkSamplerFilter_Count:
    break;
  }
  diag_crash();
}

static VkSamplerAddressMode rvk_sampler_vkaddress(const RvkSamplerWrap wrap) {
  switch (wrap) {
  case RvkSamplerWrap_Repeat:
    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
  case RvkSamplerWrap_Clamp:
    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
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

static VkSampler rvk_vksampler_create(
    const RvkDevice*       dev,
    const RvkSamplerFlags  flags,
    const RvkSamplerWrap   wrap,
    const RvkSamplerFilter filter,
    const RvkSamplerAniso  aniso,
    const u8               mipLevels) {

  VkSamplerCreateInfo samplerInfo = {
      .sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
      .magFilter               = rvk_sampler_vkfilter(filter),
      .minFilter               = rvk_sampler_vkfilter(filter),
      .addressModeU            = rvk_sampler_vkaddress(wrap),
      .addressModeV            = rvk_sampler_vkaddress(wrap),
      .addressModeW            = rvk_sampler_vkaddress(wrap),
      .maxAnisotropy           = 1,
      .borderColor             = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
      .unnormalizedCoordinates = false,
      .compareEnable           = (flags & RvkSamplerFlags_SupportCompare) != 0,
      .compareOp               = VK_COMPARE_OP_LESS,
      .mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR,
      .mipLodBias              = 0,
      .minLod                  = 0,
      .maxLod                  = mipLevels,
  };

  if (dev->flags & RvkDeviceFlags_SupportAnisotropy) {
    samplerInfo.anisotropyEnable = aniso != RvkSamplerAniso_None;
    samplerInfo.maxAnisotropy    = rvk_sampler_aniso_level(aniso);
  }

  VkSampler result;
  rvk_call(vkCreateSampler, dev->vkDev, &samplerInfo, &dev->vkAlloc, &result);
  return result;
}

RvkSampler rvk_sampler_create(
    RvkDevice*             dev,
    const RvkSamplerFlags  flags,
    const RvkSamplerWrap   wrap,
    const RvkSamplerFilter filter,
    const RvkSamplerAniso  aniso,
    const u8               mipLevels) {

  return (RvkSampler){
      .vkSampler = rvk_vksampler_create(dev, flags, wrap, filter, aniso, mipLevels),
  };
}

void rvk_sampler_destroy(RvkSampler* sampler, RvkDevice* dev) {
  vkDestroySampler(dev->vkDev, sampler->vkSampler, &dev->vkAlloc);
}

String rvk_sampler_wrap_str(const RvkSamplerWrap wrap) {
  static const String g_names[] = {
      string_static("Repeat"),
      string_static("Clamp"),
      string_static("Zero"),
  };
  ASSERT(array_elems(g_names) == RvkSamplerWrap_Count, "Incorrect number of sampler-wrap names");
  return g_names[wrap];
}

String rvk_sampler_filter_str(const RvkSamplerFilter filter) {
  static const String g_names[] = {
      string_static("Nearest"),
      string_static("Linear"),
  };
  ASSERT(
      array_elems(g_names) == RvkSamplerFilter_Count, "Incorrect number of sampler-filter names");
  return g_names[filter];
}

String rvk_sampler_aniso_str(const RvkSamplerAniso aniso) {
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
