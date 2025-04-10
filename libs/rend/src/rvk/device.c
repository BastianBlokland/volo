#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_thread.h"
#include "geo_color.h"
#include "log_logger.h"

#include "desc_internal.h"
#include "device_internal.h"
#include "lib_internal.h"
#include "mem_internal.h"
#include "pcache_internal.h"
#include "repository_internal.h"
#include "sampler_internal.h"
#include "transfer_internal.h"

static const String g_requiredExts[] = {
    string_static(VK_KHR_swapchain),
};

static const char* rvk_to_null_term_scratch(const String str) {
  const Mem scratchMem = alloc_alloc(g_allocScratch, str.size + 1, 1);
  mem_cpy(scratchMem, str);
  *mem_at_u8(scratchMem, str.size) = '\0';
  return scratchMem.ptr;
}

MAYBE_UNUSED static u32 rvk_version_major(const u32 version) { return (version >> 22) & 0x7FU; }
MAYBE_UNUSED static u32 rvk_version_minor(const u32 version) { return (version >> 12) & 0x3FFU; }

typedef struct {
  VkExtensionProperties* values;
  u32                    count;
} RendVkExts;

/**
 * Query a list of all supported device extensions.
 * NOTE: Free the list with 'rvk_device_exts_free()'
 */
static RendVkExts rvk_exts_query(RvkLib* lib, VkPhysicalDevice vkPhysDev) {
  u32 count;
  rvk_call_checked(lib, enumerateDeviceExtensionProperties, vkPhysDev, null, &count, null);
  VkExtensionProperties* props = alloc_array_t(g_allocHeap, VkExtensionProperties, count);
  rvk_call_checked(lib, enumerateDeviceExtensionProperties, vkPhysDev, null, &count, props);
  return (RendVkExts){.values = props, .count = count};
}

static void rvk_exts_free(RendVkExts exts) {
  alloc_free_array_t(g_allocHeap, exts.values, exts.count);
}

/**
 * Check if the given extension is contained in the list of available device extensions.
 */
static bool rvk_has_ext(RendVkExts availableExts, const String ext) {
  heap_array_for_t(availableExts, VkExtensionProperties, props) {
    if (string_eq(ext, string_from_null_term(props->extensionName))) {
      return true;
    }
  }
  return false;
}

static u64 rvk_device_local_memory(const VkPhysicalDeviceMemoryProperties* memProps) {
  u64 result = 0;
  for (u32 i = 0; i != memProps->memoryHeapCount; ++i) {
    if (memProps->memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
      result += memProps->memoryHeaps[i].size;
    }
  }
  return result;
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

static bool rvk_validate_16bit_storage(const VkPhysicalDevice16BitStorageFeatures* f) {
  if (!f->storageBuffer16BitAccess) {
    return false;
  }
  if (!f->uniformAndStorageBuffer16BitAccess) {
    return false;
  }
  return true;
}

static bool rvk_validate_features(const VkPhysicalDeviceFeatures* f) {
  if (!f->independentBlend) {
    return false;
  }
  return true;
}

static void rvk_config_robustness2(RvkDevice* d, VkPhysicalDeviceRobustness2FeaturesEXT* f) {
  f->robustImageAccess2  = false; // Unused.
  f->robustBufferAccess2 = false; // Unused.
  if (f->nullDescriptor) {
    d->flags |= RvkDeviceFlags_SupportNullDescriptor;
  }
}

static void rvk_config_present_id(RvkDevice* d, VkPhysicalDevicePresentIdFeaturesKHR* f) {
  if (f->presentId) {
    d->flags |= RvkDeviceFlags_SupportPresentId;
  }
}

static void rvk_config_present_wait(RvkDevice* d, VkPhysicalDevicePresentWaitFeaturesKHR* f) {
  if (f->presentWait) {
    d->flags |= RvkDeviceFlags_SupportPresentWait;
  }
}

static void rvk_config_executable_properties(
    RvkDevice* d, VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR* f) {
  if (f->pipelineExecutableInfo) {
    d->flags |= RvkDeviceFlags_SupportExecutableInfo;
  }
}

static void rvk_config_16bit_storage(RvkDevice* d, VkPhysicalDevice16BitStorageFeatures* f) {
  (void)d;
  f->storageBuffer16BitAccess           = true; // Required.
  f->uniformAndStorageBuffer16BitAccess = true; // Required.
}

static void rvk_config_features(RvkDevice* d, VkPhysicalDeviceFeatures* f) {
  VkPhysicalDeviceFeatures result = {
      .independentBlend = true, // Required.
  };
  if (f->pipelineStatisticsQuery) {
    result.pipelineStatisticsQuery = true;
    d->flags |= RvkDeviceFlags_SupportPipelineStatQuery;
  }
  if (f->samplerAnisotropy) {
    result.samplerAnisotropy = true;
    d->flags |= RvkDeviceFlags_SupportAnisotropy;
  }
  if (f->fillModeNonSolid) {
    result.fillModeNonSolid = true;
    d->flags |= RvkDeviceFlags_SupportFillNonSolid;
  }
  if (f->wideLines) {
    result.wideLines = true;
    d->flags |= RvkDeviceFlags_SupportWideLines;
  }
  if (f->depthClamp) {
    result.depthClamp = true;
    d->flags |= RvkDeviceFlags_SupportDepthClamp;
  }
  *f = result;
}

static u32 rvk_pick_graphics_queue(RvkLib* lib, VkPhysicalDevice vkPhysDev) {
  u32 count;
  rvk_call(lib, getPhysicalDeviceQueueFamilyProperties, vkPhysDev, &count, null);
  if (!count) {
    goto NoGraphicsQueue;
  }
  VkQueueFamilyProperties* arr = alloc_array_t(g_allocScratch, VkQueueFamilyProperties, count);
  rvk_call(lib, getPhysicalDeviceQueueFamilyProperties, vkPhysDev, &count, arr);

  for (u32 i = 0; i != count; ++i) {
    if (arr[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      return i;
    }
  }
NoGraphicsQueue:
  diag_crash_msg("No Vulkan graphics queue found");
}

static u32 rvk_pick_transfer_queue(RvkLib* lib, VkPhysicalDevice vkPhysDev) {
  u32 count;
  rvk_call(lib, getPhysicalDeviceQueueFamilyProperties, vkPhysDev, &count, null);
  if (!count) {
    goto NoTransferQueue;
  }
  VkQueueFamilyProperties* arr = alloc_array_t(g_allocScratch, VkQueueFamilyProperties, count);
  rvk_call(lib, getPhysicalDeviceQueueFamilyProperties, vkPhysDev, &count, arr);

  for (u32 i = 0; i != count; ++i) {
    /**
     * Graphics queues also support transfer operations, so we try to find a queue that exclusively
     * does transferring otherwise fall back to using the graphics queue for transfer.
     */
    const bool hasTransfer = (arr[i].queueFlags & VK_QUEUE_TRANSFER_BIT) != 0;
    const bool hasGraphics = (arr[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
    const bool hasCompute  = (arr[i].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
    if (hasTransfer && !hasGraphics && !hasCompute) {
      return i;
    }
  }

NoTransferQueue:
  return sentinel_u32;
}

static VkPhysicalDevice rvk_pick_physical_device(RvkLib* lib) {
  VkPhysicalDevice vkPhysDevs[32];
  u32              vkPhysDevsCount = array_elems(vkPhysDevs);
  rvk_call_checked(lib, enumeratePhysicalDevices, lib->vkInst, &vkPhysDevsCount, vkPhysDevs);

  VkPhysicalDevice bestVkPhysDev = null;
  i32              bestScore     = -1;

  for (usize i = 0; i != vkPhysDevsCount; ++i) {
    const RendVkExts exts = rvk_exts_query(lib, vkPhysDevs[i]);

    i32 score = 0;
    array_for_t(g_requiredExts, String, reqExt) {
      if (!rvk_has_ext(exts, *reqExt)) {
        score = -1;
        goto detectionDone;
      }
    }

    void*                                nextFeature         = null;
    VkPhysicalDevice16BitStorageFeatures feature16BitStorage = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES,
        .pNext = nextFeature,
    };
    nextFeature = &feature16BitStorage;

    VkPhysicalDeviceFeatures2 featureBase = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = nextFeature,
    };
    rvk_call(lib, getPhysicalDeviceFeatures2, vkPhysDevs[i], &featureBase);

    if (!rvk_validate_16bit_storage(&feature16BitStorage)) {
      score = -1;
      goto detectionDone;
    }
    if (!rvk_validate_features(&featureBase.features)) {
      score = -1;
      goto detectionDone;
    }

    void*                            nextProps   = null;
    VkPhysicalDeviceDriverProperties driverProps = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES,
        .pNext = nextProps,
    };
    if (rvk_has_ext(exts, string_lit(VK_KHR_driver_properties))) {
      nextProps = &driverProps;
    }
    VkPhysicalDeviceProperties2 props = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = nextProps,
    };
    rvk_call(lib, getPhysicalDeviceProperties2, vkPhysDevs[i], &props);

    if (!rvk_lib_api_version_supported(props.properties.apiVersion)) {
      score = -1;
      goto detectionDone;
    }

    VkPhysicalDeviceMemoryProperties vkMemProperties;
    rvk_call(lib, getPhysicalDeviceMemoryProperties, vkPhysDevs[i], &vkMemProperties);

    const u64 deviceMemory = rvk_device_local_memory(&vkMemProperties);

    score += rvk_device_type_score_value(props.properties.deviceType);

  detectionDone:
    rvk_exts_free(exts);

    log_i(
        "Vulkan physical device detected",
        log_param("device-name", fmt_text(string_from_null_term(props.properties.deviceName))),
        log_param("device-type", fmt_text(vkPhysicalDeviceTypeStr(props.properties.deviceType))),
        log_param("vendor", fmt_text(vkVendorIdStr(props.properties.vendorID))),
        log_param("driver-name", fmt_text(string_from_null_term(driverProps.driverName))),
        log_param("driver-info", fmt_text(string_from_null_term(driverProps.driverInfo))),
        log_param("memory", fmt_size(deviceMemory)),
        log_param("version-major", fmt_int(rvk_version_major(props.properties.apiVersion))),
        log_param("version-minor", fmt_int(rvk_version_minor(props.properties.apiVersion))),
        log_param("score", fmt_int(score)));

    if (score > bestScore) {
      bestVkPhysDev = vkPhysDevs[i];
      bestScore     = score;
    }
  }
  if (!bestVkPhysDev) {
    diag_crash_msg("No compatible Vulkan device found");
  }
  return bestVkPhysDev;
}

static VkFormat rvk_pick_depthformat(RvkDevice* dev) {
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

  const RendVkExts supportedExts = rvk_exts_query(lib, dev->vkPhysDev);

  // Add required extensions.
  array_for_t(g_requiredExts, String, reqExt) {
    extsToEnable[extsToEnableCount++] = rvk_to_null_term_scratch(*reqExt);
  }

  // Add optional extensions and features.
  void* nextFeature = null;

  if (rvk_has_ext(supportedExts, string_from_null_term(VK_KHR_maintenance4))) {
    extsToEnable[extsToEnableCount++] = VK_KHR_maintenance4; // For relaxed shader interface rules.
  }

  VkPhysicalDeviceRobustness2FeaturesEXT featureRobustness = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT,
      .pNext = nextFeature,
  };
  if (rvk_has_ext(supportedExts, string_from_null_term(VK_EXT_robustness2))) {
    nextFeature                       = &featureRobustness;
    extsToEnable[extsToEnableCount++] = VK_EXT_robustness2;
  }

  VkPhysicalDevicePresentIdFeaturesKHR featurePresentId = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR,
      .pNext = nextFeature,
  };
  if (rvk_has_ext(supportedExts, string_from_null_term(VK_KHR_present_id))) {
    nextFeature                       = &featurePresentId;
    extsToEnable[extsToEnableCount++] = VK_KHR_present_id;
  }

  VkPhysicalDevicePresentWaitFeaturesKHR featurePresentWait = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR,
      .pNext = nextFeature,
  };
  if (rvk_has_ext(supportedExts, string_from_null_term(VK_KHR_present_wait))) {
    nextFeature                       = &featurePresentWait;
    extsToEnable[extsToEnableCount++] = VK_KHR_present_wait;
  }

  VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR featureExecutableProperties = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR,
      .pNext = nextFeature,
  };
  if (rvk_has_ext(supportedExts, string_from_null_term(VK_KHR_pipeline_executable_properties))) {
    nextFeature                       = &featureExecutableProperties;
    extsToEnable[extsToEnableCount++] = VK_KHR_pipeline_executable_properties;
  }

  VkPhysicalDevice16BitStorageFeatures feature16BitStorage = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES,
      .pNext = nextFeature,
  };
  nextFeature = &feature16BitStorage;

  VkPhysicalDeviceFeatures2 featureBase = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
      .pNext = nextFeature,
  };
  rvk_call(lib, getPhysicalDeviceFeatures2, dev->vkPhysDev, &featureBase);

  rvk_config_robustness2(dev, &featureRobustness);
  rvk_config_present_id(dev, &featurePresentId);
  rvk_config_present_wait(dev, &featurePresentWait);
  rvk_config_executable_properties(dev, &featureExecutableProperties);
  rvk_config_16bit_storage(dev, &feature16BitStorage);
  rvk_config_features(dev, &featureBase.features);

  if (rvk_has_ext(supportedExts, string_from_null_term(VK_EXT_memory_budget))) {
    dev->flags |= RvkDeviceFlags_SupportMemoryBudget;
    extsToEnable[extsToEnableCount++] = VK_EXT_memory_budget;
  }

  const VkDeviceCreateInfo createInfo = {
      .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext                   = &featureBase,
      .pQueueCreateInfos       = queueCreateInfos,
      .queueCreateInfoCount    = queueCreateInfoCount,
      .enabledExtensionCount   = extsToEnableCount,
      .ppEnabledExtensionNames = extsToEnable,
  };

  rvk_exts_free(supportedExts);

  VkDevice result;
  rvk_call_checked(lib, createDevice, dev->vkPhysDev, &createInfo, &dev->vkAlloc, &result);
  return result;
}

RvkDevice* rvk_device_create(RvkLib* lib) {
  RvkDevice* dev = alloc_alloc_t(g_allocHeap, RvkDevice);

  *dev = (RvkDevice){
      .lib              = lib,
      .vkAlloc          = lib->vkAlloc,
      .queueSubmitMutex = thread_mutex_create(g_allocHeap),
  };

  dev->vkPhysDev = rvk_pick_physical_device(lib);

  dev->graphicsQueueIndex = rvk_pick_graphics_queue(lib, dev->vkPhysDev);
  dev->transferQueueIndex = rvk_pick_transfer_queue(lib, dev->vkPhysDev);

  VkPhysicalDeviceProperties2 prop = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
  };
  rvk_call(lib, getPhysicalDeviceProperties2, dev->vkPhysDev, &prop);
  dev->vkProperties = prop.properties;

  rvk_call(lib, getPhysicalDeviceMemoryProperties, dev->vkPhysDev, &dev->vkMemProperties);

  dev->vkDev = rvk_device_create_internal(lib, dev);
  rvk_api_check(string_lit("loadDevice"), vkLoadDevice(dev->vkDev, &lib->api, &dev->api));

  rvk_call(dev, getDeviceQueue, dev->vkDev, dev->graphicsQueueIndex, 0, &dev->vkGraphicsQueue);
  if (!sentinel_check(dev->transferQueueIndex)) {
    rvk_call(dev, getDeviceQueue, dev->vkDev, dev->transferQueueIndex, 0, &dev->vkTransferQueue);
  }

  dev->depthFormat              = rvk_pick_depthformat(dev);
  dev->preferredSwapchainFormat = VK_FORMAT_B8G8R8A8_SRGB;

  if (lib->flags & RvkLibFlags_Debug) {
    if (dev->vkTransferQueue) {
      rvk_debug_name_queue(dev, dev->vkGraphicsQueue, "graphics");
      rvk_debug_name_queue(dev, dev->vkTransferQueue, "transfer");
    } else {
      rvk_debug_name_queue(dev, dev->vkGraphicsQueue, "graphics_and_transfer");
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
      log_param("null-descriptor", fmt_bool(dev->flags & RvkDeviceFlags_SupportNullDescriptor)),
      log_param("present-id", fmt_bool(dev->flags & RvkDeviceFlags_SupportPresentId)),
      log_param("present-wait", fmt_bool(dev->flags & RvkDeviceFlags_SupportPresentWait)));

  return dev;
}

void rvk_device_destroy(RvkDevice* dev) {

  rvk_device_wait_idle(dev);

  rvk_pcache_save(dev, dev->vkPipelineCache);
  rvk_call(dev, destroyPipelineCache, dev->vkDev, dev->vkPipelineCache, &dev->vkAlloc);

  rvk_repository_destroy(dev->repository);
  rvk_transferer_destroy(dev->transferer);
  rvk_sampler_pool_destroy(dev->samplerPool);
  rvk_desc_pool_destroy(dev->descPool);
  rvk_mem_pool_destroy(dev->memPool);
  rvk_call(dev, destroyDevice, dev->vkDev, &dev->vkAlloc);

  thread_mutex_destroy(dev->queueSubmitMutex);
  alloc_free_t(g_allocHeap, dev);

  log_d("Vulkan device destroyed");
}

bool rvk_device_format_supported(
    const RvkDevice* dev, const VkFormat format, const VkFormatFeatureFlags requiredFeatures) {
  VkFormatProperties properties;
  rvk_call(dev->lib, getPhysicalDeviceFormatProperties, dev->vkPhysDev, format, &properties);
  return (properties.optimalTilingFeatures & requiredFeatures) == requiredFeatures;
}

String rvk_device_name(const RvkDevice* dev) {
  return string_from_null_term(dev->vkProperties.deviceName);
}

void rvk_device_update(RvkDevice* dev) {
  // Track device memory budget.
  if (dev->flags & RvkDeviceFlags_SupportMemoryBudget) {
    VkPhysicalDeviceMemoryBudgetPropertiesEXT budgetProps = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT,
    };
    VkPhysicalDeviceMemoryProperties2 memProps = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2,
        .pNext = &budgetProps,
    };
    rvk_call(dev->lib, getPhysicalDeviceMemoryProperties2, dev->vkPhysDev, &memProps);

    dev->memBudgetTotal = dev->memBudgetUsed = 0;
    for (u32 i = 0; i != memProps.memoryProperties.memoryHeapCount; ++i) {
      if (memProps.memoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
        dev->memBudgetTotal += budgetProps.heapBudget[i];
        dev->memBudgetUsed += budgetProps.heapUsage[i];
      }
    }
  }

  // Submit any pending transfers.
  rvk_transfer_flush(dev->transferer);
}

void rvk_device_wait_idle(const RvkDevice* dev) {
  rvk_call_checked(dev, deviceWaitIdle, dev->vkDev);
}

void rvk_debug_name(
    RvkDevice* dev, const VkObjectType vkType, const u64 vkHandle, const String name) {
  if (dev->lib->flags & RvkLibFlags_Debug) {
    const VkDebugUtilsObjectNameInfoEXT nameInfo = {
        .sType        = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        .objectType   = vkType,
        .objectHandle = vkHandle,
        .pObjectName  = rvk_to_null_term_scratch(name),
    };
    rvk_call_checked(dev->lib, setDebugUtilsObjectNameEXT, dev->vkDev, &nameInfo);
  }
}

void rvk_debug_label_begin_raw(
    RvkDevice* dev, VkCommandBuffer vkCmdBuffer, const GeoColor color, const String name) {
  if (dev->lib->flags & RvkLibFlags_Debug) {
    const VkDebugUtilsLabelEXT label = {
        .sType      = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        .pLabelName = rvk_to_null_term_scratch(name),
        .color      = {color.r, color.g, color.b, color.a},
    };
    rvk_call(dev->lib, cmdBeginDebugUtilsLabelEXT, vkCmdBuffer, &label);
  }
}

void rvk_debug_label_end_raw(RvkDevice* dev, VkCommandBuffer vkCmdBuffer) {
  if (dev->lib->flags & RvkLibFlags_Debug) {
    rvk_call(dev->lib, cmdEndDebugUtilsLabelEXT, vkCmdBuffer);
  }
}
