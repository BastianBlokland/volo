#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "log_logger.h"

#include "device_internal.h"

struct sRendVkDevice {
  RendVkDebug*                     debug;
  VkInstance                       vkInstance;
  VkAllocationCallbacks*           vkAllocHost;
  VkPhysicalDevice                 vkPhysicalDevice;
  VkPhysicalDeviceProperties       vkProperties;
  VkPhysicalDeviceFeatures         vkSupportedFeatures;
  VkPhysicalDeviceMemoryProperties vkMemProperties;
  VkDevice                         vkDevice;
  u32                              mainQueueIndex;
  VkQueue                          vkMainQueue;
  VkCommandPool                    vkMainCommandPool;
};

static String g_requiredExts[] = {
    string_static("VK_KHR_swapchain"),
};

typedef struct {
  VkExtensionProperties* head;
  u32                    count;
} RendDeviceExts;

static RendDeviceExts rend_vk_exts_query(VkPhysicalDevice vkPhysicalDevice) {
  u32 count;
  rend_vk_call(vkEnumerateDeviceExtensionProperties, vkPhysicalDevice, null, &count, null);
  VkExtensionProperties* props = alloc_alloc_array_t(g_alloc_heap, VkExtensionProperties, count);
  rend_vk_call(vkEnumerateDeviceExtensionProperties, vkPhysicalDevice, null, &count, props);
  return (RendDeviceExts){.head = props, .count = count};
}

static void rend_vk_exts_free(RendDeviceExts extensions) {
  alloc_free_array_t(g_alloc_heap, extensions.head, extensions.count);
}

static bool rend_vk_has_ext(RendDeviceExts availableExts, String ext) {
  for (u32 i = 0; i != availableExts.count; ++i) {
    if (string_eq(ext, string_from_null_term(availableExts.head[i].extensionName))) {
      return true;
    }
  }
  return false;
}

static i32 rend_vk_devicetype_score_value(const VkPhysicalDeviceType device) {
  switch (device) {
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

static u32 rend_vk_pick_main_queue(VkPhysicalDevice vkPhysicalDevice) {
  VkQueueFamilyProperties families[32];
  u32                     familyCount = array_elems(families);
  vkGetPhysicalDeviceQueueFamilyProperties(vkPhysicalDevice, &familyCount, families);

  for (u32 i = 0; i != familyCount; ++i) {
    if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT &&
        families[i].queueFlags & VK_QUEUE_TRANSFER_BIT) {
      return i;
    }
  }
  diag_crash_msg("No main queue found");
}

static VkPhysicalDevice rend_vk_pick_physical_device(VkInstance vkInstance) {
  VkPhysicalDevice devices[32];
  u32              devicesCount = array_elems(devices);
  rend_vk_call(vkEnumeratePhysicalDevices, vkInstance, &devicesCount, devices);

  VkPhysicalDevice bestVkDevice = null;
  i32              bestScore    = -1;

  for (usize i = 0; i != devicesCount; ++i) {
    const RendDeviceExts exts = rend_vk_exts_query(devices[i]);

    i32 score = 0;
    array_for_t(g_requiredExts, String, reqExt, {
      if (!rend_vk_has_ext(exts, *reqExt)) {
        score = -1;
        goto detectionDone;
      }
    });

    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(devices[i], &properties);

    score += rend_vk_devicetype_score_value(properties.deviceType);

  detectionDone:
    rend_vk_exts_free(exts);

    log_d(
        "Vulkan physical device detected",
        log_param("deviceName", fmt_text(string_from_null_term(properties.deviceName))),
        log_param("deviceType", fmt_text(rend_vk_devicetype_str(properties.deviceType))),
        log_param("vendor", fmt_text(rend_vk_vendor_str(properties.vendorID))),
        log_param("score", fmt_int(score)));

    if (score > bestScore) {
      bestVkDevice = devices[i];
      bestScore    = score;
    }
  }
  if (!bestVkDevice) {
    diag_crash_msg("No compatible Vulkan device found");
  }
  return bestVkDevice;
}

static VkPhysicalDeviceFeatures rend_vk_pick_features(RendVkDevice* device) {
  VkPhysicalDeviceFeatures result = {0};
  if (device->vkSupportedFeatures.pipelineStatisticsQuery) {
    result.pipelineStatisticsQuery = true;
  }
  if (device->vkSupportedFeatures.samplerAnisotropy) {
    result.samplerAnisotropy = true;
  }
  if (device->vkSupportedFeatures.fillModeNonSolid) {
    result.fillModeNonSolid = true;
  }
  if (device->vkSupportedFeatures.wideLines) {
    result.wideLines = true;
  }
  return result;
}

static void rend_vk_device_init(RendVkDevice* device) {
  // Request our main queue (both graphics and transfer) to be created on the device.
  const float             queuePriority   = 1.0f;
  VkDeviceQueueCreateInfo queueCreateInfo = {
      .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = device->mainQueueIndex,
      .queueCount       = 1,
      .pQueuePriorities = &queuePriority,
  };

  VkPhysicalDeviceFeatures featuresToEnable = rend_vk_pick_features(device);

  const char* extensionsToEnabled[array_elems(g_requiredExts)];
  for (u32 i = 0; i != array_elems(g_requiredExts); ++i) {
    extensionsToEnabled[i] = g_requiredExts[i].ptr;
  }

  VkDeviceCreateInfo createInfo = {
      .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pQueueCreateInfos       = &queueCreateInfo,
      .queueCreateInfoCount    = 1,
      .enabledExtensionCount   = array_elems(extensionsToEnabled),
      .ppEnabledExtensionNames = extensionsToEnabled,
      .pEnabledFeatures        = &featuresToEnable,
  };
  rend_vk_call(
      vkCreateDevice,
      device->vkPhysicalDevice,
      &createInfo,
      device->vkAllocHost,
      &device->vkDevice);

  // Retrive our created main-queue.
  vkGetDeviceQueue(device->vkDevice, device->mainQueueIndex, 0, &device->vkMainQueue);
}

static void rend_vk_commandpool_init(RendVkDevice* device) {
  VkCommandPoolCreateInfo createInfo = {
      .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .queueFamilyIndex = device->mainQueueIndex,
      .flags =
          VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
  };
  rend_vk_call(
      vkCreateCommandPool,
      device->vkDevice,
      &createInfo,
      device->vkAllocHost,
      &device->vkMainCommandPool);
}

RendVkDevice* rend_vk_device_create(
    VkInstance vkInstance, VkAllocationCallbacks* vkAllocHost, RendVkDebug* debug) {
  VkPhysicalDevice vkPhysicalDevice = rend_vk_pick_physical_device(vkInstance);
  RendVkDevice*    device           = alloc_alloc_t(g_alloc_heap, RendVkDevice);
  *device                           = (RendVkDevice){
      .debug            = debug,
      .vkInstance       = vkInstance,
      .vkAllocHost      = vkAllocHost,
      .vkPhysicalDevice = vkPhysicalDevice,
      .mainQueueIndex   = rend_vk_pick_main_queue(vkPhysicalDevice),
  };

  vkGetPhysicalDeviceProperties(device->vkPhysicalDevice, &device->vkProperties);
  vkGetPhysicalDeviceFeatures(device->vkPhysicalDevice, &device->vkSupportedFeatures);
  vkGetPhysicalDeviceMemoryProperties(device->vkPhysicalDevice, &device->vkMemProperties);

  rend_vk_device_init(device);
  rend_vk_commandpool_init(device);

  if (debug) {
    dbg_name_queue(device->debug, device->vkDevice, device->vkMainQueue, "main");
    dbg_name_commandpool(device->debug, device->vkDevice, device->vkMainCommandPool, "main");
  }

  log_i(
      "Vulkan device created",
      log_param("deviceName", fmt_text(string_from_null_term(device->vkProperties.deviceName))),
      log_param("mainQueueIdx", fmt_int(device->mainQueueIndex)));

  return device;
}

void rend_vk_device_destroy(RendVkDevice* device) {

  // Wait for device activity be done before destroying the device.
  rend_vk_call(vkDeviceWaitIdle, device->vkDevice);

  vkDestroyCommandPool(device->vkDevice, device->vkMainCommandPool, device->vkAllocHost);
  vkDestroyDevice(device->vkDevice, device->vkAllocHost);

  alloc_free_t(g_alloc_heap, device);
}
