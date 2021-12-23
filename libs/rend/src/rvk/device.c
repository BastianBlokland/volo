#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_path.h"
#include "gap_native.h"
#include "log_logger.h"

#include "desc_internal.h"
#include "device_internal.h"
#include "mem_internal.h"
#include "repository_internal.h"
#include "transfer_internal.h"

#define rend_debug_verbose true

static const RvkDebugFlags g_debugFlags      = rend_debug_verbose ? RvkDebugFlags_Verbose : 0;
static const String        g_validationLayer = string_static("VK_LAYER_KHRONOS_validation");
static const String        g_validationExt   = string_static("VK_EXT_debug_utils");
static String              g_requiredExts[]  = {
    string_static("VK_KHR_swapchain"),
    string_static("VK_KHR_shader_float16_int8"),
};

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
  VkExtensionProperties* values;
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
  return (RendDeviceExts){.values = props, .count = count};
}

static void rvk_device_exts_free(RendDeviceExts exts) {
  alloc_free_array_t(g_alloc_heap, exts.values, exts.count);
}

/**
 * Check if the given extension is contained in the list of available device extensions.
 */
static bool rvk_device_has_ext(RendDeviceExts availableExts, String ext) {
  array_ptr_for_t(availableExts, VkExtensionProperties, props) {
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

static u32 rvk_device_pick_graphics_queue(VkPhysicalDevice vkPhysDev) {
  VkQueueFamilyProperties families[32];
  u32                     familyCount = array_elems(families);
  vkGetPhysicalDeviceQueueFamilyProperties(vkPhysDev, &familyCount, families);

  for (u32 i = 0; i != familyCount; ++i) {
    if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      return i;
    }
  }
  diag_crash_msg("No graphics queue found");
}

static u32 rvk_device_pick_transfer_queue(VkPhysicalDevice vkPhysDev) {
  VkQueueFamilyProperties families[32];
  u32                     familyCount = array_elems(families);
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

  VkPhysicalDevice16BitStorageFeatures float16IStorageFeatures = {
      .sType                    = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES_KHR,
      .storageBuffer16BitAccess = true,
  };

  VkPhysicalDeviceShaderFloat16Int8Features float16Int8Features = {
      .sType         = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES,
      .pNext         = &float16IStorageFeatures,
      .shaderFloat16 = true,
  };

  const char* extensionsToEnabled[array_elems(g_requiredExts)];
  for (u32 i = 0; i != array_elems(g_requiredExts); ++i) {
    extensionsToEnabled[i] = g_requiredExts[i].ptr;
  }

  const VkPhysicalDeviceFeatures featuresToEnable = rvk_device_pick_features(dev);
  VkDeviceCreateInfo             createInfo       = {
      .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext                   = &float16Int8Features,
      .pQueueCreateInfos       = queueCreateInfos,
      .queueCreateInfoCount    = queueCreateInfoCount,
      .enabledExtensionCount   = array_elems(extensionsToEnabled),
      .ppEnabledExtensionNames = extensionsToEnabled,
      .pEnabledFeatures        = &featuresToEnable,
  };

  VkDevice result;
  rvk_call(vkCreateDevice, dev->vkPhysDev, &createInfo, &dev->vkAlloc, &result);
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
  dev->vkInst             = rvk_instance_create(&dev->vkAlloc, dev->flags);
  dev->vkPhysDev          = rvk_device_pick_physical_device(dev->vkInst);
  dev->graphicsQueueIndex = rvk_device_pick_graphics_queue(dev->vkPhysDev);
  dev->transferQueueIndex = rvk_device_pick_transfer_queue(dev->vkPhysDev);

  vkGetPhysicalDeviceProperties(dev->vkPhysDev, &dev->vkProperties);
  vkGetPhysicalDeviceFeatures(dev->vkPhysDev, &dev->vkSupportedFeatures);
  vkGetPhysicalDeviceMemoryProperties(dev->vkPhysDev, &dev->vkMemProperties);

  dev->vkDev = rvk_device_create_internal(dev);
  vkGetDeviceQueue(dev->vkDev, dev->graphicsQueueIndex, 0, &dev->vkGraphicsQueue);
  vkGetDeviceQueue(dev->vkDev, dev->transferQueueIndex, 0, &dev->vkTransferQueue);

  dev->vkDepthFormat = rvk_device_pick_depthformat(dev);

  if (dev->flags & RvkDeviceFlags_Validation) {
    dev->debug = rvk_debug_create(dev->vkInst, dev->vkDev, &dev->vkAlloc, g_debugFlags);
    rvk_name_queue(dev->debug, dev->vkGraphicsQueue, "graphics");

    if (dev->vkTransferQueue != dev->vkGraphicsQueue) {
      rvk_name_queue(dev->debug, dev->vkTransferQueue, "transfer");
    }
  }

  dev->memPool    = rvk_mem_pool_create(dev->vkDev, dev->vkMemProperties, dev->vkProperties.limits);
  dev->descPool   = rvk_desc_pool_create(dev->vkDev);
  dev->transferer = rvk_transferer_create(dev);
  dev->repository = rvk_repository_create();

  log_i(
      "Vulkan device created",
      log_param("validation", fmt_bool(dev->flags & RvkDeviceFlags_Validation)),
      log_param("device-name", fmt_text(string_from_null_term(dev->vkProperties.deviceName))),
      log_param("graphics-queue-idx", fmt_int(dev->graphicsQueueIndex)),
      log_param("transfer-queue-idx", fmt_int(dev->transferQueueIndex)),
      log_param("depth-format", fmt_text(rvk_format_info(dev->vkDepthFormat).name)));

  return dev;
}

void rvk_device_destroy(RvkDevice* dev) {

  rvk_device_wait_idle(dev);

  rvk_repository_destroy(dev->repository);
  rvk_transferer_destroy(dev->transferer);
  rvk_desc_pool_destroy(dev->descPool);
  rvk_mem_pool_destroy(dev->memPool);
  vkDestroyDevice(dev->vkDev, &dev->vkAlloc);

  if (dev->debug) {
    rvk_debug_destroy(dev->debug);
  }

  vkDestroyInstance(dev->vkInst, &dev->vkAlloc);
  alloc_free_t(g_alloc_heap, dev);

  log_d("Vulkan device destroyed");
}

void rvk_device_update(RvkDevice* dev) { rvk_transfer_flush(dev->transferer); }

void rvk_device_wait_idle(const RvkDevice* dev) { rvk_call(vkDeviceWaitIdle, dev->vkDev); }
