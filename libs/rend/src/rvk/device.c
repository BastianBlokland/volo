#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_path.h"
#include "gap_native.h"
#include "log_logger.h"

#include "desc_internal.h"
#include "device_internal.h"
#include "mem_internal.h"

#define rend_debug_verbose true

static const RvkDebugFlags g_debugFlags      = rend_debug_verbose ? RvkDebugFlags_Verbose : 0;
static const String        g_validationLayer = string_static("VK_LAYER_KHRONOS_validation");
static const String        g_validationExt   = string_static("VK_EXT_debug_utils");
static String              g_requiredExts[]  = {string_static("VK_KHR_swapchain")};

static VkApplicationInfo rvk_instance_app_info() {
  return (VkApplicationInfo){
      .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName   = path_stem(g_path_executable).ptr,
      .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
      .pEngineName        = "volo",
      .engineVersion      = VK_MAKE_VERSION(0, 1, 0),
      .apiVersion         = VK_API_VERSION_1_1,
  };
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
  if (flags & RvkDeviceFlags_Validation) {
    output[i++] = g_validationExt.ptr;
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

  VkInstance result;
  rvk_call(vkCreateInstance, &createInfo, vkAlloc, &result);
  return result;
}

typedef struct {
  VkExtensionProperties* head;
  u32                    count;
} RendDeviceExts;

/**
 * Query a list of all supported device extensions.
 * NOTE: Free the list with 'rvk_device_exts_free()'
 */
static RendDeviceExts rvk_device_exts_query(VkPhysicalDevice vkPhysDev) {
  u32 count;
  rvk_call(vkEnumerateDeviceExtensionProperties, vkPhysDev, null, &count, null);
  VkExtensionProperties* props = alloc_array_t(g_alloc_heap, VkExtensionProperties, count);
  rvk_call(vkEnumerateDeviceExtensionProperties, vkPhysDev, null, &count, props);
  return (RendDeviceExts){.head = props, .count = count};
}

static void rvk_device_exts_free(RendDeviceExts exts) {
  alloc_free_array_t(g_alloc_heap, exts.head, exts.count);
}

/**
 * Check if the given extension is contained in the list of available device extensions.
 */
static bool rvk_device_has_ext(RendDeviceExts availableExts, String ext) {
  for (u32 i = 0; i != availableExts.count; ++i) {
    if (string_eq(ext, string_from_null_term(availableExts.head[i].extensionName))) {
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

static u32 rvk_device_pick_main_queue(VkPhysicalDevice vkPhysDev) {
  VkQueueFamilyProperties families[32];
  u32                     familyCount = array_elems(families);
  vkGetPhysicalDeviceQueueFamilyProperties(vkPhysDev, &familyCount, families);

  for (u32 i = 0; i != familyCount; ++i) {
    if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT &&
        families[i].queueFlags & VK_QUEUE_TRANSFER_BIT) {
      return i;
    }
  }
  diag_crash_msg("No main queue found");
}

static VkPhysicalDevice rvk_device_pick_physical_device(VkInstance vkInst) {
  VkPhysicalDevice vkPhysDevs[32];
  u32              vkPhysDevsCount = array_elems(vkPhysDevs);
  rvk_call(vkEnumeratePhysicalDevices, vkInst, &vkPhysDevsCount, vkPhysDevs);

  VkPhysicalDevice bestVkPhysDev = null;
  i32              bestScore     = -1;

  for (usize i = 0; i != vkPhysDevsCount; ++i) {
    const RendDeviceExts exts = rvk_device_exts_query(vkPhysDevs[i]);

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
    rvk_device_exts_free(exts);

    log_d(
        "Vulkan physical device detected",
        log_param("device-name", fmt_text(string_from_null_term(properties.deviceName))),
        log_param("device-type", fmt_text(rvk_devicetype_str(properties.deviceType))),
        log_param("vendor", fmt_text(rvk_vendor_str(properties.vendorID))),
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

static VkPhysicalDeviceFeatures rvk_device_pick_features(RvkDevice* dev) {
  VkPhysicalDeviceFeatures result = {0};
  if (dev->vkSupportedFeatures.pipelineStatisticsQuery) {
    result.pipelineStatisticsQuery = true;
  }
  if (dev->vkSupportedFeatures.samplerAnisotropy) {
    result.samplerAnisotropy = true;
  }
  if (dev->vkSupportedFeatures.fillModeNonSolid) {
    result.fillModeNonSolid = true;
  }
  if (dev->vkSupportedFeatures.wideLines) {
    result.wideLines = true;
  }
  return result;
}

static VkDevice rvk_device_create_internal(RvkDevice* dev) {
  // Request our main queue (both graphics and transfer) to be created on the device.
  const f32               queuePriority   = 1.0f;
  VkDeviceQueueCreateInfo queueCreateInfo = {
      .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = dev->mainQueueIndex,
      .queueCount       = 1,
      .pQueuePriorities = &queuePriority,
  };

  const char* extensionsToEnabled[array_elems(g_requiredExts)];
  for (u32 i = 0; i != array_elems(g_requiredExts); ++i) {
    extensionsToEnabled[i] = g_requiredExts[i].ptr;
  }

  const VkPhysicalDeviceFeatures featuresToEnable = rvk_device_pick_features(dev);
  VkDeviceCreateInfo             createInfo       = {
      .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pQueueCreateInfos       = &queueCreateInfo,
      .queueCreateInfoCount    = 1,
      .enabledExtensionCount   = array_elems(extensionsToEnabled),
      .ppEnabledExtensionNames = extensionsToEnabled,
      .pEnabledFeatures        = &featuresToEnable,
  };

  VkDevice result;
  rvk_call(vkCreateDevice, dev->vkPhysDev, &createInfo, &dev->vkAlloc, &result);
  return result;
}

static VkCommandPool rvk_device_commandpool_create(RvkDevice* dev) {
  VkCommandPoolCreateInfo createInfo = {
      .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .queueFamilyIndex = dev->mainQueueIndex,
      .flags =
          VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
  };
  VkCommandPool result;
  rvk_call(vkCreateCommandPool, dev->vkDev, &createInfo, &dev->vkAlloc, &result);
  return result;
}

static VkFormat rvk_device_pick_depthformat(RvkDevice* dev) {
  static const VkFormat             desiredFormat = VK_FORMAT_D32_SFLOAT;
  static const VkFormatFeatureFlags features      = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;

  VkFormatProperties properties;
  vkGetPhysicalDeviceFormatProperties(dev->vkPhysDev, desiredFormat, &properties);

  if ((properties.optimalTilingFeatures & features) == features) {
    return desiredFormat;
  }
  diag_crash_msg("No suitable depth-format found");
}

RvkDevice* rvk_device_create() {
  RvkDevice* dev = alloc_alloc_t(g_alloc_heap, RvkDevice);
  *dev           = (RvkDevice){
      .vkAlloc = rvk_mem_allocator(g_alloc_heap),
  };

  if (rvk_instance_layer_supported(g_validationLayer)) {
    dev->flags |= RvkDeviceFlags_Validation;
  }
  dev->vkInst         = rvk_instance_create(&dev->vkAlloc, dev->flags);
  dev->vkPhysDev      = rvk_device_pick_physical_device(dev->vkInst);
  dev->mainQueueIndex = rvk_device_pick_main_queue(dev->vkPhysDev);

  vkGetPhysicalDeviceProperties(dev->vkPhysDev, &dev->vkProperties);
  vkGetPhysicalDeviceFeatures(dev->vkPhysDev, &dev->vkSupportedFeatures);
  vkGetPhysicalDeviceMemoryProperties(dev->vkPhysDev, &dev->vkMemProperties);

  dev->vkDev = rvk_device_create_internal(dev);
  vkGetDeviceQueue(dev->vkDev, dev->mainQueueIndex, 0, &dev->vkMainQueue);

  dev->vkMainCommandPool = rvk_device_commandpool_create(dev);
  dev->vkDepthFormat     = rvk_device_pick_depthformat(dev);

  if (dev->flags & RvkDeviceFlags_Validation) {
    dev->debug = rvk_debug_create(dev->vkInst, dev->vkDev, &dev->vkAlloc, g_debugFlags);
    rvk_name_queue(dev->debug, dev->vkMainQueue, "main");
    rvk_name_commandpool(dev->debug, dev->vkMainCommandPool, "main");
  }

  dev->memPool  = rvk_mem_pool_create(dev->vkDev, dev->vkMemProperties, dev->vkProperties.limits);
  dev->descPool = rvk_desc_pool_create(dev->vkDev);

  log_i(
      "Vulkan device created",
      log_param("validation", fmt_bool(dev->flags & RvkDeviceFlags_Validation)),
      log_param("device-name", fmt_text(string_from_null_term(dev->vkProperties.deviceName))),
      log_param("main-queue-idx", fmt_int(dev->mainQueueIndex)),
      log_param("depth-format", fmt_text(rvk_format_info(dev->vkDepthFormat).name)));

  return dev;
}

void rvk_device_destroy(RvkDevice* dev) {

  rvk_call(vkDeviceWaitIdle, dev->vkDev);

  rvk_mem_pool_destroy(dev->memPool);
  rvk_desc_pool_destroy(dev->descPool);

  vkDestroyCommandPool(dev->vkDev, dev->vkMainCommandPool, &dev->vkAlloc);
  vkDestroyDevice(dev->vkDev, &dev->vkAlloc);

  if (dev->debug) {
    rvk_debug_destroy(dev->debug);
  }

  vkDestroyInstance(dev->vkInst, &dev->vkAlloc);
  alloc_free_t(g_alloc_heap, dev);

  log_i("Vulkan device destroyed");
}
