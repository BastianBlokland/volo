#include "core_bits.h"
#include "core_diag.h"
#include "core_math.h"
#include "log_logger.h"

#include "bc_internal.h"
#include "debug_internal.h"
#include "desc_internal.h"
#include "device_internal.h"
#include "image_internal.h"
#include "texture_internal.h"
#include "transfer_internal.h"

#define VOLO_RVK_TEXTURE_COMPRESSION 0
#define VOLO_RVK_TEXTURE_LOGGING 0

#define rvk_texture_max_scratch_size (64 * usize_kibibyte)

typedef enum {
  RvkTextureCompress_None,
  RvkTextureCompress_Bc1, // RGB 4x4 block compression.
} RvkTextureCompress;

/**
 * Compute how many times we can cut the image in half before both sides hit 1 pixel.
 */
static u16 rvk_compute_miplevels(const RvkSize size) {
  const u16 biggestSide = math_max(size.width, size.height);
  return (u16)(32 - bits_clz_32(biggestSide));
}

static RvkTextureCompress rvk_texture_compression(const AssetTextureComp* asset) {
#if VOLO_RVK_TEXTURE_COMPRESSION
  if (asset->type != AssetTextureType_U8) {
    return RvkTextureCompress_None;
  }
  if (!bits_ispow2(asset->width) || !bits_ispow2(asset->height)) {
    /**
     * Requiring both sides to be powers of two makes mip-map generation easier as all levels are
     * neatly divisible by four, and then the only needed exceptions are the last levels that are
     * smaller then 4 pixels.
     */
    return RvkTextureCompress_None;
  }
  if (asset->width < 4 || asset->height < 4) {
    /**
     * At least 4x4 pixels are needed for block compression, in theory we could add padding but for
     * these tiny sizes its probably not worth it.
     */
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
    // TODO: Support BC3 compression.
    return RvkTextureCompress_None;
  }
  return RvkTextureCompress_Bc1;
#else
  (void)asset;
  return RvkTextureCompress_None;
#endif
}

static VkFormat rvk_texture_format_byte(const AssetTextureComp* asset, const RvkTextureCompress c) {
  const bool srgb = (asset->flags & AssetTextureFlags_Srgb) != 0;
  switch (asset->channels) {
  case AssetTextureChannels_One:
    diag_assert_msg(c == RvkTextureCompress_None, "Single channel compression is not supported");
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
  diag_assert_msg(!(asset->flags & AssetTextureFlags_Srgb), "U16 srgb is not supported");
  diag_assert_msg(c == RvkTextureCompress_None, "U16 compression is not supported");
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
  diag_assert_msg(!(asset->flags & AssetTextureFlags_Srgb), "F32 srgb is not supported");
  diag_assert_msg(c == RvkTextureCompress_None, "F32 compression is not supported");
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

static usize rvk_texture_data_size_mip(
    const VkFormat format, const RvkSize size, const u32 layers, const u32 mipLevel) {
  const u32           mipWidth   = math_max(size.width >> mipLevel, 1);
  const u32           mipHeight  = math_max(size.height >> mipLevel, 1);
  const RvkFormatInfo formatInfo = rvk_format_info(format);
  if (formatInfo.flags & RvkFormat_Block4x4) {
    const u32 blocks = math_max(mipWidth / 4, 1) * math_max(mipHeight / 4, 1);
    return blocks * formatInfo.size * layers;
  }
  return mipWidth * mipHeight * formatInfo.size * layers;
}

static usize rvk_texture_data_size(
    const VkFormat format, const RvkSize size, const u32 layers, const u32 mipLevels) {
  diag_assert(layers >= 1);
  diag_assert(mipLevels >= 1);

  usize dataSize = 0;
  for (u32 mipLevel = 0; mipLevel != mipLevels; ++mipLevel) {
    dataSize += rvk_texture_data_size_mip(format, size, layers, mipLevel);
  }
  return dataSize;
}

static void rvk_texture_encode(
    const RvkSize            size,
    const u32                layers,
    const RvkTextureCompress compress,
    const Mem                dataIn,
    const Mem                dataOut) {
  diag_assert(layers >= 1);
  diag_assert(compress != RvkTextureCompress_None);
  diag_assert_msg(bits_aligned(size.width, 4), "Width has to be a multiple of 4");
  diag_assert_msg(bits_aligned(size.height, 4), "Height has to be a multiple of 4");

  const RvkBcColor8888* inPtr  = dataIn.ptr;
  RvkBc1Block*          outPtr = dataOut.ptr;

  RvkBc0Block block;
  for (u32 l = 0; l != layers; ++l) {
    for (u32 y = 0; y < size.height; y += 4, inPtr += size.width * 4) {
      for (u32 x = 0; x < size.width; x += 4, ++outPtr) {
        rvk_bc0_extract(inPtr + x, size.width, &block);
        rvk_bc1_encode(&block, outPtr);
      }
    }
  }

  diag_assert(mem_from_to(dataIn.ptr, inPtr).size == dataIn.size);
  diag_assert(mem_from_to(dataOut.ptr, outPtr).size == dataOut.size);
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
  const u8                 layers   = math_max(asset->layers, 1);

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
    diag_assert_msg(layers == 6, "CubeMap needs 6 layers");
    const bool mipGpuGen = (tex->flags & RvkTextureFlags_GpuMipGen) != 0;
    tex->image = rvk_image_create_source_color_cube(dev, vkFormat, size, mipLevels, mipGpuGen);
  } else {
    const bool mipGpuGen = (tex->flags & RvkTextureFlags_GpuMipGen) != 0;
    tex->image = rvk_image_create_source_color(dev, vkFormat, size, layers, mipLevels, mipGpuGen);
  }

  const usize encodedSize      = rvk_texture_data_size(vkFormat, size, layers, mipLevels);
  const usize encodedAlign     = rvk_format_info(vkFormat).size;
  const bool  encodeNeeded     = compress != RvkTextureCompress_None;
  const bool  encodeUseScratch = encodedSize < rvk_texture_max_scratch_size;
  Allocator*  encodeAlloc      = encodeUseScratch ? g_alloc_scratch : g_alloc_heap;

  Mem srcData = asset_texture_data(asset);
  if (encodeNeeded) {
    const Mem encodedData = alloc_alloc(encodeAlloc, encodedSize, encodedAlign);
    rvk_texture_encode(size, asset->layers, compress, srcData, encodedData);
    srcData = encodedData;
  }
  const u32 srcMips  = math_max(asset->srcMipLevels, 1);
  tex->pixelTransfer = rvk_transfer_image(dev->transferer, &tex->image, srcData, srcMips);

  if (encodeNeeded) {
    alloc_free(encodeAlloc, srcData);
  }

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
