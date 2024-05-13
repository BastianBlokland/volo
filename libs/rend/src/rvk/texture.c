#include "core_bc.h"
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

#define rvk_texture_max_scratch_size (64 * usize_kibibyte)

typedef enum {
  RvkTextureCompress_None,
  RvkTextureCompress_Bc1, // RGB  4x4 block compression.
  RvkTextureCompress_Bc3, // RGBA 4x4 block compression.
  RvkTextureCompress_Bc4, // R    4x4 block compression.
} RvkTextureCompress;

/**
 * Compute how many times we can cut the image in half before both sides hit 1 pixel.
 */
static u16 rvk_texture_mip_count(const AssetTextureComp* asset) {
  const u16 biggestSide = math_max(asset->width, asset->height);
  const u16 mipCount    = (u16)(32 - bits_clz_32(biggestSide));
  return asset->maxMipLevels ? math_min(mipCount, asset->maxMipLevels) : mipCount;
}

static RvkTextureCompress rvk_texture_compression(RvkDevice* dev, const AssetTextureComp* asset) {
  if (!(dev->flags & RvkDeviceFlags_TextureCompression)) {
    return RvkTextureCompress_None;
  }
  if (asset->flags & AssetTextureFlags_Uncompressed) {
    return RvkTextureCompress_None;
  }
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
  if (asset->channels == AssetTextureChannels_One) {
    return RvkTextureCompress_Bc4;
  }
  if (asset->channels == AssetTextureChannels_Four) {
    return asset->flags & AssetTextureFlags_Alpha ? RvkTextureCompress_Bc3 : RvkTextureCompress_Bc1;
  }
  return RvkTextureCompress_None;
}

static VkFormat rvk_texture_format_byte(const AssetTextureComp* asset, const RvkTextureCompress c) {
  const bool srgb = (asset->flags & AssetTextureFlags_Srgb) != 0;
  switch (asset->channels) {
  case AssetTextureChannels_One:
    diag_assert_msg(!srgb, "Single channel srgb is not supported");
    switch (c) {
    case RvkTextureCompress_Bc4:
      return VK_FORMAT_BC4_UNORM_BLOCK;
    case RvkTextureCompress_None:
      return VK_FORMAT_R8_UNORM;
    default:
      diag_crash_msg("Unsupported compression '{}' for 1 channel textures", fmt_int(c));
    }
  case AssetTextureChannels_Four:
    switch (c) {
    case RvkTextureCompress_Bc1:
      return srgb ? VK_FORMAT_BC1_RGB_SRGB_BLOCK : VK_FORMAT_BC1_RGB_UNORM_BLOCK;
    case RvkTextureCompress_Bc3:
      return srgb ? VK_FORMAT_BC3_SRGB_BLOCK : VK_FORMAT_BC3_UNORM_BLOCK;
    case RvkTextureCompress_None:
      return srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    default:
      diag_crash_msg("Unsupported compression '{}' for 4 channel textures", fmt_int(c));
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
    const AssetTextureComp* asset, const VkFormat format, const u32 mipLevel) {
  const u32           mipWidth   = math_max(asset->width >> mipLevel, 1);
  const u32           mipHeight  = math_max(asset->height >> mipLevel, 1);
  const RvkFormatInfo formatInfo = rvk_format_info(format);
  if (formatInfo.flags & RvkFormat_Block4x4) {
    const u32 blocks = math_max(mipWidth / 4, 1) * math_max(mipHeight / 4, 1);
    return blocks * formatInfo.size * math_max(asset->layers, 1);
  }
  return mipWidth * mipHeight * formatInfo.size * math_max(asset->layers, 1);
}

static usize
rvk_texture_data_size(const AssetTextureComp* asset, const VkFormat format, const u32 mipLevels) {
  diag_assert(mipLevels >= 1);

  usize dataSize = 0;
  for (u32 mipLevel = 0; mipLevel != mipLevels; ++mipLevel) {
    dataSize += rvk_texture_data_size_mip(asset, format, mipLevel);
  }
  return dataSize;
}

static BcColor8888 rvk_texture_encode_bilerp(
    const BcColor8888 c0, const BcColor8888 c1, const BcColor8888 c2, const BcColor8888 c3) {
  return (BcColor8888){
      (c0.r + c1.r + c2.r + c3.r) / 4,
      (c0.g + c1.g + c2.g + c3.g) / 4,
      (c0.b + c1.b + c2.b + c3.b) / 4,
      (c0.a + c1.a + c2.a + c3.a) / 4,
  };
}

static u32 rvk_texture_encode_block(const Bc0Block* b, const RvkTextureCompress c, u8* outPtr) {
  switch (c) {
  case RvkTextureCompress_Bc1:
    bc1_encode(b, (Bc1Block*)outPtr);
    return sizeof(Bc1Block);
  case RvkTextureCompress_Bc3:
    bc3_encode(b, (Bc3Block*)outPtr);
    return sizeof(Bc3Block);
  case RvkTextureCompress_Bc4:
    bc4_encode(b, (Bc4Block*)outPtr);
    return sizeof(Bc4Block);
  default:
    UNREACHABLE
  }
}

static void rvk_texture_encode(
    const AssetTextureComp* asset, const RvkTextureCompress compress, const Mem out) {
  diag_assert(asset->type == AssetTextureType_U8);
  diag_assert(asset->channels == 1 || asset->channels == 4);
  diag_assert(compress != RvkTextureCompress_None);
  diag_assert(bits_aligned(asset->width, 4));
  diag_assert(bits_aligned(asset->height, 4));

  const u8* inPtr  = asset_texture_data(asset).ptr;
  u8*       outPtr = out.ptr;

  Bc0Block block;
  for (u32 mip = 0; mip != math_max(asset->srcMipLevels, 1); ++mip) {
    const u32 mipWidth  = math_max(asset->width >> mip, 1);
    const u32 mipHeight = math_max(asset->height >> mip, 1);
    for (u32 l = 0; l != math_max(asset->layers, 1); ++l) {
      for (u32 y = 0; y < mipHeight; y += 4, inPtr += mipWidth * 4 * asset->channels) {
        for (u32 x = 0; x < mipWidth; x += 4) {
          if (asset->channels == 1) {
            bc0_extract1(inPtr + x, mipWidth, &block);
          } else {
            bc0_extract4((const BcColor8888*)inPtr + x, mipWidth, &block);
          }
          outPtr += rvk_texture_encode_block(&block, compress, outPtr);
        }
      }
    }
  }
  diag_assert(mem_from_to(out.ptr, outPtr).size == out.size);
}

static void rvk_texture_encode_gen_mips(
    const AssetTextureComp*  asset,
    const RvkTextureCompress comp,
    const u32                mipLevels,
    const Mem                out) {
  diag_assert(asset->srcMipLevels <= 1); // Cannot both generate mips and have source mips.
  diag_assert(asset->type == AssetTextureType_U8);
  diag_assert(asset->channels == 1 || asset->channels == 4);
  diag_assert(comp != RvkTextureCompress_None);
  diag_assert(bits_aligned(asset->width, 4) && bits_ispow2(asset->width));
  diag_assert(bits_aligned(asset->height, 4) && bits_ispow2(asset->height));

  const u32   layerCount      = math_max(asset->layers, 1);
  const u32   layerBlockCount = (asset->width / 4) * (asset->height / 4);
  const usize blockBufferSize = layerCount * layerBlockCount * sizeof(Bc0Block);
  const Mem   blockBuffer     = alloc_alloc(g_allocHeap, blockBufferSize, alignof(Bc0Block));

  Bc0Block* blockPtr = blockBuffer.ptr;
  const u8* inPtr    = asset_texture_data(asset).ptr;
  u8*       outPtr   = out.ptr;

  // Extract 4x4 blocks from the source data and encode mip0.
  for (u32 l = 0; l != layerCount; ++l) {
    for (u32 y = 0; y < asset->height; y += 4, inPtr += asset->width * 4 * asset->channels) {
      for (u32 x = 0; x < asset->width; x += 4, ++blockPtr) {
        if (asset->channels == 1) {
          bc0_extract1(inPtr + x, asset->width, blockPtr);
        } else {
          bc0_extract4((const BcColor8888*)inPtr + x, asset->width, blockPtr);
        }
        outPtr += rvk_texture_encode_block(blockPtr, comp, outPtr);
      }
    }
  }

  // Down-sample and encode the other mips.
  for (u32 mip = 1; mip < mipLevels; ++mip) {
    blockPtr              = blockBuffer.ptr; // Reset the block pointer to the beginning.
    const u32 blockCountX = math_max((asset->width >> mip) / 4, 1);
    const u32 blockCountY = math_max((asset->height >> mip) / 4, 1);
    for (u32 l = 0; l != layerCount; ++l) {
      for (u32 blockY = 0; blockY != blockCountY; ++blockY) {
        for (u32 blockX = 0; blockX != blockCountX; ++blockX) {
          Bc0Block block;
          // Fill the 4x4 block by down-sampling from 4 blocks of the previous mip.
          for (u32 y = 0; y != 4; ++y) {
            for (u32 x = 0; x != 4; ++x) {
              const u32       srcBlockY = blockY * 2 + (y >= 2);
              const u32       srcBlockX = blockX * 2 + (x >= 2);
              const Bc0Block* src       = &blockPtr[srcBlockY * blockCountX * 2 + srcBlockX];
              const u32       srcX      = (x % 2) * 2;
              const u32       srcY      = (y % 2) * 2;

              const BcColor8888 c0 = src->colors[srcY * 4 + srcX];
              const BcColor8888 c1 = src->colors[srcY * 4 + srcX + 1];
              const BcColor8888 c2 = src->colors[(srcY + 1) * 4 + srcX];
              const BcColor8888 c3 = src->colors[(srcY + 1) * 4 + srcX + 1];

              block.colors[y * 4 + x] = rvk_texture_encode_bilerp(c0, c1, c2, c3);
            }
          }
          // Save the down-sampled block for use in the next mip.
          blockPtr[blockY * blockCountX + blockX] = block;
          // Encode and output this block.
          outPtr += rvk_texture_encode_block(&block, comp, outPtr);
        }
      }
      blockPtr += layerBlockCount;
    }
  }

  alloc_free(g_allocHeap, blockBuffer);
  diag_assert(mem_from_to(out.ptr, outPtr).size == out.size);
}

RvkTexture* rvk_texture_create(RvkDevice* dev, const AssetTextureComp* asset, String dbgName) {
  diag_assert_msg(asset->layers < u8_max, "Only {} texture layers are supported", fmt_int(u8_max));

  RvkTexture* tex = alloc_alloc_t(g_allocHeap, RvkTexture);
  *tex            = (RvkTexture){
                 .device  = dev,
                 .dbgName = string_dup(g_allocHeap, dbgName),
  };
  const RvkSize            size     = rvk_size(asset->width, asset->height);
  const RvkTextureCompress compress = rvk_texture_compression(dev, asset);
  const VkFormat           vkFormat = rvk_texture_format(asset, compress);
  const u8                 layers   = math_max(asset->layers, 1);

  u8 mipLevels = math_max(asset->srcMipLevels, 1);
  enum {
    MipGen_None,
    MipGen_Cpu,
    MipGen_Gpu,
  } mipGen = MipGen_None;

  if (asset->flags & AssetTextureFlags_GenerateMipMaps) {
    diag_assert(asset->srcMipLevels <= 1);
    mipLevels = rvk_texture_mip_count(asset);
    mipGen    = compress == RvkTextureCompress_None ? MipGen_Gpu : MipGen_Cpu;
  } else {
    diag_assert(asset->srcMipLevels <= rvk_texture_mip_count(asset));
  }

  if (mipGen == MipGen_Gpu) {
    tex->flags |= RvkTextureFlags_MipGenGpu;
  }
  if (asset->flags & AssetTextureFlags_Alpha) {
    tex->flags |= RvkTextureFlags_Alpha;
  }

  if (asset->flags & AssetTextureFlags_CubeMap) {
    diag_assert_msg(layers == 6, "CubeMap needs 6 layers");
    const bool mipGenGpu = mipGen == MipGen_Gpu;
    tex->image = rvk_image_create_source_color_cube(dev, vkFormat, size, mipLevels, mipGenGpu);
  } else {
    const bool mipGenGpu = mipGen == MipGen_Gpu;
    tex->image = rvk_image_create_source_color(dev, vkFormat, size, layers, mipLevels, mipGenGpu);
  }

  const usize encodedSize      = rvk_texture_data_size(asset, vkFormat, mipLevels);
  const usize encodedAlign     = rvk_format_info(vkFormat).size;
  const bool  encodeNeeded     = compress != RvkTextureCompress_None;
  const bool  encodeUseScratch = encodedSize < rvk_texture_max_scratch_size;
  Allocator*  encodeAlloc      = encodeUseScratch ? g_allocScratch : g_allocHeap;

  Mem data;
  if (encodeNeeded) {
    data = alloc_alloc(encodeAlloc, encodedSize, encodedAlign);
    if (mipGen == MipGen_None) {
      rvk_texture_encode(asset, compress, data);
    } else {
      diag_assert(mipGen == MipGen_Cpu);
      rvk_texture_encode_gen_mips(asset, compress, mipLevels, data);
    }
  } else {
    data = asset_texture_data(asset);
  }
  const u32 transferMips = mipGen == MipGen_Cpu ? mipLevels : math_max(asset->srcMipLevels, 1);
  tex->pixelTransfer     = rvk_transfer_image(dev->transferer, &tex->image, data, transferMips);

  if (encodeNeeded) {
    alloc_free(encodeAlloc, data);
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

  string_free(g_allocHeap, texture->dbgName);
  alloc_free_t(g_allocHeap, texture);
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

  if (texture->flags & RvkTextureFlags_MipGenGpu) {
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
