#include "core/alloc.h"
#include "core/array.h"
#include "core/diag.h"
#include "core/time.h"
#include "gap/native.h"
#include "log/logger.h"

#include "device.h"
#include "image.h"
#include "lib.h"
#include "swapchain.h"

#define swapchain_images_max 5
#define swapchain_presentmode_desired_max 8

/**
 * What present stage to measure when using present timings.
 *
 * Ideally we would measure 'VK_PRESENT_STAGE_IMAGE_FIRST_PIXEL_OUT_BIT_EXT' but XWayland does not
 * support this and with native X11 compositors becoming rare that is likely what we will run on
 * linux. Its slowly time for us to implement Wayland support.
 * TODO: Test what the situation is like on windows.
 */
#define swapchain_measured_present_stage VK_PRESENT_STAGE_REQUEST_DEQUEUED_BIT_EXT

typedef enum {
  RvkSwapchainFlags_PresentTimingEnabled = 1 << 0,
  RvkSwapchainFlags_OutOfDate            = 1 << 1,
} RvkSwapchainFlags;

typedef struct {
  VkSurfaceCapabilitiesKHR capabilities;
  bool                     presentTiming;
} RvkSurfaceCaps;

struct sRvkSwapchain {
  RvkLib*            lib;
  RvkDevice*         dev;
  VkSurfaceKHR       vkSurf;
  VkSurfaceFormatKHR vkSurfFormat;
  VkSwapchainKHR     vkSwap;
  RendSyncMode       syncMode;
  RvkSwapchainFlags  flags;
  RvkSize            size;
  u32                imgCount;
  RvkImage           imgs[swapchain_images_max];
  VkSemaphore        semaphores[swapchain_images_max]; // Semaphore to signal presentation.

  TimeDuration lastAcquireDur, lastPresentEnqueueDur, lastPresentWaitDur;
  u64          originPresentId; // Identifier of the last present before recreating the swapchain.
  u64          curPresentId;
};

static VkSemaphore rvk_semaphore_create(RvkDevice* dev) {
  VkSemaphoreCreateInfo semaphoreInfo = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  VkSemaphore           result;
  rvk_call_checked(dev, createSemaphore, dev->vkDev, &semaphoreInfo, &dev->vkAlloc, &result);
  return result;
}

static void rvk_semaphore_destroy(RvkDevice* dev, const VkSemaphore sema) {
  rvk_call(dev, destroySemaphore, dev->vkDev, sema, &dev->vkAlloc);
}

static RvkSize rvk_surface_clamp_size(RvkSize size, const RvkSurfaceCaps* caps) {
  if (size.width < caps->capabilities.minImageExtent.width) {
    size.width = (u16)caps->capabilities.minImageExtent.width;
  }
  if (size.height < caps->capabilities.minImageExtent.height) {
    size.height = (u16)caps->capabilities.minImageExtent.height;
  }
  if (size.width > caps->capabilities.maxImageExtent.width) {
    size.width = (u16)caps->capabilities.maxImageExtent.width;
  }
  if (size.height > caps->capabilities.maxImageExtent.height) {
    size.height = (u16)caps->capabilities.maxImageExtent.height;
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
    rvk_call_checked(lib, createXcbSurfaceKHR, lib->vkInst, &createInfo, &lib->vkAlloc, &result);
  } break;
  case GapNativeWm_Win32: {
    const VkWin32SurfaceCreateInfoKHR createInfo = {
        .sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
        .hinstance = gap_native_app_handle(window),
        .hwnd      = gap_native_window_handle(window),
    };
    rvk_call_checked(lib, createWin32SurfaceKHR, lib->vkInst, &createInfo, &lib->vkAlloc, &result);
  } break;
  }
  return result;
}

static VkSurfaceFormatKHR rvk_pick_surface_format(RvkLib* lib, RvkDevice* dev, VkSurfaceKHR surf) {
  u32 count;
  rvk_call_checked(lib, getPhysicalDeviceSurfaceFormatsKHR, dev->vkPhysDev, surf, &count, null);
  if (!count) {
    diag_crash_msg("No Vulkan surface formats available");
  }
  VkSurfaceFormatKHR* formats = mem_stack(sizeof(VkSurfaceFormatKHR) * count).ptr;
  rvk_call_checked(lib, getPhysicalDeviceSurfaceFormatsKHR, dev->vkPhysDev, surf, &count, formats);

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

static u32 rvk_pick_imagecount(const RvkSurfaceCaps* caps, const VkPresentModeKHR presentMode) {
  u32 imgCount;
  switch (presentMode) {
  case VK_PRESENT_MODE_IMMEDIATE_KHR:
    imgCount = 2; // one on-screen, and one being rendered to.
    break;
  case VK_PRESENT_MODE_FIFO_KHR:
  case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
  case VK_PRESENT_MODE_MAILBOX_KHR:
  default:
    imgCount = 3; // one on-screen, one ready, and one being rendered to.
    break;
  }
  if (imgCount < caps->capabilities.minImageCount) {
    imgCount = caps->capabilities.minImageCount;
  }
  if (caps->capabilities.maxImageCount && imgCount > caps->capabilities.maxImageCount) {
    imgCount = caps->capabilities.maxImageCount;
  }
  return imgCount;
}

static u32 rvk_pick_presentmode_desired(
    const RendSyncMode mode,
    VkPresentModeKHR   out[PARAM_ARRAY_SIZE(swapchain_presentmode_desired_max)]) {
  u32 count = 0;
  switch (mode) {
  case RendSyncMode_Immediate:
    out[count++] = VK_PRESENT_MODE_MAILBOX_KHR;   // Mailbox prevents tearing without blocking.
    out[count++] = VK_PRESENT_MODE_IMMEDIATE_KHR; // Tearing mode.
    break;
  case RendSyncMode_VSync:
    out[count++] = VK_PRESENT_MODE_FIFO_RELAXED_KHR; // Vsync with tearing if too slow.
    out[count++] = VK_PRESENT_MODE_FIFO_KHR;         // Vsync mode.
    break;
  }
  return count;
}

static VkPresentModeKHR rvk_pick_presentmode(
    RvkLib* lib, RvkDevice* dev, const RendSettingsComp* settings, const VkSurfaceKHR surf) {
  VkPresentModeKHR available[32];
  u32              availableCount = array_elems(available);
  rvk_call_checked(
      lib,
      getPhysicalDeviceSurfacePresentModesKHR,
      dev->vkPhysDev,
      surf,
      &availableCount,
      available);

  VkPresentModeKHR desired[swapchain_presentmode_desired_max];
  const u32        desiredCount = rvk_pick_presentmode_desired(settings->syncMode, desired);

  for (u32 desiredIdx = 0; desiredIdx != desiredCount; ++desiredIdx) {
    const VkPresentModeKHR mode = desired[desiredIdx];

    for (u32 availableIdx = 0; availableIdx != availableCount; ++availableIdx) {
      if (available[availableIdx] == mode) {
        return mode; // Mode supported.
      }
    }
  }

  log_w("All desired present modes unavailable");
  return VK_PRESENT_MODE_FIFO_KHR; // FIFO is required by the spec to be always available.
}

static RvkSurfaceCaps rvk_surface_caps(RvkLib* lib, RvkDevice* dev, VkSurfaceKHR surf) {
  const VkPhysicalDeviceSurfaceInfo2KHR info = {
      .sType   = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR,
      .surface = surf,
  };

  void* nextCapabilities = null;

  VkPresentTimingSurfaceCapabilitiesEXT timingCapabilities = {
      .sType = VK_STRUCTURE_TYPE_PRESENT_TIMING_SURFACE_CAPABILITIES_EXT,
      .pNext = nextCapabilities,
  };
  if (dev->flags & RvkDeviceFlags_SupportPresentTiming) {
    nextCapabilities = &timingCapabilities;
  }

  VkSurfaceCapabilities2KHR result = {
      .sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR,
      .pNext = nextCapabilities,
  };
  rvk_call_checked(lib, getPhysicalDeviceSurfaceCapabilities2KHR, dev->vkPhysDev, &info, &result);

  return (RvkSurfaceCaps){
      .capabilities  = result.surfaceCapabilities,
      .presentTiming = timingCapabilities.presentTimingSupported &&
                       ((timingCapabilities.presentStageQueries &
                         swapchain_measured_present_stage) == swapchain_measured_present_stage),
  };
}

static bool rvk_swapchain_init(RvkSwapchain* swap, const RendSettingsComp* settings, RvkSize size) {
  if (!size.width || !size.height) {
    swap->size = size;
    return false;
  }

  for (u32 i = 0; i != swap->imgCount; ++i) {
    rvk_image_destroy(&swap->imgs[i], swap->dev);
  }

  const VkDevice         vkDev    = swap->dev->vkDev;
  VkAllocationCallbacks* vkAlloc  = &swap->dev->vkAlloc;
  const RvkSurfaceCaps   surfCaps = rvk_surface_caps(swap->lib, swap->dev, swap->vkSurf);
  size                            = rvk_surface_clamp_size(size, &surfCaps);

  const VkPresentModeKHR presentMode =
      rvk_pick_presentmode(swap->lib, swap->dev, settings, swap->vkSurf);

  const VkSwapchainKHR oldSwapchain = swap->vkSwap;

  VkSwapchainCreateFlagBitsKHR swapchainFlags = 0;
  if (surfCaps.presentTiming) {
    swapchainFlags |= VK_SWAPCHAIN_CREATE_PRESENT_TIMING_BIT_EXT;
  }

  const VkSwapchainCreateInfoKHR createInfo = {
      .sType              = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
      .surface            = swap->vkSurf,
      .flags              = swapchainFlags,
      .minImageCount      = rvk_pick_imagecount(&surfCaps, presentMode),
      .imageFormat        = swap->vkSurfFormat.format,
      .imageColorSpace    = swap->vkSurfFormat.colorSpace,
      .imageExtent.width  = size.width,
      .imageExtent.height = size.height,
      .imageArrayLayers   = 1,
      .imageUsage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      .imageSharingMode   = VK_SHARING_MODE_EXCLUSIVE,
      .preTransform       = surfCaps.capabilities.currentTransform,
      .compositeAlpha     = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
      .presentMode        = presentMode,
      .clipped            = true,
      .oldSwapchain       = oldSwapchain,
  };

  rvk_call_checked(swap->dev, createSwapchainKHR, vkDev, &createInfo, vkAlloc, &swap->vkSwap);
  if (oldSwapchain) {
    rvk_call(swap->dev, destroySwapchainKHR, vkDev, oldSwapchain, &swap->dev->vkAlloc);
  }

  rvk_call_checked(swap->dev, getSwapchainImagesKHR, vkDev, swap->vkSwap, &swap->imgCount, null);
  if (UNLIKELY(swap->imgCount > swapchain_images_max)) {
    diag_crash_msg("Vulkan surface uses more swapchain images then are supported");
  }

  VkImage vkImgs[swapchain_images_max];
  rvk_call_checked(swap->dev, getSwapchainImagesKHR, vkDev, swap->vkSwap, &swap->imgCount, vkImgs);

  const VkFormat format = swap->vkSurfFormat.format;
  for (u32 i = 0; i != swap->imgCount; ++i) {
    swap->imgs[i] = rvk_image_create_swapchain(swap->dev, vkImgs[i], format, size);
    rvk_debug_name_img(swap->dev, vkImgs[i], "swapchain_{}", fmt_int(i));

    if (!swap->semaphores[i]) {
      swap->semaphores[i] = rvk_semaphore_create(swap->dev);
      rvk_debug_name_semaphore(swap->dev, swap->semaphores[i], "swapchain_{}", fmt_int(i));
    }
  }

  swap->flags &= ~RvkSwapchainFlags_OutOfDate;
  swap->syncMode        = settings->syncMode;
  swap->size            = size;
  swap->originPresentId = swap->curPresentId;

  if (surfCaps.presentTiming) {
    swap->flags |= RvkSwapchainFlags_PresentTimingEnabled;
  } else {
    swap->flags &= ~RvkSwapchainFlags_PresentTimingEnabled;
  }

  log_i(
      "Vulkan swapchain created",
      log_param("size", rvk_size_fmt(size)),
      log_param("format", fmt_text(vkFormatStr(format))),
      log_param("color", fmt_text(vkColorSpaceKHRStr(swap->vkSurfFormat.colorSpace))),
      log_param("present-mode", fmt_text(vkPresentModeKHRStr(presentMode))),
      log_param("present-timing", fmt_bool(surfCaps.presentTiming)),
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
  rvk_call_checked(
      lib,
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
  for (u32 i = 0; i != swapchain_images_max; ++i) {
    if (swap->imgs[i].vkImageView) {
      rvk_image_destroy(&swap->imgs[i], swap->dev);
    }
    if (swap->semaphores[i]) {
      rvk_semaphore_destroy(swap->dev, swap->semaphores[i]);
    }
  }
  if (swap->vkSwap) {
    rvk_call(swap->dev, destroySwapchainKHR, swap->dev->vkDev, swap->vkSwap, &swap->dev->vkAlloc);
  }

  rvk_call(swap->lib, destroySurfaceKHR, swap->lib->vkInst, swap->vkSurf, &swap->dev->vkAlloc);
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

VkSemaphore rvk_swapchain_semaphore(RvkSwapchain* swap, const RvkSwapchainIdx idx) {
  diag_assert_msg(idx < swap->imgCount, "Swapchain index {} is out of bounds", fmt_int(idx));
  return swap->semaphores[idx];
}

bool rvk_swapchain_prepare(
    RvkSwapchain* swap, const RendSettingsComp* settings, const RvkSize size) {

  const bool outOfDate      = (swap->flags & RvkSwapchainFlags_OutOfDate) != 0;
  const bool changedSize    = !rvk_size_equal(size, swap->size);
  const bool changedPresent = swap->syncMode != settings->syncMode;

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
  const VkResult result = rvk_call(
      swap->dev,
      acquireNextImageKHR,
      swap->dev->vkDev,
      swap->vkSwap,
      u64_max,
      available,
      null,
      &index);

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
    rvk_api_check(string_lit("acquireNextImageKHR"), result);
    return index;
  }
}

bool rvk_swapchain_enqueue_present(RvkSwapchain* swap, const RvkSwapchainIdx idx) {
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
      .pWaitSemaphores    = &swap->semaphores[idx],
      .swapchainCount     = 1,
      .pSwapchains        = &swap->vkSwap,
      .pImageIndices      = &idx,
  };

  const TimeSteady startTime = time_steady_clock();
  VkResult result = rvk_call(swap->dev, queuePresentKHR, swap->dev->vkGraphicsQueue, &presentInfo);
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
    rvk_api_check(string_lit("queuePresentKHR"), result);
    return true;
  }
}

void rvk_swapchain_wait_for_present(const RvkSwapchain* swap, const u32 numBehind) {
  if (numBehind >= (swap->curPresentId - swap->originPresentId)) {
    /**
     * Out of bound presentation-ids are considered to be already presented.
     * This is convenient for the calling code as it doesn't need the special case the first frame.
     */
    return;
  }
  if ((swap->dev->flags & RvkDeviceFlags_SupportPresentWait) && swap->dev->api.waitForPresentKHR) {
    RvkSwapchain*    mutableSwapchain = (RvkSwapchain*)swap;
    const TimeSteady startTime        = time_steady_clock();

    const TimeDuration timeout = time_second / 30;
    VkResult           result  = rvk_call(
        swap->dev,
        waitForPresentKHR,
        swap->dev->vkDev,
        swap->vkSwap,
        swap->curPresentId - numBehind,
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
          log_param("id", fmt_int(swap->curPresentId)));
      break;
    case VK_ERROR_DEVICE_LOST:
      log_w("Device lost during swapchain wait", log_param("id", fmt_int(swap->curPresentId)));
      break;
    default:
      rvk_api_check(string_lit("waitForPresentKHR"), result);
    }
  }
}
