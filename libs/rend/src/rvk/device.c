#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "log_logger.h"

#include "debug_internal.h"
#include "desc_internal.h"
#include "device_internal.h"
#include "lib_internal.h"
#include "mem_internal.h"
#include "pcache_internal.h"
#include "repository_internal.h"
#include "sampler_internal.h"
#include "transfer_internal.h"

static const String g_requiredExts[] = {
    string_static("VK_KHR_swapchain"),
    string_static("VK_KHR_16bit_storage"),
};
static const String g_optionalExts[] = {
    /**
     * 'VK_KHR_maintenance4' allows relaxed shader interface rules.
     * For devices that do not support this we are technically violating the spec, however in
     * practice all tested drivers handle this as expected.
     */
    string_static("VK_KHR_maintenance4"),
};

typedef struct {
  VkExtensionProperties* values;
  u32                    count;
} RendVkExts;

/**
 * Query a list of all supported device extensions.
 * NOTE: Free the list with 'rvk_device_exts_free()'
 */
static RendVkExts rvk_device_exts_query(RvkLib* lib, VkPhysicalDevice vkPhysDev) {
  u32 count;
  rvk_call_checked(lib, enumerateDeviceExtensionProperties, vkPhysDev, null, &count, null);
  VkExtensionProperties* props = alloc_array_t(g_allocHeap, VkExtensionProperties, count);
  rvk_call_checked(lib, enumerateDeviceExtensionProperties, vkPhysDev, null, &count, props);
  return (RendVkExts){.values = props, .count = count};
}

static void rvk_vk_exts_free(RendVkExts exts) {
  alloc_free_array_t(g_allocHeap, exts.values, exts.count);
}

/**
 * Check if the given extension is contained in the list of available device extensions.
 */
static bool rvk_device_has_ext(RendVkExts availableExts, String ext) {
  heap_array_for_t(availableExts, VkExtensionProperties, props) {
    if (string_eq(ext, string_from_null_term(props->extensionName))) {
      return true;
    }
  }
  return false;
}

static i32 rvk_device_type_score_value(const VkPhysicalDeviceType vkDevType) {
  switch (vkDevType) {
  case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
    return 4;
  case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
    return 3;
  case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
    return 2;
  case VK_PHYSICAL_DEVICE_TYPE_CPU:
    return 1;
  default:
    return 0;
  }
}

static u32 rvk_device_pick_graphics_queue(RvkLib* lib, VkPhysicalDevice vkPhysDev) {
  VkQueueFamilyProperties families[32] = {0};
  u32                     familyCount  = array_elems(families);
  lib->api.getPhysicalDeviceQueueFamilyProperties(vkPhysDev, &familyCount, families);

  for (u32 i = 0; i != familyCount; ++i) {
    if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      return i;
    }
  }
  diag_crash_msg("No graphics queue found");
}

static u32 rvk_device_pick_transfer_queue(RvkLib* lib, VkPhysicalDevice vkPhysDev) {
  VkQueueFamilyProperties families[32] = {0};
  u32                     familyCount  = array_elems(families);
  lib->api.getPhysicalDeviceQueueFamilyProperties(vkPhysDev, &familyCount, families);

  for (u32 i = 0; i != familyCount; ++i) {
    /**
     * Graphics queues also support transfer operations, so we try to find a queue that exclusively
     * does transferring otherwise fall back to using the graphics queue for transfer.
     */
    const bool hasTransfer = (families[i].queueFlags & VK_QUEUE_TRANSFER_BIT) != 0;
    const bool hasGraphics = (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
    const bool hasCompute  = (families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
    if (hasTransfer && !hasGraphics && !hasCompute) {
      return i;
    }
  }
  return sentinel_u32;
}

static VkPhysicalDevice rvk_device_pick_physical_device(RvkLib* lib) {
  VkPhysicalDevice vkPhysDevs[32];
  u32              vkPhysDevsCount = array_elems(vkPhysDevs);
  rvk_call_checked(lib, enumeratePhysicalDevices, lib->vkInst, &vkPhysDevsCount, vkPhysDevs);

  VkPhysicalDevice bestVkPhysDev  = null;
  u32              bestApiVersion = 0;
  i32              bestScore      = -1;

  for (usize i = 0; i != vkPhysDevsCount; ++i) {
    const RendVkExts exts = rvk_device_exts_query(lib, vkPhysDevs[i]);

    i32 score = 0;
    array_for_t(g_requiredExts, String, reqExt) {
      if (!rvk_device_has_ext(exts, *reqExt)) {
        score = -1;
        goto detectionDone;
      }
    }

    VkPhysicalDeviceProperties properties;
    lib->api.getPhysicalDeviceProperties(vkPhysDevs[i], &properties);

    score += rvk_device_type_score_value(properties.deviceType);

  detectionDone:
    rvk_vk_exts_free(exts);

    log_d(
        "Vulkan physical device detected",
        log_param("device-name", fmt_text(string_from_null_term(properties.deviceName))),
        log_param("device-type", fmt_text(vkPhysicalDeviceTypeStr(properties.deviceType))),
        log_param("vendor", fmt_text(vkVendorIdStr(properties.vendorID))),
        log_param("score", fmt_int(score)));

    if (score > bestScore || (score == bestScore && properties.apiVersion > bestApiVersion)) {
      bestVkPhysDev  = vkPhysDevs[i];
      bestScore      = score;
      bestApiVersion = properties.apiVersion;
    }
  }
  if (!bestVkPhysDev) {
    diag_crash_msg("No compatible Vulkan device found");
  }
  return bestVkPhysDev;
}

static VkPhysicalDeviceFeatures
rvk_device_pick_features(RvkDevice* dev, const VkPhysicalDeviceFeatures2* supported) {
  VkPhysicalDeviceFeatures result = {0};
  if (supported->features.pipelineStatisticsQuery) {
    result.pipelineStatisticsQuery = true;
    dev->flags |= RvkDeviceFlags_SupportPipelineStatQuery;
  }
  if (supported->features.samplerAnisotropy) {
    result.samplerAnisotropy = true;
    dev->flags |= RvkDeviceFlags_SupportAnisotropy;
  }
  if (supported->features.fillModeNonSolid) {
    result.fillModeNonSolid = true;
    dev->flags |= RvkDeviceFlags_SupportFillNonSolid;
  }
  if (supported->features.wideLines) {
    result.wideLines = true;
    dev->flags |= RvkDeviceFlags_SupportWideLines;
  }
  if (supported->features.depthClamp) {
    result.depthClamp = true;
    dev->flags |= RvkDeviceFlags_SupportDepthClamp;
  }
  // TODO: Either support devices without the 'independentBlend' feature or disqualify devices
  // without this feature during device selection.
  result.independentBlend = true;
  return result;
}

static VkDevice rvk_device_create_internal(RvkLib* lib, RvkDevice* dev) {
  const char* extsToEnable[64];
  u32         extsToEnableCount = 0;

  // Setup queues.
  const f32               queuePriorities[] = {1.0f, 0.5f};
  VkDeviceQueueCreateInfo queueCreateInfos[2];
  u32                     queueCreateInfoCount = 0;
  queueCreateInfos[queueCreateInfoCount++]     = (VkDeviceQueueCreateInfo){
          .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
          .queueFamilyIndex = dev->graphicsQueueIndex,
          .queueCount       = 1,
          .pQueuePriorities = &queuePriorities[0],
  };
  if (!sentinel_check(dev->transferQueueIndex)) {
    queueCreateInfos[queueCreateInfoCount++] = (VkDeviceQueueCreateInfo){
        .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = dev->transferQueueIndex,
        .queueCount       = 1,
        .pQueuePriorities = &queuePriorities[1],
    };
  }

  // Add required extensions.
  array_for_t(g_requiredExts, String, reqExt) {
    extsToEnable[extsToEnableCount++] = reqExt->ptr; // TODO: Hacky as it assumes null-term.
  }

  // Add optional extensions.
  const RendVkExts supportedExts = rvk_device_exts_query(lib, dev->vkPhysDev);
  array_for_t(g_optionalExts, String, optExt) {
    if (rvk_device_has_ext(supportedExts, *optExt)) {
      extsToEnable[extsToEnableCount++] = optExt->ptr; // TODO: Hacky as it assumes null-term.
    }
  }
  rvk_vk_exts_free(supportedExts);

  // Add optional features.
  void*                                nextOptFeature      = null;
  VkPhysicalDevicePresentIdFeaturesKHR optFeaturePresentId = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR,
      .pNext = nextOptFeature,
  };
  nextOptFeature = &optFeaturePresentId;

  VkPhysicalDevicePresentWaitFeaturesKHR optFeaturePresentWait = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR,
      .pNext = nextOptFeature,
  };
  nextOptFeature = &optFeaturePresentWait;

  VkPhysicalDeviceFeatures2 supportedFeatures = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
      .pNext = nextOptFeature,
  };
  lib->api.getPhysicalDeviceFeatures2(dev->vkPhysDev, &supportedFeatures);

  if (optFeaturePresentId.presentId) {
    extsToEnable[extsToEnableCount++] = "VK_KHR_present_id";
    dev->flags |= RvkDeviceFlags_SupportPresentId;
  }
  if (optFeaturePresentWait.presentWait) {
    extsToEnable[extsToEnableCount++] = "VK_KHR_present_wait";
    dev->flags |= RvkDeviceFlags_SupportPresentWait;
  }

  VkPhysicalDevice16BitStorageFeatures float16IStorageFeatures = {
      .sType                    = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES,
      .pNext                    = nextOptFeature, // Enable all supported optional features.
      .storageBuffer16BitAccess = true,
      .uniformAndStorageBuffer16BitAccess = true,
  };
  const VkPhysicalDeviceFeatures2 featuresToEnable = {
      .sType    = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
      .pNext    = &float16IStorageFeatures,
      .features = rvk_device_pick_features(dev, &supportedFeatures),
  };
  const VkDeviceCreateInfo createInfo = {
      .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext                   = &featuresToEnable,
      .pQueueCreateInfos       = queueCreateInfos,
      .queueCreateInfoCount    = queueCreateInfoCount,
      .enabledExtensionCount   = extsToEnableCount,
      .ppEnabledExtensionNames = extsToEnable,
  };

  VkDevice result;
  rvk_call_checked(lib, createDevice, dev->vkPhysDev, &createInfo, &dev->vkAlloc, &result);
  return result;
}

static VkFormat rvk_device_pick_depthformat(RvkDevice* dev) {
  static const VkFormat g_supportedFormats[] = {
      VK_FORMAT_D32_SFLOAT,
      VK_FORMAT_D16_UNORM,
  };
  static const VkFormatFeatureFlags g_features = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;

  array_for_t(g_supportedFormats, VkFormat, format) {
    if (rvk_device_format_supported(dev, *format, g_features)) {
      return *format;
    }
  }
  diag_crash_msg("No suitable depth-format found");
}

RvkDevice* rvk_device_create(RvkLib* lib, const RendSettingsGlobalComp* settingsGlobal) {
  RvkDevice* dev = alloc_alloc_t(g_allocHeap, RvkDevice);

  *dev = (RvkDevice){
      .lib              = lib,
      .vkAlloc          = lib->vkAlloc,
      .queueSubmitMutex = thread_mutex_create(g_allocHeap),
  };

  dev->vkPhysDev = rvk_device_pick_physical_device(lib);

  dev->graphicsQueueIndex = rvk_device_pick_graphics_queue(lib, dev->vkPhysDev);
  dev->transferQueueIndex = rvk_device_pick_transfer_queue(lib, dev->vkPhysDev);

  lib->api.getPhysicalDeviceProperties(dev->vkPhysDev, &dev->vkProperties);
  lib->api.getPhysicalDeviceMemoryProperties(dev->vkPhysDev, &dev->vkMemProperties);

  dev->vkDev = rvk_device_create_internal(lib, dev);
  rvk_api_check(string_lit("loadDevice"), vkLoadDevice(dev->vkDev, &lib->api, &dev->api));

  dev->api.getDeviceQueue(dev->vkDev, dev->graphicsQueueIndex, 0, &dev->vkGraphicsQueue);
  if (!sentinel_check(dev->transferQueueIndex)) {
    dev->api.getDeviceQueue(dev->vkDev, dev->transferQueueIndex, 0, &dev->vkTransferQueue);
  }

  dev->depthFormat              = rvk_device_pick_depthformat(dev);
  dev->preferredSwapchainFormat = VK_FORMAT_B8G8R8A8_SRGB;

  if (lib->flags & RvkLibFlags_Debug) {
    const bool          verbose    = (settingsGlobal->flags & RendGlobalFlags_Verbose) != 0;
    const RvkDebugFlags debugFlags = verbose ? RvkDebugFlags_Verbose : 0;
    dev->debug                     = rvk_debug_create(lib, dev, debugFlags);
    if (dev->vkTransferQueue) {
      rvk_debug_name_queue(dev->debug, dev->vkGraphicsQueue, "graphics");
      rvk_debug_name_queue(dev->debug, dev->vkTransferQueue, "transfer");
    } else {
      rvk_debug_name_queue(dev->debug, dev->vkGraphicsQueue, "graphics_and_transfer");
    }
  }

  dev->vkPipelineCache = rvk_pcache_load(dev);
  dev->memPool         = rvk_mem_pool_create(dev, dev->vkMemProperties, dev->vkProperties.limits);
  dev->descPool        = rvk_desc_pool_create(dev);
  dev->samplerPool     = rvk_sampler_pool_create(dev);
  dev->transferer      = rvk_transferer_create(dev);
  dev->repository      = rvk_repository_create();

  log_i(
      "Vulkan device created",
      log_param("device-name", fmt_text(string_from_null_term(dev->vkProperties.deviceName))),
      log_param("graphics-queue-idx", fmt_int(dev->graphicsQueueIndex)),
      log_param("transfer-queue-idx", fmt_int(dev->transferQueueIndex)),
      log_param("depth-format", fmt_text(vkFormatStr(dev->depthFormat))),
      log_param("present-id-enabled", fmt_bool(dev->flags & RvkDeviceFlags_SupportPresentId)),
      log_param("present-wait-enabled", fmt_bool(dev->flags & RvkDeviceFlags_SupportPresentWait)));

  return dev;
}

void rvk_device_destroy(RvkDevice* dev) {

  rvk_device_wait_idle(dev);

  rvk_pcache_save(dev, dev->vkPipelineCache);
  dev->api.destroyPipelineCache(dev->vkDev, dev->vkPipelineCache, &dev->vkAlloc);

  rvk_repository_destroy(dev->repository);
  rvk_transferer_destroy(dev->transferer);
  rvk_sampler_pool_destroy(dev->samplerPool);
  rvk_desc_pool_destroy(dev->descPool);
  rvk_mem_pool_destroy(dev->memPool);
  dev->api.destroyDevice(dev->vkDev, &dev->vkAlloc);

  if (dev->debug) {
    rvk_debug_destroy(dev->debug);
  }

  thread_mutex_destroy(dev->queueSubmitMutex);
  alloc_free_t(g_allocHeap, dev);

  log_d("Vulkan device destroyed");
}

bool rvk_device_format_supported(
    const RvkDevice* dev, const VkFormat format, const VkFormatFeatureFlags requiredFeatures) {
  VkFormatProperties properties;
  dev->lib->api.getPhysicalDeviceFormatProperties(dev->vkPhysDev, format, &properties);
  return (properties.optimalTilingFeatures & requiredFeatures) == requiredFeatures;
}

String rvk_device_name(const RvkDevice* dev) {
  return string_from_null_term(dev->vkProperties.deviceName);
}

void rvk_device_update(RvkDevice* dev) { rvk_transfer_flush(dev->transferer); }

void rvk_device_wait_idle(const RvkDevice* dev) {
  rvk_call_checked(dev, deviceWaitIdle, dev->vkDev);
}
