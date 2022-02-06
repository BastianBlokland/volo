#include "core_bits.h"
#include "core_diag.h"
#include "core_math.h"
#include "log_logger.h"

#include "debug_internal.h"
#include "device_internal.h"
#include "image_internal.h"
#include "texture_internal.h"
#include "transfer_internal.h"

static u32 rvk_compute_miplevels(const RvkSize size) {
  /**
   * Check how many times we can cut the image in half before both sides hit 1 pixel.
   */
  const u32 biggestSide = math_max(size.width, size.height);
  return 32 - bits_clz_32(biggestSide);
}

static VkFormat rvk_texture_format_byte(const AssetTextureChannels channels) {
  switch (channels) {
  case AssetTextureChannels_One:
    return VK_FORMAT_R8_UNORM;
  case AssetTextureChannels_Four:
    return VK_FORMAT_R8G8B8A8_UNORM;
  }
  diag_crash();
}

static VkFormat rvk_texture_format_float(const AssetTextureChannels channels) {
  switch (channels) {
  case AssetTextureChannels_One:
    return VK_FORMAT_R32_SFLOAT;
  case AssetTextureChannels_Four:
    return VK_FORMAT_R32G32B32A32_SFLOAT;
  }
  diag_crash();
}

static VkFormat
rvk_texture_format(const AssetTextureType type, const AssetTextureChannels channels) {
  switch (type) {
  case AssetTextureType_Byte:
    return rvk_texture_format_byte(channels);
  case AssetTextureType_Float:
    return rvk_texture_format_float(channels);
  }
  diag_crash();
}

RvkTexture* rvk_texture_create(RvkDevice* dev, const AssetTextureComp* asset, String dbgName) {
  RvkTexture* texture = alloc_alloc_t(g_alloc_heap, RvkTexture);
  *texture            = (RvkTexture){
      .device  = dev,
      .dbgName = string_dup(g_alloc_heap, dbgName),
  };

  const VkFormat vkFormat = rvk_texture_format(asset->type, asset->channels);
  diag_assert(rvk_format_info(vkFormat).size == asset_texture_pixel_size(asset));
  diag_assert(rvk_format_info(vkFormat).channels == (u32)asset->channels);

  // TODO: Check if texture format is supported.

  const RvkSize size      = rvk_size(asset->width, asset->height);
  const u8      mipLevels = rvk_compute_miplevels(size);
  texture->image          = rvk_image_create_source_color(dev, vkFormat, size, mipLevels);

  const Mem pixelData    = asset_texture_data(asset);
  texture->pixelTransfer = rvk_transfer_image(dev->transferer, &texture->image, pixelData);

  rvk_debug_name_img(dev->debug, texture->image.vkImage, "{}", fmt_text(dbgName));
  rvk_debug_name_img_view(dev->debug, texture->image.vkImageView, "{}", fmt_text(dbgName));

  log_d(
      "Vulkan texture created",
      log_param("name", fmt_text(dbgName)),
      log_param("format", fmt_text(rvk_format_info(vkFormat).name)),
      log_param("size", rvk_size_fmt(texture->image.size)),
      log_param("memory", fmt_size(texture->image.mem.size)));

  return texture;
}

void rvk_texture_destroy(RvkTexture* texture) {

  RvkDevice* dev = texture->device;
  rvk_image_destroy(&texture->image, dev);

  log_d("Vulkan texture destroyed", log_param("name", fmt_text(texture->dbgName)));

  string_free(g_alloc_heap, texture->dbgName);
  alloc_free_t(g_alloc_heap, texture);
}

bool rvk_texture_prepare(RvkTexture* texture, VkCommandBuffer vkCmdBuf) {
  if (texture->flags & RvkTextureFlags_Ready) {
    return true;
  }

  if (!rvk_transfer_poll(texture->device->transferer, texture->pixelTransfer)) {
    return false;
  }

  rvk_debug_label_begin(
      texture->device->debug, vkCmdBuf, geo_color_silver, "prepare_{}", fmt_text(texture->dbgName));

  rvk_image_generate_mipmaps(&texture->image, vkCmdBuf);
  rvk_image_transition(&texture->image, vkCmdBuf, RvkImagePhase_ShaderRead);

  rvk_debug_label_end(texture->device->debug, vkCmdBuf);

  texture->flags |= RvkTextureFlags_Ready;
  return true;
}
