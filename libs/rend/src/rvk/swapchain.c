#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_time.h"
#include "gap_native.h"
#include "log_logger.h"

#include "debug_internal.h"
#include "device_internal.h"
#include "image_internal.h"
#include "swapchain_internal.h"

#define swapchain_images_max 5

typedef enum {
  RvkSwapchainFlags_OutOfDate = 1 << 0,
} RvkSwapchainFlags;

struct sRvkSwapchain {
  RvkDevice*         dev;
  VkSurfaceKHR       vkSurf;
  VkSurfaceFormatKHR vkSurfFormat;
  VkSwapchainKHR     vkSwapchain;
  RendPresentMode    presentModeSetting;
  RvkSwapchainFlags  flags;
  RvkSize            size;
  u32                imageCount;
  RvkImage           images[swapchain_images_max];

  TimeDuration lastAcquireDur, lastPresentEnqueueDur, lastPresentWaitDur;
  u64          curPresentId; // NOTE: Present-id zero is unused.

#ifdef VK_KHR_present_wait
  PFN_vkWaitForPresentKHR vkWaitForPresentFunc;
#endif
};

static RvkSize rvk_surface_clamp_size(RvkSize size, const VkSurfaceCapabilitiesKHR* vkCaps) {
  if (size.width < vkCaps->minImageExtent.width) {
    size.width = (u16)vkCaps->minImageExtent.width;
  }
  if (size.height < vkCaps->minImageExtent.height) {
    size.height = (u16)vkCaps->minImageExtent.height;
  }
  if (size.width > vkCaps->maxImageExtent.width) {
    size.width = (u16)vkCaps->maxImageExtent.width;
  }
  if (size.height > vkCaps->maxImageExtent.height) {
    size.height = (u16)vkCaps->maxImageExtent.height;
  }
  return size;
}

static VkSurfaceKHR rvk_surface_create(RvkDevice* dev, const GapWindowComp* window) {
  VkSurfaceKHR result;
#if defined(VOLO_LINUX)
  const VkXcbSurfaceCreateInfoKHR createInfo = {
      .sType      = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
      .connection = gap_native_app_handle(window),
      .window     = gap_native_window_handle(window),
  };
  rvk_call(vkCreateXcbSurfaceKHR, dev->vkInst, &createInfo, &dev->vkAlloc, &result);
#elif defined(VOLO_WIN32)
  const VkWin32SurfaceCreateInfoKHR createInfo = {
      .sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
      .hinstance = gap_native_app_handle(window),
      .hwnd      = gap_native_window_handle(window),
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
  VkSurfaceFormatKHR* surfFormats = mem_stack(sizeof(VkSurfaceFormatKHR) * formatCount).ptr;
  rvk_call(vkGetPhysicalDeviceSurfaceFormatsKHR, dev->vkPhysDev, vkSurf, &formatCount, surfFormats);

  // Check if the preferred swapchain format is available.
  for (u32 i = 0; i != formatCount; ++i) {
    if (surfFormats[i].format == dev->preferredSwapchainFormat) {
      return surfFormats[i];
    }
  }

  log_w(
      "Preferred swapchain format not available",
      log_param("fallback", fmt_text(rvk_format_info(surfFormats[0].format).name)));

  return surfFormats[0];
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
  if (imgCount < caps->minImageCount) {
    imgCount = caps->minImageCount;
  }
  if (caps->maxImageCount && imgCount > caps->maxImageCount) {
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
      log_param("preferred", fmt_text(vkPresentModeKHRStr(preferred))),
      log_param("fallback", fmt_text(vkPresentModeKHRStr(VK_PRESENT_MODE_FIFO_KHR))));

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

  for (u32 i = 0; i != swapchain->imageCount; ++i) {
    rvk_image_destroy(&swapchain->images[i], swapchain->dev);
  }

  const VkDevice           vkDev   = swapchain->dev->vkDev;
  VkAllocationCallbacks*   vkAlloc = &swapchain->dev->vkAlloc;
  VkSurfaceCapabilitiesKHR vkCaps  = rvk_surface_capabilities(swapchain->dev, swapchain->vkSurf);
  size                             = rvk_surface_clamp_size(size, &vkCaps);

  const VkPresentModeKHR presentMode =
      rvk_pick_presentmode(swapchain->dev, settings, swapchain->vkSurf);

  const VkSwapchainKHR oldSwapchain = swapchain->vkSwapchain;

  const VkSwapchainCreateInfoKHR createInfo = {
      .sType              = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
      .surface            = swapchain->vkSurf,
      .minImageCount      = rvk_pick_imagecount(&vkCaps, settings),
      .imageFormat        = swapchain->vkSurfFormat.format,
      .imageColorSpace    = swapchain->vkSurfFormat.colorSpace,
      .imageExtent.width  = size.width,
      .imageExtent.height = size.height,
      .imageArrayLayers   = 1,
      .imageUsage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
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

  rvk_call(vkGetSwapchainImagesKHR, vkDev, swapchain->vkSwapchain, &swapchain->imageCount, null);
  if (UNLIKELY(swapchain->imageCount > swapchain_images_max)) {
    diag_crash_msg("Vulkan surface uses more swapchain images then are supported");
  }

  VkImage vkImgs[swapchain_images_max];
  rvk_call(vkGetSwapchainImagesKHR, vkDev, swapchain->vkSwapchain, &swapchain->imageCount, vkImgs);

  const VkFormat format = swapchain->vkSurfFormat.format;
  for (u32 i = 0; i != swapchain->imageCount; ++i) {
    swapchain->images[i] = rvk_image_create_swapchain(swapchain->dev, vkImgs[i], format, size);
    rvk_debug_name_img(swapchain->dev->debug, vkImgs[i], "swapchain_{}", fmt_int(i));
  }

  swapchain->flags &= ~RvkSwapchainFlags_OutOfDate;
  swapchain->presentModeSetting = settings->presentMode;
  swapchain->size               = size;

  log_i(
      "Vulkan swapchain created",
      log_param("size", rvk_size_fmt(size)),
      log_param("format", fmt_text(rvk_format_info(format).name)),
      log_param("color", fmt_text(vkColorSpaceKHRStr(swapchain->vkSurfFormat.colorSpace))),
      log_param("present-mode", fmt_text(vkPresentModeKHRStr(presentMode))),
      log_param("image-count", fmt_int(swapchain->imageCount)));

  return true;
}

RvkSwapchain* rvk_swapchain_create(RvkDevice* dev, const GapWindowComp* window) {
  VkSurfaceKHR  vkSurf    = rvk_surface_create(dev, window);
  RvkSwapchain* swapchain = alloc_alloc_t(g_allocHeap, RvkSwapchain);

  *swapchain = (RvkSwapchain){
      .dev          = dev,
      .vkSurf       = vkSurf,
      .vkSurfFormat = rvk_pick_surface_format(dev, vkSurf),
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
  for (u32 i = 0; i != swapchain->imageCount; ++i) {
    rvk_image_destroy(&swapchain->images[i], swapchain->dev);
  }
  if (swapchain->vkSwapchain) {
    vkDestroySwapchainKHR(swapchain->dev->vkDev, swapchain->vkSwapchain, &swapchain->dev->vkAlloc);
  }

  vkDestroySurfaceKHR(swapchain->dev->vkInst, swapchain->vkSurf, &swapchain->dev->vkAlloc);
  alloc_free_t(g_allocHeap, swapchain);
}

VkFormat rvk_swapchain_format(const RvkSwapchain* swapchain) {
  return swapchain->vkSurfFormat.format;
}

RvkSize rvk_swapchain_size(const RvkSwapchain* swapchain) { return swapchain->size; }

void rvk_swapchain_stats(const RvkSwapchain* swapchain, RvkSwapchainStats* out) {
  out->acquireDur        = swapchain->lastAcquireDur;
  out->presentEnqueueDur = swapchain->lastPresentEnqueueDur;
  out->presentWaitDur    = swapchain->lastPresentWaitDur;
  out->presentId         = swapchain->curPresentId;
  out->imageCount        = (u16)swapchain->imageCount;
}

void rvk_swapchain_invalidate(RvkSwapchain* swapchain) {
  swapchain->flags |= RvkSwapchainFlags_OutOfDate;
}

RvkImage* rvk_swapchain_image(RvkSwapchain* swapchain, const RvkSwapchainIdx idx) {
  diag_assert_msg(idx < swapchain->imageCount, "Swapchain index {} is out of bounds", fmt_int(idx));
  return &swapchain->images[idx];
}

bool rvk_swapchain_prepare(
    RvkSwapchain* swapchain, const RendSettingsComp* settings, const RvkSize size) {

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
      return false;
    }
  }

  if (!swapchain->size.width || !swapchain->size.height) {
    return false;
  }

  return true;
}

RvkSwapchainIdx rvk_swapchain_acquire(RvkSwapchain* swapchain, VkSemaphore available) {
  const TimeSteady acquireStart = time_steady_clock();
  u32              index;
  const VkResult   result = vkAcquireNextImageKHR(
      swapchain->dev->vkDev, swapchain->vkSwapchain, u64_max, available, null, &index);
  swapchain->lastAcquireDur = time_steady_duration(acquireStart, time_steady_clock());

  switch (result) {
  case VK_SUBOPTIMAL_KHR:
    swapchain->flags |= RvkSwapchainFlags_OutOfDate;
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

void rvk_swapchain_wait_for_present(const RvkSwapchain* swapchain, const u32 numBehind) {
  if (numBehind >= swapchain->curPresentId) {
    /**
     * Out of bound presentation-ids are considered to be already presented.
     * This is convenient for the calling code as it doesn't need the special case the first frame.
     */
    return;
  }
#ifdef VK_KHR_present_wait
  if (swapchain->vkWaitForPresentFunc) {
    RvkSwapchain*    mutableSwapchain = (RvkSwapchain*)swapchain;
    const TimeSteady startTime        = time_steady_clock();

    const TimeDuration timeout = time_second / 30;
    VkResult           result  = swapchain->vkWaitForPresentFunc(
        swapchain->dev->vkDev,
        swapchain->vkSwapchain,
        swapchain->curPresentId - numBehind,
        timeout);

    mutableSwapchain->lastPresentWaitDur = time_steady_duration(startTime, time_steady_clock());

    switch (result) {
    case VK_TIMEOUT:
      /**
       * Maximum wait-time has elapsed, either GPU is producing frames VERY slow or the driver
       * decided not to present this image.
       */
      break;
    case VK_SUBOPTIMAL_KHR:
      mutableSwapchain->flags |= RvkSwapchainFlags_OutOfDate;
      // Presenting still succeeded.
      break;
    case VK_ERROR_OUT_OF_DATE_KHR:
      mutableSwapchain->flags |= RvkSwapchainFlags_OutOfDate;
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
