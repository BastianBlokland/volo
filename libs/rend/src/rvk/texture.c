#include "core_bits.h"
#include "core_diag.h"
#include "core_math.h"
#include "log_logger.h"

#include "debug_internal.h"
#include "desc_internal.h"
#include "device_internal.h"
#include "image_internal.h"
#include "texture_internal.h"
#include "transfer_internal.h"

#define VOLO_RVK_TEXTURE_LOGGING 0

typedef enum {
  RvkTextureCompress_None,
  RvkTextureCompress_Bc1, // RGB, 8 bit per channel.
} RvkTextureCompress;

/**
 * Compute how many times we can cut the image in half before both sides hit 1 pixel.
 */
static u16 rvk_compute_miplevels(const RvkSize size) {
  const u16 biggestSide = math_max(size.width, size.height);
  return (u16)(32 - bits_clz_32(biggestSide));
}

static RvkTextureCompress rvk_texture_compression(const AssetTextureComp* asset) {
  if (asset->type != AssetTextureType_U8) {
    return RvkTextureCompress_None;
  }
  if (asset->channels != AssetTextureChannels_Four) {
    return RvkTextureCompress_None;
  }
  if (asset->flags & AssetTextureFlags_GenerateMipMaps || asset->srcMipLevels > 1) {
    // TODO: Support compressed textures with mip-maps.
    return RvkTextureCompress_None;
  }
  if (asset->flags & AssetTextureFlags_Alpha) {
    // TODO: Support BC3 compression
    return RvkTextureCompress_None;
  }
  return RvkTextureCompress_Bc1;
}

static VkFormat rvk_texture_format_byte(const AssetTextureComp* asset, const RvkTextureCompress c) {
  const bool srgb = (asset->flags & AssetTextureFlags_Srgb) != 0;
  switch (asset->channels) {
  case AssetTextureChannels_One:
    diag_assert_msg(c == RvkTextureCompress_None, "One channel with compression is not supported");
    return srgb ? VK_FORMAT_R8_SRGB : VK_FORMAT_R8_UNORM;
  case AssetTextureChannels_Four:
    switch (c) {
    case RvkTextureCompress_Bc1:
      return srgb ? VK_FORMAT_BC1_RGB_SRGB_BLOCK : VK_FORMAT_BC1_RGB_UNORM_BLOCK;
    case RvkTextureCompress_None:
      return srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    }
  }
  diag_crash();
}

static VkFormat rvk_texture_format_u16(const AssetTextureComp* asset, const RvkTextureCompress c) {
  diag_assert_msg(!(asset->flags & AssetTextureFlags_Srgb), "U16 with srgb is not supported");
  diag_assert_msg(c == RvkTextureCompress_None, "U16 with compression is not supported");
  (void)c;

  switch (asset->channels) {
  case AssetTextureChannels_One:
    return VK_FORMAT_R16_UNORM;
  case AssetTextureChannels_Four:
    return VK_FORMAT_R16G16B16A16_UNORM;
  }
  diag_crash();
}

static VkFormat rvk_texture_format_f32(const AssetTextureComp* asset, const RvkTextureCompress c) {
  diag_assert_msg(!(asset->flags & AssetTextureFlags_Srgb), "F32 with srgb is not supported");
  diag_assert_msg(c == RvkTextureCompress_None, "F32 with compression is not supported");
  (void)c;

  switch (asset->channels) {
  case AssetTextureChannels_One:
    return VK_FORMAT_R32_SFLOAT;
  case AssetTextureChannels_Four:
    return VK_FORMAT_R32G32B32A32_SFLOAT;
  }
  diag_crash();
}

static VkFormat rvk_texture_format(const AssetTextureComp* asset, const RvkTextureCompress c) {
  switch (asset->type) {
  case AssetTextureType_U8:
    return rvk_texture_format_byte(asset, c);
  case AssetTextureType_U16:
    return rvk_texture_format_u16(asset, c);
  case AssetTextureType_F32:
    return rvk_texture_format_f32(asset, c);
  case AssetTextureType_Count:
    UNREACHABLE
  }
  diag_crash();
}

RvkTexture* rvk_texture_create(RvkDevice* dev, const AssetTextureComp* asset, String dbgName) {
  diag_assert_msg(asset->layers < u8_max, "Only {} texture layers are supported", fmt_int(u8_max));

  RvkTexture* tex = alloc_alloc_t(g_alloc_heap, RvkTexture);
  *tex            = (RvkTexture){
                 .device  = dev,
                 .dbgName = string_dup(g_alloc_heap, dbgName),
  };
  const RvkSize            size     = rvk_size(asset->width, asset->height);
  const RvkTextureCompress compress = rvk_texture_compression(asset);
  const VkFormat           vkFormat = rvk_texture_format(asset, compress);

  u8 mipLevels;
  if (asset->flags & AssetTextureFlags_GenerateMipMaps) {
    diag_assert(asset->srcMipLevels <= 1);
    mipLevels = rvk_compute_miplevels(size);
    tex->flags |= RvkTextureFlags_GpuMipGen;
  } else {
    diag_assert(asset->srcMipLevels <= rvk_compute_miplevels(size));
    mipLevels = math_max(asset->srcMipLevels, 1);
  }

  if (asset->flags & AssetTextureFlags_Alpha) {
    tex->flags |= RvkTextureFlags_Alpha;
  }

  if (asset->flags & AssetTextureFlags_CubeMap) {
    diag_assert_msg(asset->layers == 6, "CubeMap needs 6 layers");
    const bool mipGpuGen = (tex->flags & RvkTextureFlags_GpuMipGen) != 0;
    tex->image = rvk_image_create_source_color_cube(dev, vkFormat, size, mipLevels, mipGpuGen);
  } else {
    const u8   layers    = math_max(1, asset->layers);
    const bool mipGpuGen = (tex->flags & RvkTextureFlags_GpuMipGen) != 0;
    tex->image = rvk_image_create_source_color(dev, vkFormat, size, layers, mipLevels, mipGpuGen);
  }

  const Mem srcData  = asset_texture_data(asset);
  const u32 srcMips  = math_max(asset->srcMipLevels, 1);
  tex->pixelTransfer = rvk_transfer_image(dev->transferer, &tex->image, srcData, srcMips);

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
  log_d("Vulkan texture destroyed", log_param("name", fmt_text(texture->dbgName)));
#endif

  string_free(g_alloc_heap, texture->dbgName);
  alloc_free_t(g_alloc_heap, texture);
}

RvkDescKind rvk_texture_sampler_kind(RvkTexture* texture) {
  switch (texture->image.type) {
  case RvkImageType_ColorSourceCube:
    return RvkDescKind_CombinedImageSamplerCube;
  default:
    return RvkDescKind_CombinedImageSampler2D;
  }
}

bool rvk_texture_prepare(RvkTexture* texture, VkCommandBuffer vkCmdBuf) {
  if (texture->flags & RvkTextureFlags_Ready) {
    return true;
  }

  if (!rvk_transfer_poll(texture->device->transferer, texture->pixelTransfer)) {
    return false;
  }

  if (texture->flags & RvkTextureFlags_GpuMipGen) {
    rvk_debug_label_begin(
        texture->device->debug,
        vkCmdBuf,
        geo_color_silver,
        "generate_mipmaps_{}",
        fmt_text(texture->dbgName));

    rvk_image_generate_mipmaps(&texture->image, vkCmdBuf);

    rvk_debug_label_end(texture->device->debug, vkCmdBuf);
  }

  rvk_image_transition(&texture->image, RvkImagePhase_ShaderRead, vkCmdBuf);

  texture->flags |= RvkTextureFlags_Ready;
  return true;
}
