#include "core_diag.h"
#include "log_logger.h"

#include "device_internal.h"
#include "texture_internal.h"
#include "transfer_internal.h"

RvkTexture* rvk_texture_create(RvkDevice* dev, const AssetTextureComp* asset) {
  RvkTexture* mesh = alloc_alloc_t(g_alloc_heap, RvkTexture);
  *mesh            = (RvkTexture){
      .dev = dev,
  };

  const VkFormat vkFormat = VK_FORMAT_R8G8B8A8_UNORM;
  diag_assert(rvk_format_info(vkFormat).size == sizeof(AssetTexturePixel));
  diag_assert(rvk_format_info(vkFormat).channels == 4);

  mesh->image =
      rvk_image_create_source_color(dev, vkFormat, rend_size(asset->width, asset->height));

  // TODO: Transfer pixels.

  log_d(
      "Vulkan texture created",
      log_param("format", fmt_text(rvk_format_info(vkFormat).name)),
      log_param("size", rend_size_fmt(mesh->image.size)),
      log_param("memory", fmt_size(mesh->image.mem.size)));

  return mesh;
}

void rvk_texture_destroy(RvkTexture* mesh) {

  rvk_image_destroy(&mesh->image);

  log_d("Vulkan texture destroyed");

  alloc_free_t(g_alloc_heap, mesh);
}

bool rvk_texture_prepare(RvkTexture* mesh, const RvkCanvas* canvas) {
  (void)canvas;

  // if (!rvk_transfer_poll(mesh->dev->transferer, mesh->pixelTransfer)) {
  //   return false;
  // }
  // return true; // All resources have been transferred to the device.

  // TODO: Transfer pixels.
  (void)mesh;
  return false;
}
