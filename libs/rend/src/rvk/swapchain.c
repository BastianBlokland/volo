#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_time.h"
#include "gap_native.h"
#include "log_logger.h"

#include "debug_internal.h"
#include "device_internal.h"
#include "image_internal.h"
#include "lib_internal.h"
#include "swapchain_internal.h"

#define swapchain_images_max 5

typedef enum {
  RvkSwapchainFlags_OutOfDate = 1 << 0,
} RvkSwapchainFlags;

struct sRvkSwapchain {
  RvkLib*            lib;
  RvkDevice*         dev;
  VkSurfaceKHR       vkSurf;
  VkSurfaceFormatKHR vkSurfFormat;
  VkSwapchainKHR     vkSwap;
  RendPresentMode    presentModeSetting;
  RvkSwapchainFlags  flags;
  RvkSize            size;
  u32                imgCount;
  RvkImage           imgs[swapchain_images_max];

  TimeDuration lastAcquireDur, lastPresentEnqueueDur, lastPresentWaitDur;
  u64          curPresentId; // NOTE: Present-id zero is unused.
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

static VkSurfaceKHR rvk_surface_create(RvkLib* lib, const GapWindowComp* window) {
  VkSurfaceKHR result;
  switch (gap_native_wm()) {
  case GapNativeWm_Xcb: {
    const VkXcbSurfaceCreateInfoKHR createInfo = {
        .sType      = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
        .connection = gap_native_app_handle(window),
        .window     = gap_native_window_handle(window),
    };
    rvk_call(lib->api, createXcbSurfaceKHR, lib->vkInst, &createInfo, &lib->vkAlloc, &result);
  } break;
  case GapNativeWm_Win32: {
    const VkWin32SurfaceCreateInfoKHR createInfo = {
        .sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
        .hinstance = gap_native_app_handle(window),
        .hwnd      = gap_native_window_handle(window),
    };
    rvk_call(lib->api, createWin32SurfaceKHR, lib->vkInst, &createInfo, &lib->vkAlloc, &result);
  } break;
  }
  return result;
}

static VkSurfaceFormatKHR rvk_pick_surface_format(RvkLib* lib, RvkDevice* dev, VkSurfaceKHR surf) {
  u32 count;
  rvk_call(lib->api, getPhysicalDeviceSurfaceFormatsKHR, dev->vkPhysDev, surf, &count, null);
  if (!count) {
    diag_crash_msg("No Vulkan surface formats available");
  }
  VkSurfaceFormatKHR* formats = mem_stack(sizeof(VkSurfaceFormatKHR) * count).ptr;
  rvk_call(lib->api, getPhysicalDeviceSurfaceFormatsKHR, dev->vkPhysDev, surf, &count, formats);

  // Check if the preferred swapchain format is available.
  for (u32 i = 0; i != count; ++i) {
    if (formats[i].format == dev->preferredSwapchainFormat) {
      return formats[i];
    }
  }

  log_w(
      "Preferred swapchain format not available",
      log_param("fallback", fmt_text(vkFormatStr(formats[0].format))));

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

static VkPresentModeKHR rvk_pick_presentmode(
    RvkLib* lib, RvkDevice* dev, const RendSettingsComp* settings, const VkSurfaceKHR surf) {
  VkPresentModeKHR available[32];
  u32              availableCount = array_elems(available);
  rvk_call(
      lib->api,
      getPhysicalDeviceSurfacePresentModesKHR,
      dev->vkPhysDev,
      surf,
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

static VkSurfaceCapabilitiesKHR rvk_surface_caps(RvkLib* lib, RvkDevice* dev, VkSurfaceKHR surf) {
  VkSurfaceCapabilitiesKHR result;
  rvk_call(lib->api, getPhysicalDeviceSurfaceCapabilitiesKHR, dev->vkPhysDev, surf, &result);
  return result;
}

static bool rvk_swapchain_init(RvkSwapchain* swap, const RendSettingsComp* settings, RvkSize size) {
  if (!size.width || !size.height) {
    swap->size = size;
    return false;
  }

  for (u32 i = 0; i != swap->imgCount; ++i) {
    rvk_image_destroy(&swap->imgs[i], swap->dev);
  }

  const VkDevice           vkDev   = swap->dev->vkDev;
  VkAllocationCallbacks*   vkAlloc = &swap->dev->vkAlloc;
  VkSurfaceCapabilitiesKHR vkCaps  = rvk_surface_caps(swap->lib, swap->dev, swap->vkSurf);
  size                             = rvk_surface_clamp_size(size, &vkCaps);

  const VkPresentModeKHR presentMode =
      rvk_pick_presentmode(swap->lib, swap->dev, settings, swap->vkSurf);

  const VkSwapchainKHR oldSwapchain = swap->vkSwap;

  const VkSwapchainCreateInfoKHR createInfo = {
      .sType              = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
      .surface            = swap->vkSurf,
      .minImageCount      = rvk_pick_imagecount(&vkCaps, settings),
      .imageFormat        = swap->vkSurfFormat.format,
      .imageColorSpace    = swap->vkSurfFormat.colorSpace,
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

  rvk_call(swap->dev->api, createSwapchainKHR, vkDev, &createInfo, vkAlloc, &swap->vkSwap);
  if (oldSwapchain) {
    swap->dev->api.destroySwapchainKHR(vkDev, oldSwapchain, &swap->dev->vkAlloc);
  }

  rvk_call(swap->dev->api, getSwapchainImagesKHR, vkDev, swap->vkSwap, &swap->imgCount, null);
  if (UNLIKELY(swap->imgCount > swapchain_images_max)) {
    diag_crash_msg("Vulkan surface uses more swapchain images then are supported");
  }

  VkImage vkImgs[swapchain_images_max];
  rvk_call(swap->dev->api, getSwapchainImagesKHR, vkDev, swap->vkSwap, &swap->imgCount, vkImgs);

  const VkFormat format = swap->vkSurfFormat.format;
  for (u32 i = 0; i != swap->imgCount; ++i) {
    swap->imgs[i] = rvk_image_create_swapchain(swap->dev, vkImgs[i], format, size);
    rvk_debug_name_img(swap->dev->debug, vkImgs[i], "swapchain_{}", fmt_int(i));
  }

  swap->flags &= ~RvkSwapchainFlags_OutOfDate;
  swap->presentModeSetting = settings->presentMode;
  swap->size               = size;

  log_i(
      "Vulkan swapchain created",
      log_param("size", rvk_size_fmt(size)),
      log_param("format", fmt_text(vkFormatStr(format))),
      log_param("color", fmt_text(vkColorSpaceKHRStr(swap->vkSurfFormat.colorSpace))),
      log_param("present-mode", fmt_text(vkPresentModeKHRStr(presentMode))),
      log_param("image-count", fmt_int(swap->imgCount)));

  return true;
}

RvkSwapchain* rvk_swapchain_create(RvkLib* lib, RvkDevice* dev, const GapWindowComp* window) {
  VkSurfaceKHR  vkSurf = rvk_surface_create(lib, window);
  RvkSwapchain* swap   = alloc_alloc_t(g_allocHeap, RvkSwapchain);

  *swap = (RvkSwapchain){
      .lib          = lib,
      .dev          = dev,
      .vkSurf       = vkSurf,
      .vkSurfFormat = rvk_pick_surface_format(lib, dev, vkSurf),
  };

  VkBool32 supported;
  rvk_call(
      lib->api,
      getPhysicalDeviceSurfaceSupportKHR,
      dev->vkPhysDev,
      dev->graphicsQueueIndex,
      vkSurf,
      &supported);
  if (!supported) {
    diag_crash_msg("Vulkan device does not support presenting to the given surface");
  }

  return swap;
}

void rvk_swapchain_destroy(RvkSwapchain* swap) {
  for (u32 i = 0; i != swap->imgCount; ++i) {
    rvk_image_destroy(&swap->imgs[i], swap->dev);
  }
  if (swap->vkSwap) {
    swap->dev->api.destroySwapchainKHR(swap->dev->vkDev, swap->vkSwap, &swap->dev->vkAlloc);
  }

  swap->lib->api.destroySurfaceKHR(swap->lib->vkInst, swap->vkSurf, &swap->dev->vkAlloc);
  alloc_free_t(g_allocHeap, swap);
}

VkFormat rvk_swapchain_format(const RvkSwapchain* swap) { return swap->vkSurfFormat.format; }

RvkSize rvk_swapchain_size(const RvkSwapchain* swap) { return swap->size; }

void rvk_swapchain_stats(const RvkSwapchain* swap, RvkSwapchainStats* out) {
  out->acquireDur        = swap->lastAcquireDur;
  out->presentEnqueueDur = swap->lastPresentEnqueueDur;
  out->presentWaitDur    = swap->lastPresentWaitDur;
  out->presentId         = swap->curPresentId;
  out->imageCount        = (u16)swap->imgCount;
}

void rvk_swapchain_invalidate(RvkSwapchain* swap) { swap->flags |= RvkSwapchainFlags_OutOfDate; }

RvkImage* rvk_swapchain_image(RvkSwapchain* swap, const RvkSwapchainIdx idx) {
  diag_assert_msg(idx < swap->imgCount, "Swapchain index {} is out of bounds", fmt_int(idx));
  return &swap->imgs[idx];
}

bool rvk_swapchain_prepare(
    RvkSwapchain* swap, const RendSettingsComp* settings, const RvkSize size) {

  const bool outOfDate      = (swap->flags & RvkSwapchainFlags_OutOfDate) != 0;
  const bool changedSize    = !rvk_size_equal(size, swap->size);
  const bool changedPresent = swap->presentModeSetting != settings->presentMode;

  if (!swap->vkSwap || outOfDate || changedSize || changedPresent) {
    /**
     * Synchronize swapchain (re)creation by waiting for all rendering to be done. This a very
     * crude way of synchronizing and causes stalls when resizing the window. In the future we can
     * consider keeping the old swapchain alive during recreation and only destroy it after all
     * rendering to it was finished.
     */
    rvk_device_wait_idle(swap->dev);

    if (!rvk_swapchain_init(swap, settings, size)) {
      return false;
    }
  }

  if (!swap->size.width || !swap->size.height) {
    return false;
  }

  return true;
}

RvkSwapchainIdx rvk_swapchain_acquire(RvkSwapchain* swap, VkSemaphore available) {
  const TimeSteady acquireStart = time_steady_clock();

  u32            index;
  const VkResult result = swap->dev->api.acquireNextImageKHR(
      swap->dev->vkDev, swap->vkSwap, u64_max, available, null, &index);

  swap->lastAcquireDur = time_steady_duration(acquireStart, time_steady_clock());

  switch (result) {
  case VK_SUBOPTIMAL_KHR:
    swap->flags |= RvkSwapchainFlags_OutOfDate;
    return index;
  case VK_ERROR_OUT_OF_DATE_KHR:
    log_d("Out-of-date swapchain detected during acquire");
    swap->flags |= RvkSwapchainFlags_OutOfDate;
    return sentinel_u32;
  default:
    rvk_check(string_lit("vkAcquireNextImageKHR"), result);
    return index;
  }
}

bool rvk_swapchain_enqueue_present(
    RvkSwapchain* swap, VkSemaphore ready, const RvkSwapchainIdx idx) {
  RvkImage* image = rvk_swapchain_image(swap, idx);
  rvk_image_assert_phase(image, RvkImagePhase_Present);

  const void* nextPresentData = null;

  ++swap->curPresentId;

  const VkPresentIdKHR presentIdData = {
      .sType          = VK_STRUCTURE_TYPE_PRESENT_ID_KHR,
      .pNext          = nextPresentData,
      .swapchainCount = 1,
      .pPresentIds    = &swap->curPresentId,
  };
  if (swap->dev->flags & RvkDeviceFlags_SupportPresentId) {
    nextPresentData = &presentIdData;
  }

  const VkPresentInfoKHR presentInfo = {
      .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .pNext              = nextPresentData,
      .waitSemaphoreCount = 1,
      .pWaitSemaphores    = &ready,
      .swapchainCount     = 1,
      .pSwapchains        = &swap->vkSwap,
      .pImageIndices      = &idx,
  };

  const TimeSteady startTime = time_steady_clock();
  VkResult result = swap->dev->api.queuePresentKHR(swap->dev->vkGraphicsQueue, &presentInfo);
  swap->lastPresentEnqueueDur = time_steady_duration(startTime, time_steady_clock());

  switch (result) {
  case VK_SUBOPTIMAL_KHR:
    swap->flags |= RvkSwapchainFlags_OutOfDate;
    return true; // Presenting will still succeed.
  case VK_ERROR_OUT_OF_DATE_KHR:
    swap->flags |= RvkSwapchainFlags_OutOfDate;
    log_d(
        "Out-of-date swapchain detected during present",
        log_param("id", fmt_int(swap->curPresentId)));
    return false; // Presenting will fail.
  default:
    rvk_check(string_lit("vkQueuePresentKHR"), result);
    return true;
  }
}

void rvk_swapchain_wait_for_present(const RvkSwapchain* swap, const u32 numBehind) {
  if (numBehind >= swap->curPresentId) {
    /**
     * Out of bound presentation-ids are considered to be already presented.
     * This is convenient for the calling code as it doesn't need the special case the first frame.
     */
    return;
  }
  if (swap->dev->api.waitForPresentKHR) {
    RvkSwapchain*    mutableSwapchain = (RvkSwapchain*)swap;
    const TimeSteady startTime        = time_steady_clock();

    const TimeDuration timeout = time_second / 30;
    VkResult           result  = swap->dev->api.waitForPresentKHR(
        swap->dev->vkDev, swap->vkSwap, swap->curPresentId - numBehind, timeout);

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
          log_param("id", fmt_int(swap->curPresentId)));
      break;
    default:
      rvk_check(string_lit("vkWaitForPresentKHR"), result);
    }
  }
}
