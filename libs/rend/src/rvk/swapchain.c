#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_time.h"
#include "gap_native.h"
#include "log_logger.h"

#include "device_internal.h"
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
  RendPresentMode    presentModeSetting;
  RvkSwapchainFlags  flags;
  RvkSize            size;
  DynArray           images; // RvkImage[]
  TimeDuration       lastAcquireDur, lastPresentEnqueueDur, lastPresentWaitDur;
  u64                curPresentId; // NOTE: Present-id zero is unused.

#ifdef VK_KHR_present_wait
  PFN_vkWaitForPresentKHR vkWaitForPresentFunc;
#endif
};

static RvkSize rvk_surface_clamp_size(RvkSize size, const VkSurfaceCapabilitiesKHR* vkCaps) {
  if (size.width < vkCaps->minImageExtent.width) {
    size.width = vkCaps->minImageExtent.width;
  }
  if (size.height < vkCaps->minImageExtent.height) {
    size.height = vkCaps->minImageExtent.height;
  }
  if (size.width > vkCaps->maxImageExtent.width) {
    size.width = vkCaps->maxImageExtent.width;
  }
  if (size.height > vkCaps->maxImageExtent.height) {
    size.height = vkCaps->maxImageExtent.height;
  }
  return size;
}

static VkSurfaceKHR rvk_surface_create(RvkDevice* dev, const GapWindowComp* window) {
  VkSurfaceKHR result;
#if defined(VOLO_LINUX)
  const VkXcbSurfaceCreateInfoKHR createInfo = {
      .sType      = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
      .connection = (xcb_connection_t*)gap_native_app_handle(window),
      .window     = (xcb_window_t)gap_native_window_handle(window),
  };
  rvk_call(vkCreateXcbSurfaceKHR, dev->vkInst, &createInfo, &dev->vkAlloc, &result);
#elif defined(VOLO_WIN32)
  const VkWin32SurfaceCreateInfoKHR createInfo = {
      .sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
      .hinstance = (HINSTANCE)gap_native_app_handle(window),
      .hwnd      = (HWND)gap_native_window_handle(window),
  };
  rvk_call(vkCreateWin32SurfaceKHR, dev->vkInst, &createInfo, &dev->vkAlloc, &result);
#endif
  return result;
}

static VkSurfaceFormatKHR rvk_pick_surface_format(RvkDevice* dev, VkSurfaceKHR vkSurf) {
  u32 formatCount;
  rvk_call(vkGetPhysicalDeviceSurfaceFormatsKHR, dev->vkPhysDev, vkSurf, &formatCount, null);
  if (!formatCount) {
    diag_crash_msg("No Vulkan surface formats available");
  }
  VkSurfaceFormatKHR* formats = alloc_array_t(g_alloc_scratch, VkSurfaceFormatKHR, formatCount);
  rvk_call(vkGetPhysicalDeviceSurfaceFormatsKHR, dev->vkPhysDev, vkSurf, &formatCount, formats);

  // Prefer srgb, so the gpu can itself perform the linear to srgb conversion.
  for (u32 i = 0; i != formatCount; ++i) {
    if (formats[i].format == VK_FORMAT_B8G8R8A8_SRGB) {
      return formats[i];
    }
  }

  log_w("No SRGB surface format available");
  return formats[0];
}

static u32
rvk_pick_imagecount(const VkSurfaceCapabilitiesKHR* caps, const RendSettingsComp* settings) {
  u32 imgCount;
  switch (settings->presentMode) {
  case RendPresentMode_Immediate:
    imgCount = 2; // one on-screen, and one being rendered to.
    break;
  case RendPresentMode_VSync:
  case RendPresentMode_VSyncRelaxed:
  case RendPresentMode_Mailbox:
    imgCount = 3; // one on-screen, one ready, and one being rendered to.
    break;
  }
  if (caps->minImageCount > imgCount) {
    imgCount = caps->minImageCount;
  }
  if (caps->maxImageCount && caps->maxImageCount < imgCount) {
    imgCount = caps->maxImageCount;
  }
  return imgCount;
}

static VkPresentModeKHR rvk_preferred_presentmode(const RendPresentMode setting) {
  switch (setting) {
  case RendPresentMode_Immediate:
    return VK_PRESENT_MODE_IMMEDIATE_KHR;
  case RendPresentMode_VSync:
    return VK_PRESENT_MODE_FIFO_KHR;
  case RendPresentMode_VSyncRelaxed:
    return VK_PRESENT_MODE_FIFO_RELAXED_KHR;
  case RendPresentMode_Mailbox:
    return VK_PRESENT_MODE_MAILBOX_KHR;
  }
  diag_crash();
}

static VkPresentModeKHR
rvk_pick_presentmode(RvkDevice* dev, const RendSettingsComp* settings, const VkSurfaceKHR vkSurf) {
  VkPresentModeKHR available[32];
  u32              availableCount = array_elems(available);
  rvk_call(
      vkGetPhysicalDeviceSurfacePresentModesKHR,
      dev->vkPhysDev,
      vkSurf,
      &availableCount,
      available);

  const VkPresentModeKHR preferred = rvk_preferred_presentmode(settings->presentMode);
  for (u32 i = 0; i != availableCount; ++i) {
    if (available[i] == preferred) {
      return available[i];
    }
  }
  log_w(
      "Preferred present mode unavailable",
      log_param("preferred", fmt_text(rvk_presentmode_str(preferred))),
      log_param("fallback", fmt_text(rvk_presentmode_str(VK_PRESENT_MODE_FIFO_KHR))));

  return VK_PRESENT_MODE_FIFO_KHR; // FIFO is required by the spec to be always available.
}

static VkSurfaceCapabilitiesKHR rvk_surface_capabilities(RvkDevice* dev, VkSurfaceKHR vkSurf) {
  VkSurfaceCapabilitiesKHR result;
  rvk_call(vkGetPhysicalDeviceSurfaceCapabilitiesKHR, dev->vkPhysDev, vkSurf, &result);
  return result;
}

static bool
rvk_swapchain_init(RvkSwapchain* swapchain, const RendSettingsComp* settings, RvkSize size) {
  if (!size.width || !size.height) {
    swapchain->size = size;
    return false;
  }

  dynarray_for_t(&swapchain->images, RvkImage, img) { rvk_image_destroy(img, swapchain->dev); }
  dynarray_clear(&swapchain->images);

  const VkDevice           vkDev   = swapchain->dev->vkDev;
  VkAllocationCallbacks*   vkAlloc = &swapchain->dev->vkAlloc;
  VkSurfaceCapabilitiesKHR vkCaps  = rvk_surface_capabilities(swapchain->dev, swapchain->vkSurf);
  size                             = rvk_surface_clamp_size(size, &vkCaps);

  const VkPresentModeKHR presentMode =
      rvk_pick_presentmode(swapchain->dev, settings, swapchain->vkSurf);

  const VkSwapchainKHR           oldSwapchain = swapchain->vkSwapchain;
  const VkSwapchainCreateInfoKHR createInfo   = {
      .sType              = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
      .surface            = swapchain->vkSurf,
      .minImageCount      = rvk_pick_imagecount(&vkCaps, settings),
      .imageFormat        = swapchain->vkSurfFormat.format,
      .imageColorSpace    = swapchain->vkSurfFormat.colorSpace,
      .imageExtent.width  = size.width,
      .imageExtent.height = size.height,
      .imageArrayLayers   = 1,
      .imageUsage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      .imageSharingMode   = VK_SHARING_MODE_EXCLUSIVE,
      .preTransform       = vkCaps.currentTransform,
      .compositeAlpha     = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
      .presentMode        = presentMode,
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

  const VkFormat format = swapchain->vkSurfFormat.format;
  for (u32 i = 0; i != imageCount; ++i) {
    RvkImage* img = dynarray_push_t(&swapchain->images, RvkImage);
    *img          = rvk_image_create_swapchain(swapchain->dev, images[i], format, size);
    rvk_debug_name_img(swapchain->dev->debug, img->vkImage, "swapchain_{}", fmt_int(i));
  }

  swapchain->flags &= ~RvkSwapchainFlags_OutOfDate;
  swapchain->presentModeSetting = settings->presentMode;
  swapchain->size               = size;

  log_i(
      "Vulkan swapchain created",
      log_param("size", rvk_size_fmt(size)),
      log_param("format", fmt_text(rvk_format_info(format).name)),
      log_param("color", fmt_text(rvk_colorspace_str(swapchain->vkSurfFormat.colorSpace))),
      log_param("present-mode", fmt_text(rvk_presentmode_str(presentMode))),
      log_param("image-count", fmt_int(imageCount)));

  return true;
}

RvkSwapchain* rvk_swapchain_create(RvkDevice* dev, const GapWindowComp* window) {
  VkSurfaceKHR  vkSurf    = rvk_surface_create(dev, window);
  RvkSwapchain* swapchain = alloc_alloc_t(g_alloc_heap, RvkSwapchain);
  *swapchain              = (RvkSwapchain){
      .dev          = dev,
      .vkSurf       = vkSurf,
      .vkSurfFormat = rvk_pick_surface_format(dev, vkSurf),
      .images       = dynarray_create_t(g_alloc_heap, RvkImage, 3),
  };

  VkBool32 supported;
  rvk_call(
      vkGetPhysicalDeviceSurfaceSupportKHR,
      dev->vkPhysDev,
      dev->graphicsQueueIndex,
      vkSurf,
      &supported);
  if (!supported) {
    diag_crash_msg("Vulkan device does not support presenting to the given surface");
  }

#ifdef VK_KHR_present_wait
  if (dev->flags & RvkDeviceFlags_SupportPresentWait) {
    swapchain->vkWaitForPresentFunc = rvk_func_load_instance(dev->vkInst, vkWaitForPresentKHR);
  }
#endif
  return swapchain;
}

void rvk_swapchain_destroy(RvkSwapchain* swapchain) {
  dynarray_for_t(&swapchain->images, RvkImage, img) { rvk_image_destroy(img, swapchain->dev); }
  dynarray_destroy(&swapchain->images);

  if (swapchain->vkSwapchain) {
    vkDestroySwapchainKHR(swapchain->dev->vkDev, swapchain->vkSwapchain, &swapchain->dev->vkAlloc);
  }

  vkDestroySurfaceKHR(swapchain->dev->vkInst, swapchain->vkSurf, &swapchain->dev->vkAlloc);
  alloc_free_t(g_alloc_heap, swapchain);
}

RvkSize rvk_swapchain_size(const RvkSwapchain* swapchain) { return swapchain->size; }

RvkSwapchainStats rvk_swapchain_stats(const RvkSwapchain* swapchain) {
  return (RvkSwapchainStats){
      .acquireDur        = swapchain->lastAcquireDur,
      .presentEnqueueDur = swapchain->lastPresentEnqueueDur,
      .presentWaitDur    = swapchain->lastPresentWaitDur,
  };
}

RvkImage* rvk_swapchain_image(const RvkSwapchain* swapchain, const RvkSwapchainIdx idx) {
  diag_assert_msg(idx < swapchain->images.size, "Out of bound swapchain index");
  return dynarray_at_t(&swapchain->images, idx, RvkImage);
}

RvkSwapchainIdx rvk_swapchain_acquire(
    RvkSwapchain*           swapchain,
    const RendSettingsComp* settings,
    VkSemaphore             available,
    const RvkSize           size) {

  const bool outOfDate      = (swapchain->flags & RvkSwapchainFlags_OutOfDate) != 0;
  const bool changedSize    = !rvk_size_equal(size, swapchain->size);
  const bool changedPresent = swapchain->presentModeSetting != settings->presentMode;
  if (!swapchain->vkSwapchain || outOfDate || changedSize || changedPresent) {
    /**
     * Synchronize swapchain (re)creation by waiting for all rendering to be done. This a very
     * crude way of synchronizing and causes stalls when resizing the window. In the future we can
     * consider keeping the old swapchain alive during recreation and only destroy it after all
     * rendering to it was finished.
     */
    rvk_device_wait_idle(swapchain->dev);

    if (!rvk_swapchain_init(swapchain, settings, size)) {
      return sentinel_u32;
    }
  }

  if (!swapchain->size.width || !swapchain->size.height) {
    return sentinel_u32;
  }

  const TimeSteady acquireStart = time_steady_clock();
  u32              index;
  VkResult         result = vkAcquireNextImageKHR(
      swapchain->dev->vkDev, swapchain->vkSwapchain, u64_max, available, null, &index);
  swapchain->lastAcquireDur = time_steady_duration(acquireStart, time_steady_clock());

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

bool rvk_swapchain_enqueue_present(
    RvkSwapchain* swapchain, VkSemaphore ready, const RvkSwapchainIdx idx) {
  RvkImage* image = rvk_swapchain_image(swapchain, idx);
  rvk_image_assert_phase(image, RvkImagePhase_Present);

  const void* nextPresentData = null;

  ++swapchain->curPresentId;
#ifdef VK_KHR_present_id
  const VkPresentIdKHR presentIdData = {
      .sType          = VK_STRUCTURE_TYPE_PRESENT_ID_KHR,
      .pNext          = nextPresentData,
      .swapchainCount = 1,
      .pPresentIds    = &swapchain->curPresentId,
  };
  if (swapchain->dev->flags & RvkDeviceFlags_SupportPresentId) {
    nextPresentData = &presentIdData;
  }
#endif
  const VkPresentInfoKHR presentInfo = {
      .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .pNext              = nextPresentData,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores    = &ready,
      .swapchainCount     = 1,
      .pSwapchains        = &swapchain->vkSwapchain,
      .pImageIndices      = &idx,
  };

  const TimeSteady startTime = time_steady_clock();
  VkResult         result    = vkQueuePresentKHR(swapchain->dev->vkGraphicsQueue, &presentInfo);
  swapchain->lastPresentEnqueueDur = time_steady_duration(startTime, time_steady_clock());

  switch (result) {
  case VK_SUBOPTIMAL_KHR:
    swapchain->flags |= RvkSwapchainFlags_OutOfDate;
    log_d(
        "Sub-optimal swapchain detected during present",
        log_param("id", fmt_int(swapchain->curPresentId)));
    return true; // Presenting will still succeed.
  case VK_ERROR_OUT_OF_DATE_KHR:
    swapchain->flags |= RvkSwapchainFlags_OutOfDate;
    log_d(
        "Out-of-date swapchain detected during present",
        log_param("id", fmt_int(swapchain->curPresentId)));
    return false; // Presenting will fail.
  default:
    rvk_check(string_lit("vkQueuePresentKHR"), result);
    return true;
  }
}

void rvk_swapchain_wait_for_present(RvkSwapchain* swapchain, const u32 numBehind) {
  if (numBehind >= swapchain->curPresentId) {
    /**
     * Out of bound presentation-ids are considered to be already presented.
     * This is convenient for the calling code as it doesn't need the special case the first frame.
     */
    return;
  }
#ifdef VK_KHR_present_wait
  if (swapchain->vkWaitForPresentFunc) {
    const TimeSteady startTime = time_steady_clock();

    VkResult result = swapchain->vkWaitForPresentFunc(
        swapchain->dev->vkDev,
        swapchain->vkSwapchain,
        swapchain->curPresentId - numBehind,
        u64_max);

    swapchain->lastPresentWaitDur = time_steady_duration(startTime, time_steady_clock());

    switch (result) {
    case VK_ERROR_OUT_OF_DATE_KHR:
      swapchain->flags |= RvkSwapchainFlags_OutOfDate;
      log_d(
          "Out-of-date swapchain detected during wait",
          log_param("id", fmt_int(swapchain->curPresentId)));
      break;
    default:
      rvk_check(string_lit("vkWaitForPresentKHR"), result);
    }
  }
#endif
}
