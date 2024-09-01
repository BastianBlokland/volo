#include "core_diag.h"
#include "log_logger.h"

#include "debug_internal.h"
#include "desc_internal.h"
#include "device_internal.h"
#include "image_internal.h"
#include "texture_internal.h"
#include "transfer_internal.h"

#define VOLO_RVK_TEXTURE_LOGGING 0

static VkFormat rvk_texture_format(const AssetTextureComp* asset) {
  const bool srgb = (asset->flags & AssetTextureFlags_Srgb) != 0;
  switch (asset->format) {
  case AssetTextureFormat_u8_r:
    diag_assert_msg(!srgb, "Single channel srgb is not supported");
    return VK_FORMAT_R8_UNORM;
  case AssetTextureFormat_u8_rgba:
    return srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
  case AssetTextureFormat_u16_r:
    diag_assert_msg(!srgb, "U16 srgb is not supported");
    return VK_FORMAT_R16_UNORM;
  case AssetTextureFormat_u16_rgba:
    diag_assert_msg(!srgb, "U16 srgb is not supported");
    return VK_FORMAT_R16G16B16A16_UNORM;
  case AssetTextureFormat_f32_r:
    diag_assert_msg(!srgb, "F32 srgb is not supported");
    return VK_FORMAT_R32_SFLOAT;
  case AssetTextureFormat_f32_rgba:
    diag_assert_msg(!srgb, "F32 srgb is not supported");
    return VK_FORMAT_R32G32B32A32_SFLOAT;
  case AssetTextureFormat_Bc1:
    return srgb ? VK_FORMAT_BC1_RGB_SRGB_BLOCK : VK_FORMAT_BC1_RGB_UNORM_BLOCK;
  case AssetTextureFormat_Bc3:
    return srgb ? VK_FORMAT_BC3_SRGB_BLOCK : VK_FORMAT_BC3_UNORM_BLOCK;
  case AssetTextureFormat_Bc4:
    return VK_FORMAT_BC4_UNORM_BLOCK;
  case AssetTextureFormat_Count:
    UNREACHABLE
  }
  diag_crash();
}

RvkTexture* rvk_texture_create(RvkDevice* dev, const AssetTextureComp* asset, String dbgName) {
  diag_assert_msg(asset->layers < u8_max, "Only {} texture layers are supported", fmt_int(u8_max));
  diag_assert_msg(asset->mipsMax < u8_max, "Only {} texture mips are supported", fmt_int(u8_max));

  RvkTexture* tex = alloc_alloc_t(g_allocHeap, RvkTexture);

  *tex = (RvkTexture){.device = dev};

  const RvkSize  size       = rvk_size(asset->width, asset->height);
  const u8       layers     = (u8)asset->layers;
  const u8       mipLevels  = (u8)asset->mipsMax;
  const VkFormat vkFormat   = rvk_texture_format(asset);
  const bool     compressed = (rvk_format_info(vkFormat).flags & RvkFormat_Block4x4) != 0;
  (void)compressed;

  bool mipGenGpu = false;
  if (asset->mipsData != asset->mipsMax) {
    diag_assert(asset->mipsData == 1); // Cannot both have source mips and generate mips.
    diag_assert(!compressed);          // Cannot generate mips for compressed textures on the gpu.
    mipGenGpu = true;
  }

  if (asset->flags & AssetTextureFlags_CubeMap) {
    diag_assert_msg(layers == 6, "CubeMap needs 6 layers");
    tex->image = rvk_image_create_source_color_cube(dev, vkFormat, size, mipLevels, mipGenGpu);
  } else {
    tex->image = rvk_image_create_source_color(dev, vkFormat, size, layers, mipLevels, mipGenGpu);
  }

  const Mem transferData = asset_texture_data(asset);
  const u32 transferMips = asset->mipsData;
  tex->pixelTransfer =
      rvk_transfer_image(dev->transferer, &tex->image, transferData, transferMips, mipGenGpu);

  rvk_debug_name_img(dev->debug, tex->image.vkImage, "{}", fmt_text(dbgName));
  rvk_debug_name_img_view(dev->debug, tex->image.vkImageView, "{}", fmt_text(dbgName));

#if VOLO_RVK_TEXTURE_LOGGING
  log_d(
      "Vulkan texture created",
      log_param("name", fmt_text(dbgName)),
      log_param("format", fmt_text(rvk_format_info(vkFormat).name)),
      log_param("size", rvk_size_fmt(tex->image.size)),
      log_param("layers", fmt_int(tex->image.layers)),
      log_param("memory", fmt_size(tex->image.mem.size)));
#endif

  return tex;
}

void rvk_texture_destroy(RvkTexture* texture) {
  RvkDevice* dev = texture->device;
  rvk_image_destroy(&texture->image, dev);

#if VOLO_RVK_TEXTURE_LOGGING
  log_d("Vulkan texture destroyed");
#endif

  alloc_free_t(g_allocHeap, texture);
}

RvkDescKind rvk_texture_sampler_kind(const RvkTexture* texture) {
  switch (texture->image.type) {
  case RvkImageType_ColorSourceCube:
    return RvkDescKind_CombinedImageSamplerCube;
  default:
    return RvkDescKind_CombinedImageSampler2D;
  }
}

bool rvk_texture_is_ready(const RvkTexture* texture) {
  if (!rvk_transfer_poll(texture->device->transferer, texture->pixelTransfer)) {
    return false;
  }
  return true;
}
