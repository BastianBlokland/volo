#include "core_bits.h"
#include "core_diag.h"
#include "core_math.h"
#include "log_logger.h"

#include "device_internal.h"
#include "image_internal.h"
#include "texture_internal.h"
#include "transfer_internal.h"

static u32 rvk_compute_miplevels(const RendSize size) {
  /**
   * Check how many times we can cut the image in half before both sides hit 1 pixel.
   */
  const u32 biggestSide = math_max(size.width, size.height);
  return 32 - bits_clz_32(biggestSide);
}

RvkTexture* rvk_texture_create(RvkDevice* dev, const AssetTextureComp* asset) {
  RvkTexture* texture = alloc_alloc_t(g_alloc_heap, RvkTexture);
  *texture            = (RvkTexture){
      .device = dev,
  };

  const VkFormat vkFormat = VK_FORMAT_R8G8B8A8_UNORM;
  diag_assert(rvk_format_info(vkFormat).size == sizeof(AssetTexturePixel));
  diag_assert(rvk_format_info(vkFormat).channels == 4);

  const RendSize size      = rend_size(asset->width, asset->height);
  const u8       mipLevels = rvk_compute_miplevels(size);
  texture->image           = rvk_image_create_source_color(dev, vkFormat, size, mipLevels);

  const usize pixelDataSize = sizeof(AssetTexturePixel) * asset->width * asset->height;
  texture->pixelTransfer    = rvk_transfer_image(
      dev->transferer, &texture->image, mem_create(asset->pixels, pixelDataSize));

  log_d(
      "Vulkan texture created",
      log_param("format", fmt_text(rvk_format_info(vkFormat).name)),
      log_param("size", rend_size_fmt(texture->image.size)),
      log_param("memory", fmt_size(texture->image.mem.size)));

  return texture;
}

void rvk_texture_destroy(RvkTexture* texture) {

  RvkDevice* dev = texture->device;
  rvk_image_destroy(&texture->image, dev);

  alloc_free_t(g_alloc_heap, texture);
}

bool rvk_texture_prepare(RvkTexture* texture, VkCommandBuffer vkCmdBuf) {
  if (texture->flags & RvkTextureFlags_Ready) {
    return true;
  }

  if (!rvk_transfer_poll(texture->device->transferer, texture->pixelTransfer)) {
    return false;
  }

  rvk_image_generate_mipmaps(&texture->image, vkCmdBuf);
  rvk_image_transition(&texture->image, vkCmdBuf, RvkImagePhase_ShaderRead);
  texture->flags |= RvkTextureFlags_Ready;
  return true;
}
