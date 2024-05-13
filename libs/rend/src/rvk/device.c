#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_path.h"
#include "gap_native.h"
#include "log_logger.h"

#include "debug_internal.h"
#include "desc_internal.h"
#include "device_internal.h"
#include "mem_internal.h"
#include "psocache_internal.h"
#include "repository_internal.h"
#include "sampler_internal.h"
#include "transfer_internal.h"

static const String g_validationLayer = string_static("VK_LAYER_KHRONOS_validation");
static const VkValidationFeatureEnableEXT g_validationEnabledFeatures[] = {
    VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT,
#if VK_EXT_VALIDATION_FEATURES_SPEC_VERSION >= 4
    VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,
#endif
};
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
static const String g_debugExts[] = {
    string_static("VK_EXT_debug_utils"),
};

#ifdef VOLO_LINUX
/**
 * On linux disable present-id (VK_KHR_present_id) and present-wait (VK_KHR_present_wait) even if
 * the device supports it.
 * Unfortunately at least the 510 NVidia driver on x11 claims to support these but then fails to
 * create a swapchain when either is enabled.
 * TODO: Test support on other vendors running on Linux (and on the x11 window system).
 */
MAYBE_UNUSED static const bool g_rend_enable_vk_present_id   = false;
MAYBE_UNUSED static const bool g_rend_enable_vk_present_wait = false;
#else
MAYBE_UNUSED static const bool g_rend_enable_vk_present_id   = true;
MAYBE_UNUSED static const bool g_rend_enable_vk_present_wait = true;
#endif

static VkApplicationInfo rvk_instance_app_info(void) {
  return (VkApplicationInfo){
      .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName   = path_stem(g_pathExecutable).ptr,
      .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
      .pEngineName        = "volo",
      .engineVersion      = VK_MAKE_VERSION(0, 1, 0),
      .apiVersion         = VK_API_VERSION_1_1,
  };
}

typedef struct {
  VkExtensionProperties* values;
  u32                    count;
} RendVkExts;

/**
 * Query a list of all supported device extensions.
 * NOTE: Free the list with 'rvk_device_exts_free()'
 */
static RendVkExts rvk_device_exts_query(VkPhysicalDevice vkPhysDev) {
  u32 count;
  rvk_call(vkEnumerateDeviceExtensionProperties, vkPhysDev, null, &count, null);
  VkExtensionProperties* props = alloc_array_t(g_allocHeap, VkExtensionProperties, count);
  rvk_call(vkEnumerateDeviceExtensionProperties, vkPhysDev, null, &count, props);
  return (RendVkExts){.values = props, .count = count};
}

static void rvk_vk_exts_free(RendVkExts exts) {
  alloc_free_array_t(g_allocHeap, exts.values, exts.count);
}

/**
 * Check if the given extension is contained in the list of available device extensions.
 */
static bool rvk_device_has_ext(RendVkExts availableExts, String ext) {
  array_ptr_for_t(availableExts, VkExtensionProperties, props) {
    if (string_eq(ext, string_from_null_term(props->extensionName))) {
      return true;
    }
  }
  return false;
}

/**
 * Check if the given instance layer is supported.
 */
static bool rvk_instance_layer_supported(const String layer) {
  VkLayerProperties availableLayers[32];
  u32               availableLayerCount = array_elems(availableLayers);
  rvk_call(vkEnumerateInstanceLayerProperties, &availableLayerCount, availableLayers);

  for (u32 i = 0; i != availableLayerCount; ++i) {
    if (string_eq(layer, string_from_null_term(availableLayers[i].layerName))) {
      return true;
    }
  }
  return false;
}

/**
 * Retrieve a list of required instance layers.
 */
static u32 rvk_instance_required_layers(const char** output, const RvkDeviceFlags flags) {
  u32 i = 0;
  if (flags & RvkDeviceFlags_Validation) {
    output[i++] = g_validationLayer.ptr;
  }
  return i;
}

/**
 * Retrieve a list of required instance extensions.
 */
static u32 rvk_instance_required_extensions(const char** output, const RvkDeviceFlags flags) {
  u32 i       = 0;
  output[i++] = VK_KHR_SURFACE_EXTENSION_NAME;
  switch (gap_native_wm()) {
  case GapNativeWm_Xcb:
    output[i++] = "VK_KHR_xcb_surface";
    break;
  case GapNativeWm_Win32:
    output[i++] = "VK_KHR_win32_surface";
    break;
  }
  if (flags & (RvkDeviceFlags_Validation | RvkDeviceFlags_Debug)) {
    array_for_t(g_debugExts, String, ext) { output[i++] = ext->ptr; }
  }
  return i;
}

static VkInstance rvk_instance_create(VkAllocationCallbacks* vkAlloc, const RvkDeviceFlags flags) {
  const VkApplicationInfo appInfo = rvk_instance_app_info();

  const char* layerNames[16];
  const u32   layerCount = rvk_instance_required_layers(layerNames, flags);

  const char* extensionNames[16];
  const u32   extensionCount = rvk_instance_required_extensions(extensionNames, flags);

  VkInstanceCreateInfo createInfo = {
      .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo        = &appInfo,
      .enabledExtensionCount   = extensionCount,
      .ppEnabledExtensionNames = extensionNames,
      .enabledLayerCount       = layerCount,
      .ppEnabledLayerNames     = layerNames,
  };

  VkValidationFeaturesEXT validationFeatures;
  if (flags & RvkDeviceFlags_Validation) {
    validationFeatures = (VkValidationFeaturesEXT){
        .sType                         = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
        .pEnabledValidationFeatures    = g_validationEnabledFeatures,
        .enabledValidationFeatureCount = array_elems(g_validationEnabledFeatures),
    };
    createInfo.pNext = &validationFeatures;
  }

  VkInstance result;
  rvk_call(vkCreateInstance, &createInfo, vkAlloc, &result);
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

static u32 rvk_device_pick_graphics_queue(VkPhysicalDevice vkPhysDev) {
  VkQueueFamilyProperties families[32] = {0};
  u32                     familyCount  = array_elems(families);
  vkGetPhysicalDeviceQueueFamilyProperties(vkPhysDev, &familyCount, families);

  for (u32 i = 0; i != familyCount; ++i) {
    if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      return i;
    }
  }
  diag_crash_msg("No graphics queue found");
}

static u32 rvk_device_pick_transfer_queue(VkPhysicalDevice vkPhysDev) {
  VkQueueFamilyProperties families[32] = {0};
  u32                     familyCount  = array_elems(families);
  vkGetPhysicalDeviceQueueFamilyProperties(vkPhysDev, &familyCount, families);

  for (u32 i = 0; i != familyCount; ++i) {
    const bool hasGraphics = (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
    const bool hasCompute  = (families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
    if (families[i].queueFlags & VK_QUEUE_TRANSFER_BIT && !hasGraphics && !hasCompute) {
      return i;
    }
  }
  for (u32 i = 0; i != familyCount; ++i) {
    if (families[i].queueFlags & VK_QUEUE_TRANSFER_BIT) {
      return i;
    }
  }
  diag_crash_msg("No transfer queue found");
}

static VkPhysicalDevice rvk_device_pick_physical_device(VkInstance vkInst) {
  VkPhysicalDevice vkPhysDevs[32];
  u32              vkPhysDevsCount = array_elems(vkPhysDevs);
  rvk_call(vkEnumeratePhysicalDevices, vkInst, &vkPhysDevsCount, vkPhysDevs);

  VkPhysicalDevice bestVkPhysDev  = null;
  u32              bestApiVersion = 0;
  i32              bestScore      = -1;

  for (usize i = 0; i != vkPhysDevsCount; ++i) {
    const RendVkExts exts = rvk_device_exts_query(vkPhysDevs[i]);

    i32 score = 0;
    array_for_t(g_requiredExts, String, reqExt) {
      if (!rvk_device_has_ext(exts, *reqExt)) {
        score = -1;
        goto detectionDone;
      }
    }

    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(vkPhysDevs[i], &properties);

    score += rvk_device_type_score_value(properties.deviceType);

  detectionDone:
    rvk_vk_exts_free(exts);

    log_d(
        "Vulkan physical device detected",
        log_param("device-name", fmt_text(string_from_null_term(properties.deviceName))),
        log_param("device-type", fmt_text(rvk_devicetype_str(properties.deviceType))),
        log_param("vendor", fmt_text(rvk_vendor_str(properties.vendorID))),
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

static VkDevice rvk_device_create_internal(RvkDevice* dev) {
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
  if (dev->transferQueueIndex != dev->graphicsQueueIndex) {
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
  const RendVkExts supportedExts = rvk_device_exts_query(dev->vkPhysDev);
  array_for_t(g_optionalExts, String, optExt) {
    if (rvk_device_has_ext(supportedExts, *optExt)) {
      extsToEnable[extsToEnableCount++] = optExt->ptr; // TODO: Hacky as it assumes null-term.
    }
  }
  rvk_vk_exts_free(supportedExts);

  // Add optional features..
  void* nextOptFeature = null;
#ifdef VK_KHR_present_id
  VkPhysicalDevicePresentIdFeaturesKHR optFeaturePresentId = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR,
      .pNext = nextOptFeature,
  };
  nextOptFeature = &optFeaturePresentId;
#endif
#ifdef VK_KHR_present_wait
  VkPhysicalDevicePresentWaitFeaturesKHR optFeaturePresentWait = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR,
      .pNext = nextOptFeature,
  };
  nextOptFeature = &optFeaturePresentWait;
#endif
  VkPhysicalDeviceFeatures2 supportedFeatures = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
      .pNext = nextOptFeature,
  };
  vkGetPhysicalDeviceFeatures2(dev->vkPhysDev, &supportedFeatures);

#ifdef VK_KHR_present_id
  if (g_rend_enable_vk_present_id && optFeaturePresentId.presentId) {
    extsToEnable[extsToEnableCount++] = "VK_KHR_present_id";
    dev->flags |= RvkDeviceFlags_SupportPresentId;
  }
#endif
#ifdef VK_KHR_present_wait
  if (g_rend_enable_vk_present_wait && optFeaturePresentWait.presentWait) {
    extsToEnable[extsToEnableCount++] = "VK_KHR_present_wait";
    dev->flags |= RvkDeviceFlags_SupportPresentWait;
  }
#endif

  VkPhysicalDevice16BitStorageFeatures float16IStorageFeatures = {
      .sType                    = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES_KHR,
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
  rvk_call(vkCreateDevice, dev->vkPhysDev, &createInfo, &dev->vkAlloc, &result);
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

RvkDevice* rvk_device_create(const RendSettingsGlobalComp* settingsGlobal) {
  RvkDevice* dev = alloc_alloc_t(g_allocHeap, RvkDevice);
  *dev           = (RvkDevice){
                .vkAlloc          = rvk_mem_allocator(g_allocHeap),
                .queueSubmitMutex = thread_mutex_create(g_allocHeap),
  };
  if (settingsGlobal->flags & RendGlobalFlags_TextureCompression) {
    dev->flags |= RvkDeviceFlags_TextureCompression;
  }
  const bool validationDesired = (settingsGlobal->flags & RendGlobalFlags_Validation) != 0;
  if (validationDesired && rvk_instance_layer_supported(g_validationLayer)) {
    dev->flags |= RvkDeviceFlags_Validation;
    dev->flags |= RvkDeviceFlags_Debug; // Validation will also enable debug features.
  }
  const bool debugDesired = (settingsGlobal->flags & RendGlobalFlags_DebugGpu) != 0;
  if (debugDesired) {
    // TODO: Support enabling this optionally based on instance support, at the moment creating the
    // instance would fail if unsupported.
    dev->flags |= RvkDeviceFlags_Debug;
  }

  dev->vkInst    = rvk_instance_create(&dev->vkAlloc, dev->flags);
  dev->vkPhysDev = rvk_device_pick_physical_device(dev->vkInst);

  dev->graphicsQueueIndex = rvk_device_pick_graphics_queue(dev->vkPhysDev);
  dev->transferQueueIndex = rvk_device_pick_transfer_queue(dev->vkPhysDev);

  vkGetPhysicalDeviceProperties(dev->vkPhysDev, &dev->vkProperties);
  vkGetPhysicalDeviceMemoryProperties(dev->vkPhysDev, &dev->vkMemProperties);

  dev->vkDev = rvk_device_create_internal(dev);
  vkGetDeviceQueue(dev->vkDev, dev->graphicsQueueIndex, 0, &dev->vkGraphicsQueue);
  vkGetDeviceQueue(dev->vkDev, dev->transferQueueIndex, 0, &dev->vkTransferQueue);

  dev->vkDepthFormat = rvk_device_pick_depthformat(dev);

  if (dev->flags & RvkDeviceFlags_Debug) {
    const bool          verbose    = (settingsGlobal->flags & RendGlobalFlags_Verbose) != 0;
    const RvkDebugFlags debugFlags = verbose ? RvkDebugFlags_Verbose : 0;
    dev->debug = rvk_debug_create(dev->vkInst, dev->vkDev, &dev->vkAlloc, debugFlags);
    if (dev->transferQueueIndex == dev->graphicsQueueIndex) {
      rvk_debug_name_queue(dev->debug, dev->vkGraphicsQueue, "graphics_and_transfer");
    } else {
      rvk_debug_name_queue(dev->debug, dev->vkGraphicsQueue, "graphics");
      rvk_debug_name_queue(dev->debug, dev->vkTransferQueue, "transfer");
    }
  }

  dev->vkPipelineCache = rvk_psocache_load(dev);
  dev->memPool  = rvk_mem_pool_create(dev->vkDev, dev->vkMemProperties, dev->vkProperties.limits);
  dev->descPool = rvk_desc_pool_create(dev);
  dev->samplerPool = rvk_sampler_pool_create(dev);
  dev->transferer  = rvk_transferer_create(dev);
  dev->repository  = rvk_repository_create();

  log_i(
      "Vulkan device created",
      log_param("device-name", fmt_text(string_from_null_term(dev->vkProperties.deviceName))),
      log_param("graphics-queue-idx", fmt_int(dev->graphicsQueueIndex)),
      log_param("transfer-queue-idx", fmt_int(dev->transferQueueIndex)),
      log_param("depth-format", fmt_text(rvk_format_info(dev->vkDepthFormat).name)),
      log_param("validation-enabled", fmt_bool(dev->flags & RvkDeviceFlags_Validation)),
      log_param("present-id-enabled", fmt_bool(dev->flags & RvkDeviceFlags_SupportPresentId)),
      log_param("present-wait-enabled", fmt_bool(dev->flags & RvkDeviceFlags_SupportPresentWait)));

  return dev;
}

void rvk_device_destroy(RvkDevice* dev) {

  rvk_device_wait_idle(dev);

  rvk_psocache_save(dev, dev->vkPipelineCache);
  vkDestroyPipelineCache(dev->vkDev, dev->vkPipelineCache, &dev->vkAlloc);

  rvk_repository_destroy(dev->repository);
  rvk_transferer_destroy(dev->transferer);
  rvk_sampler_pool_destroy(dev->samplerPool);
  rvk_desc_pool_destroy(dev->descPool);
  rvk_mem_pool_destroy(dev->memPool);
  vkDestroyDevice(dev->vkDev, &dev->vkAlloc);

  if (dev->debug) {
    rvk_debug_destroy(dev->debug);
  }

  vkDestroyInstance(dev->vkInst, &dev->vkAlloc);
  thread_mutex_destroy(dev->queueSubmitMutex);
  alloc_free_t(g_allocHeap, dev);

  log_d("Vulkan device destroyed");
}

bool rvk_device_format_supported(
    const RvkDevice* dev, const VkFormat format, const VkFormatFeatureFlags requiredFeatures) {
  VkFormatProperties properties;
  vkGetPhysicalDeviceFormatProperties(dev->vkPhysDev, format, &properties);
  return (properties.optimalTilingFeatures & requiredFeatures) == requiredFeatures;
}

String rvk_device_name(const RvkDevice* dev) {
  return string_from_null_term(dev->vkProperties.deviceName);
}

void rvk_device_update(RvkDevice* dev) { rvk_transfer_flush(dev->transferer); }

void rvk_device_wait_idle(const RvkDevice* dev) { rvk_call(vkDeviceWaitIdle, dev->vkDev); }
