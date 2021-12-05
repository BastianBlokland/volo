#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "gap_native.h"
#include "log_logger.h"

#include "swapchain_internal.h"

#if defined(VOLO_LINUX)
#include <xcb/xcb.h>
#elif defined(VOLO_WIN32)
#include <Windows.h>
#endif

#if defined(VOLO_LINUX)
#include <vulkan/vulkan_xcb.h>
#elif defined(VOLO_WIN32)
#include <vulkan/vulkan_win32.h>
#else
ASSERT(false, "Unsupported platform");
#endif

typedef enum {
  RvkSwapchainFlags_OutOfDate,
} RvkSwapchainFlags;

struct sRvkSwapchain {
  RvkDevice*         dev;
  VkSurfaceKHR       vkSurf;
  VkSurfaceFormatKHR vkSurfFormat;
  VkSwapchainKHR     vkSwapchain;
  VkPresentModeKHR   vkPresentMode;
  RvkSwapchainFlags  flags;
  RendSize           size;
  DynArray           images; // RvkImage[]
  u64                version;
};

static VkSurfaceKHR rvk_surface_create(RvkDevice* dev, const GapWindowComp* window) {
  VkSurfaceKHR result;
#if defined(VOLO_LINUX)
  VkXcbSurfaceCreateInfoKHR createInfo = {
      .sType      = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
      .connection = (xcb_connection_t*)gap_native_app_handle(window),
      .window     = (xcb_window_t)gap_native_window_handle(window),
  };
  rvk_call(vkCreateXcbSurfaceKHR, dev->vkInst, &createInfo, &dev->vkAlloc, &result);
#elif defined(VOLO_WIN32)
  VkWin32SurfaceCreateInfoKHR createInfo = {
      .sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
      .hinstance = (HINSTANCE)gap_native_app_handle(window),
      .hwnd      = (HWND)gap_native_window_handle(window),
  };
  rvk_call(vkCreateWin32SurfaceKHR, dev->vkInstance, &createInfo, &dev->vkAlloc, &result);
#endif
  return result;
}

static VkSurfaceFormatKHR rvk_pick_surface_format(RvkDevice* dev, VkSurfaceKHR vkSurf) {
  VkSurfaceFormatKHR availableFormats[64];
  u32                availableFormatCount = array_elems(availableFormats);
  rvk_call(
      vkGetPhysicalDeviceSurfaceFormatsKHR,
      dev->vkPhysDev,
      vkSurf,
      &availableFormatCount,
      availableFormats);

  if (!availableFormatCount) {
    diag_crash_msg("No Vulkan surface formats available");
  }

  // Prefer srgb, so the gpu can itself perform the linear to srgb conversion.
  for (u32 i = 0; i != availableFormatCount; ++i) {
    if (availableFormats[i].format == VK_FORMAT_B8G8R8A8_SRGB) {
      return availableFormats[i];
    }
  }

  log_w("No SRGB surface format available");
  return availableFormats[0];
}

static u32 rvk_pick_imagecount(const VkSurfaceCapabilitiesKHR* caps) {
  /**
   * Prefer having two images (one on-screen and one being rendered to).
   */
  u32 imgCount = 2;
  if (caps->minImageCount > imgCount) {
    imgCount = caps->minImageCount;
  }
  if (caps->maxImageCount && caps->maxImageCount < imgCount) {
    imgCount = caps->maxImageCount;
  }
  return imgCount;
}

static VkPresentModeKHR rvk_pick_presentmode(RvkDevice* dev, VkSurfaceKHR vkSurf) {
  VkPresentModeKHR available[32];
  u32              availableCount = array_elems(available);
  rvk_call(
      vkGetPhysicalDeviceSurfacePresentModesKHR,
      dev->vkPhysDev,
      vkSurf,
      &availableCount,
      available);

  /**
   * Prefer FIFO_RELAXED to reduce stuttering in case of late frames. If that is not available
   * fall-back to FIFO (which is required by the spec to always be available).
   */
  for (u32 i = 0; i != availableCount; ++i) {
    if (available[i] == VK_PRESENT_MODE_FIFO_RELAXED_KHR) {
      return available[i];
    }
  }
  return VK_PRESENT_MODE_FIFO_KHR;
}

static VkSurfaceCapabilitiesKHR rvk_surface_capabilities(RvkDevice* dev, VkSurfaceKHR vkSurf) {
  VkSurfaceCapabilitiesKHR result;
  rvk_call(vkGetPhysicalDeviceSurfaceCapabilitiesKHR, dev->vkPhysDev, vkSurf, &result);
  return result;
}

static bool rvk_swapchain_init(RvkSwapchain* swapchain, const RendSize size) {
  if (!size.width || !size.height) {
    swapchain->size = size;
    return false;
  }

  dynarray_for_t(&swapchain->images, RvkImage, img, { rvk_image_destroy(img); });
  dynarray_clear(&swapchain->images);

  VkDevice                 vkDev   = swapchain->dev->vkDev;
  VkAllocationCallbacks*   vkAlloc = &swapchain->dev->vkAlloc;
  VkSurfaceCapabilitiesKHR vkCaps  = rvk_surface_capabilities(swapchain->dev, swapchain->vkSurf);

  VkSwapchainKHR           oldSwapchain = swapchain->vkSwapchain;
  VkSwapchainCreateInfoKHR createInfo   = {
      .sType              = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
      .surface            = swapchain->vkSurf,
      .minImageCount      = rvk_pick_imagecount(&vkCaps),
      .imageFormat        = swapchain->vkSurfFormat.format,
      .imageColorSpace    = swapchain->vkSurfFormat.colorSpace,
      .imageExtent.width  = size.width,
      .imageExtent.height = size.height,
      .imageArrayLayers   = 1,
      .imageUsage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
      .imageSharingMode   = VK_SHARING_MODE_EXCLUSIVE,
      .preTransform       = vkCaps.currentTransform,
      .compositeAlpha     = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
      .presentMode        = swapchain->vkPresentMode,
      .clipped            = true,
      .oldSwapchain       = oldSwapchain,
  };
  rvk_call(vkCreateSwapchainKHR, vkDev, &createInfo, vkAlloc, &swapchain->vkSwapchain);
  if (oldSwapchain) {
    vkDestroySwapchainKHR(vkDev, oldSwapchain, &swapchain->dev->vkAlloc);
  }

  u32 imageCount;
  rvk_call(vkGetSwapchainImagesKHR, vkDev, swapchain->vkSwapchain, &imageCount, null);
  VkImage* images = mem_stack(sizeof(VkImage) * imageCount).ptr;
  rvk_call(vkGetSwapchainImagesKHR, vkDev, swapchain->vkSwapchain, &imageCount, images);

  for (u32 i = 0; i != imageCount; ++i) {
    *dynarray_push_t(&swapchain->images, RvkImage) =
        rvk_image_create_swapchain(swapchain->dev, images[i], swapchain->vkSurfFormat.format, size);
  }

  swapchain->flags &= ~RvkSwapchainFlags_OutOfDate;
  swapchain->size = size;
  ++swapchain->version;

  log_i(
      "Vulkan swapchain created",
      log_param("size", rend_size_fmt(size)),
      log_param("format", fmt_text(rvk_format_info(swapchain->vkSurfFormat.format).name)),
      log_param("color", fmt_text(rvk_colorspace_str(swapchain->vkSurfFormat.colorSpace))),
      log_param("present-mode", fmt_text(rvk_presentmode_str(swapchain->vkPresentMode))),
      log_param("image-count", fmt_int(imageCount)),
      log_param("version", fmt_int(swapchain->version)));

  return true;
}

RvkSwapchain* rvk_swapchain_create(RvkDevice* dev, const GapWindowComp* window) {
  VkSurfaceKHR  vkSurf    = rvk_surface_create(dev, window);
  RvkSwapchain* swapchain = alloc_alloc_t(g_alloc_heap, RvkSwapchain);
  *swapchain              = (RvkSwapchain){
      .dev           = dev,
      .vkSurf        = vkSurf,
      .vkSurfFormat  = rvk_pick_surface_format(dev, vkSurf),
      .vkPresentMode = rvk_pick_presentmode(dev, vkSurf),
      .images        = dynarray_create_t(g_alloc_heap, RvkImage, 2),
  };

  VkBool32 supported;
  rvk_call(
      vkGetPhysicalDeviceSurfaceSupportKHR,
      dev->vkPhysDev,
      dev->mainQueueIndex,
      vkSurf,
      &supported);
  if (!supported) {
    diag_crash_msg("Vulkan device does not support presenting to the given surface");
  }
  return swapchain;
}

void rvk_swapchain_destroy(RvkSwapchain* swapchain) {
  dynarray_for_t(&swapchain->images, RvkImage, img, { rvk_image_destroy(img); });
  dynarray_destroy(&swapchain->images);

  if (swapchain->vkSwapchain) {
    vkDestroySwapchainKHR(swapchain->dev->vkDev, swapchain->vkSwapchain, &swapchain->dev->vkAlloc);
  }

  vkDestroySurfaceKHR(swapchain->dev->vkInst, swapchain->vkSurf, &swapchain->dev->vkAlloc);
  alloc_free_t(g_alloc_heap, swapchain);
}

VkFormat rvk_swapchain_format(const RvkSwapchain* swapchain) {
  return swapchain->vkSurfFormat.format;
}

u64 rvk_swapchain_version(const RvkSwapchain* swapchain) { return swapchain->version; }

u32 rvk_swapchain_imagecount(const RvkSwapchain* swapchain) { return (u32)swapchain->images.size; }

RvkImage* rvk_swapchain_image(const RvkSwapchain* swapchain, const RvkSwapchainIdx idx) {
  diag_assert_msg(idx < swapchain->images.size, "Out of bound swapchain index");
  return dynarray_at_t(&swapchain->images, idx, RvkImage);
}

RvkSwapchainIdx
rvk_swapchain_acquire(RvkSwapchain* swapchain, VkSemaphore available, const RendSize size) {

  const bool outOfDate = (swapchain->flags & RvkSwapchainFlags_OutOfDate) != 0;
  if (!swapchain->vkSwapchain || outOfDate || !rend_size_equal(size, swapchain->size)) {
    /**
     * Synchronize swapchain (re)creation by waiting for all rendering to be done. This a very
     * course way of synchronizing and causes stalls when resizing the window. In the future we can
     * consider keeping the old swapchain alive during recreation and only destroy it after all
     * rendering to it was finished.
     */
    vkDeviceWaitIdle(swapchain->dev->vkDev);

    if (!rvk_swapchain_init(swapchain, size)) {
      return sentinel_u32;
    }
  }

  if (!swapchain->size.width || !swapchain->size.height) {
    return sentinel_u32;
  }

  u32      index;
  VkResult result = vkAcquireNextImageKHR(
      swapchain->dev->vkDev, swapchain->vkSwapchain, u64_max, available, null, &index);

  switch (result) {
  case VK_SUBOPTIMAL_KHR:
    swapchain->flags |= RvkSwapchainFlags_OutOfDate;
    log_d("Sub-optimal swapchain detected during acquire");
    return index;
  case VK_ERROR_OUT_OF_DATE_KHR:
    log_d("Out-of-date swapchain detected during acquire");
    swapchain->flags |= RvkSwapchainFlags_OutOfDate;
    return sentinel_u32;
  default:
    rvk_check(string_lit("vkAcquireNextImageKHR"), result);
    return index;
  }
}

bool rvk_swapchain_present(RvkSwapchain* swapchain, VkSemaphore ready, const RvkSwapchainIdx idx) {

  VkPresentInfoKHR presentInfo = {
      .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores    = &ready,
      .swapchainCount     = 1,
      .pSwapchains        = &swapchain->vkSwapchain,
      .pImageIndices      = &idx,
  };

  VkResult result = vkQueuePresentKHR(swapchain->dev->vkMainQueue, &presentInfo);
  switch (result) {
  case VK_SUBOPTIMAL_KHR:
    swapchain->flags |= RvkSwapchainFlags_OutOfDate;
    log_d("Sub-optimal swapchain detected during present");
    return true; // Presenting will still succeed.
  case VK_ERROR_OUT_OF_DATE_KHR:
    swapchain->flags |= RvkSwapchainFlags_OutOfDate;
    log_d("Out-of-date swapchain detected during present");
    return false; // Presenting will fail.
  default:
    rvk_check(string_lit("vkQueuePresentKHR"), result);
    return true;
  }
}
