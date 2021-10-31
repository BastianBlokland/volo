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
  RendVkSwapchainFlags_OutOfDate,
} RendVkSwapchainFlags;

struct sRendVkSwapchain {
  RendVkDevice*        device;
  VkSurfaceKHR         vkSurface;
  VkSurfaceFormatKHR   vkSurfaceFormat;
  VkSwapchainKHR       vkSwapchain;
  VkPresentModeKHR     vkPresentMode;
  RendVkSwapchainFlags flags;
  GapVector            size;
  DynArray             images; // RendVkImage[]
  u64                  version;
};

static VkSurfaceKHR rend_vk_surface_create(RendVkDevice* dev, const GapWindowComp* window) {
  VkSurfaceKHR result;
#if defined(VOLO_LINUX)
  VkXcbSurfaceCreateInfoKHR createInfo = {
      .sType      = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
      .connection = (xcb_connection_t*)gap_native_app_handle(window),
      .window     = (xcb_window_t)gap_native_window_handle(window),
  };
  rend_vk_call(vkCreateXcbSurfaceKHR, dev->vkInstance, &createInfo, dev->vkAllocHost, &result);
#elif defined(VOLO_WIN32)
  VkWin32SurfaceCreateInfoKHR createInfo = {
      .sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
      .hinstance = (HINSTANCE)gap_native_app_handle(window),
      .hwnd      = (HWND)gap_native_window_handle(window),
  };
  rend_vk_call(vkCreateWin32SurfaceKHR, dev->vkInstance, &createInfo, dev->vkAllocHost, &result);
#endif
  return result;
}

static VkSurfaceFormatKHR rend_vk_pick_surface_format(RendVkDevice* dev, VkSurfaceKHR vkSurface) {
  VkSurfaceFormatKHR availableFormats[64];
  u32                availableFormatCount = array_elems(availableFormats);
  rend_vk_call(
      vkGetPhysicalDeviceSurfaceFormatsKHR,
      dev->vkPhysicalDevice,
      vkSurface,
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

static u32 rend_vk_pick_imagecount(const VkSurfaceCapabilitiesKHR* capabilities) {
  /**
   * Prefer having two images (one on-screen and one being rendered to).
   */
  u32 imgCount = 2;
  if (capabilities->minImageCount > imgCount) {
    imgCount = capabilities->minImageCount;
  }
  if (capabilities->maxImageCount && capabilities->maxImageCount < imgCount) {
    imgCount = capabilities->maxImageCount;
  }
  return imgCount;
}

static VkPresentModeKHR rend_vk_pick_presentmode(RendVkDevice* dev, VkSurfaceKHR vkSurface) {
  VkPresentModeKHR available[32];
  u32              availableCount = array_elems(available);
  rend_vk_call(
      vkGetPhysicalDeviceSurfacePresentModesKHR,
      dev->vkPhysicalDevice,
      vkSurface,
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

static VkSurfaceCapabilitiesKHR
rend_vk_surface_capabilities(RendVkDevice* dev, VkSurfaceKHR vkSurface) {
  VkSurfaceCapabilitiesKHR result;
  rend_vk_call(
      vkGetPhysicalDeviceSurfaceCapabilitiesKHR, dev->vkPhysicalDevice, vkSurface, &result);
  return result;
}

static bool rend_vk_swapchain_init(RendVkSwapchain* swapchain, const GapVector size) {
  if (!size.x || !size.y) {
    swapchain->size = size;
    return false;
  }

  dynarray_for_t(&swapchain->images, RendVkImage, img, { rend_vk_image_destroy(img); });
  dynarray_clear(&swapchain->images);

  VkDevice                 vkDevice    = swapchain->device->vkDevice;
  VkAllocationCallbacks*   vkAllocHost = swapchain->device->vkAllocHost;
  VkSurfaceCapabilitiesKHR vkCapabilities =
      rend_vk_surface_capabilities(swapchain->device, swapchain->vkSurface);

  VkSwapchainKHR           oldSwapchain = swapchain->vkSwapchain;
  VkSwapchainCreateInfoKHR createInfo   = {
      .sType              = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
      .surface            = swapchain->vkSurface,
      .minImageCount      = rend_vk_pick_imagecount(&vkCapabilities),
      .imageFormat        = swapchain->vkSurfaceFormat.format,
      .imageColorSpace    = swapchain->vkSurfaceFormat.colorSpace,
      .imageExtent.width  = (u32)size.x,
      .imageExtent.height = (u32)size.y,
      .imageArrayLayers   = 1,
      .imageUsage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
      .imageSharingMode   = VK_SHARING_MODE_EXCLUSIVE,
      .preTransform       = vkCapabilities.currentTransform,
      .compositeAlpha     = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
      .presentMode        = swapchain->vkPresentMode,
      .clipped            = true,
      .oldSwapchain       = oldSwapchain,
  };
  rend_vk_call(vkCreateSwapchainKHR, vkDevice, &createInfo, vkAllocHost, &swapchain->vkSwapchain);
  vkDestroySwapchainKHR(vkDevice, oldSwapchain, swapchain->device->vkAllocHost);

  u32 imageCount;
  rend_vk_call(vkGetSwapchainImagesKHR, vkDevice, swapchain->vkSwapchain, &imageCount, null);
  VkImage images[imageCount];
  rend_vk_call(vkGetSwapchainImagesKHR, vkDevice, swapchain->vkSwapchain, &imageCount, images);

  for (u32 i = 0; i != imageCount; ++i) {
    *dynarray_push_t(&swapchain->images, RendVkImage) = rend_vk_image_create_swapchain(
        swapchain->device, images[i], swapchain->vkSurfaceFormat.format, size);
  }

  swapchain->flags &= ~RendVkSwapchainFlags_OutOfDate;
  swapchain->size = size;
  ++swapchain->version;

  log_i(
      "Vulkan swapchain created",
      log_param("size", gap_vector_fmt(size)),
      log_param("format", fmt_text(rend_vk_format_info(swapchain->vkSurfaceFormat.format).name)),
      log_param("color", fmt_text(rend_vk_colorspace_str(swapchain->vkSurfaceFormat.colorSpace))),
      log_param("present-mode", fmt_text(rend_vk_presentmode_str(swapchain->vkPresentMode))),
      log_param("image-count", fmt_int(imageCount)),
      log_param("version", fmt_int(swapchain->version)));

  return true;
}

RendVkSwapchain* rend_vk_swapchain_create(RendVkDevice* dev, const GapWindowComp* window) {
  VkSurfaceKHR     vkSurface = rend_vk_surface_create(dev, window);
  RendVkSwapchain* swapchain = alloc_alloc_t(g_alloc_heap, RendVkSwapchain);
  *swapchain                 = (RendVkSwapchain){
      .device          = dev,
      .vkSurface       = vkSurface,
      .vkSurfaceFormat = rend_vk_pick_surface_format(dev, vkSurface),
      .vkPresentMode   = rend_vk_pick_presentmode(dev, vkSurface),
      .images          = dynarray_create_t(g_alloc_heap, RendVkImage, 2),
  };

  VkBool32 presentationSupported;
  rend_vk_call(
      vkGetPhysicalDeviceSurfaceSupportKHR,
      dev->vkPhysicalDevice,
      dev->mainQueueIndex,
      vkSurface,
      &presentationSupported);
  if (!presentationSupported) {
    diag_crash_msg("Vulkan device does not support presenting to the given surface");
  }
  return swapchain;
}

void rend_vk_swapchain_destroy(RendVkSwapchain* swapchain) {
  dynarray_for_t(&swapchain->images, RendVkImage, img, { rend_vk_image_destroy(img); });
  dynarray_destroy(&swapchain->images);

  if (swapchain->vkSwapchain) {
    vkDestroySwapchainKHR(
        swapchain->device->vkDevice, swapchain->vkSwapchain, swapchain->device->vkAllocHost);
  }

  vkDestroySurfaceKHR(
      swapchain->device->vkInstance, swapchain->vkSurface, swapchain->device->vkAllocHost);
  alloc_free_t(g_alloc_heap, swapchain);
}

VkFormat rend_vk_swapchain_format(const RendVkSwapchain* swapchain) {
  return swapchain->vkSurfaceFormat.format;
}

u64 rend_vk_swapchain_version(const RendVkSwapchain* swapchain) { return swapchain->version; }

u32 rend_vk_swapchain_imagecount(const RendVkSwapchain* swapchain) {
  return swapchain->images.size;
}

RendVkImage* rend_vk_swapchain_image(const RendVkSwapchain* swapchain, const RendSwapchainIdx idx) {
  diag_assert_msg(idx < swapchain->images.size, "Out of bound swapchain index");
  return dynarray_at_t(&swapchain->images, idx, RendVkImage);
}

RendSwapchainIdx
rend_vk_swapchain_acquire(RendVkSwapchain* swapchain, VkSemaphore available, const GapVector size) {

  const bool outOfDate = (swapchain->flags & RendVkSwapchainFlags_OutOfDate) != 0;
  if (!swapchain->vkSwapchain || outOfDate || !gap_vector_equal(size, swapchain->size)) {
    /**
     * Synchronize swapchain (re)creation by waiting for all rendering to be done. This a very
     * course way of synchronizing and causes stalls when resizing the window. In the future we can
     * consider keeping the old swapchain alive during recreation and only destroy it after all
     * rendering to it was finished.
     */
    vkDeviceWaitIdle(swapchain->device->vkDevice);

    if (!rend_vk_swapchain_init(swapchain, size)) {
      return sentinel_u32;
    }
  }

  if (!swapchain->size.x || !swapchain->size.y) {
    return sentinel_u32;
  }

  u32      index;
  VkResult result = vkAcquireNextImageKHR(
      swapchain->device->vkDevice, swapchain->vkSwapchain, u64_max, available, null, &index);

  switch (result) {
  case VK_SUBOPTIMAL_KHR:
    swapchain->flags |= RendVkSwapchainFlags_OutOfDate;
    log_d("Sub-optimal swapchain detected during acquire");
    return index;
  case VK_ERROR_OUT_OF_DATE_KHR:
    log_d("Out-of-date swapchain detected during acquire");
    swapchain->flags |= RendVkSwapchainFlags_OutOfDate;
    return sentinel_u32;
  default:
    rend_vk_check(string_lit("vkAcquireNextImageKHR"), result);
    return index;
  }
}

bool rend_vk_swapchain_present(
    RendVkSwapchain* swapchain, VkSemaphore ready, const RendSwapchainIdx idx) {

  VkPresentInfoKHR presentInfo = {
      .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores    = &ready,
      .swapchainCount     = 1,
      .pSwapchains        = &swapchain->vkSwapchain,
      .pImageIndices      = &idx,
  };

  VkResult result = vkQueuePresentKHR(swapchain->device->vkMainQueue, &presentInfo);
  switch (result) {
  case VK_SUBOPTIMAL_KHR:
    swapchain->flags |= RendVkSwapchainFlags_OutOfDate;
    log_d("Sub-optimal swapchain detected during present");
    return true; // Presenting will still succeed.
  case VK_ERROR_OUT_OF_DATE_KHR:
    swapchain->flags |= RendVkSwapchainFlags_OutOfDate;
    log_d("Out-of-date swapchain detected during present");
    return false; // Presenting will fail.
  default:
    rend_vk_check(string_lit("vkQueuePresentKHR"), result);
    return true;
  }
}
