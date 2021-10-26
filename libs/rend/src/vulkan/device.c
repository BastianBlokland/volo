#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "log_logger.h"

#include "device_internal.h"

struct sRendVkDevice {
  VkInstance                       vkInstance;
  VkAllocationCallbacks*           vkAllocHost;
  VkPhysicalDevice                 vkPhysicalDevice;
  VkPhysicalDeviceProperties       vkProperties;
  VkPhysicalDeviceFeatures         vkFeatures;
  VkPhysicalDeviceMemoryProperties vkMemProperties;
  u32                              graphicsQueueIndex;
};

static String g_requiredExts[] = {
    string_static("VK_KHR_swapchain"),
};

static bool rend_vk_has_extension(VkExtensionProperties* props, const u32 propCount, String ext) {
  for (u32 i = 0; i != propCount; ++i) {
    if (string_eq(ext, string_from_null_term(props[i].extensionName))) {
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

static u32 rend_vk_pick_graphics_queue(VkPhysicalDevice vkPhysicalDevice) {
  VkQueueFamilyProperties queueFamilies[32];
  u32                     queueFamilyCount = array_elems(queueFamilies);
  vkGetPhysicalDeviceQueueFamilyProperties(vkPhysicalDevice, &queueFamilyCount, queueFamilies);

  for (u32 i = 0; i != queueFamilyCount; ++i) {
    if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      return i;
    }
  }
  diag_crash_msg("No graphics queue found");
}

static VkPhysicalDevice rend_vk_pick_physical_device(VkInstance vkInstance) {
  VkPhysicalDevice devices[32];
  u32              devicesCount = array_elems(devices);
  rend_vk_call(vkEnumeratePhysicalDevices, vkInstance, &devicesCount, devices);

  VkPhysicalDevice      bestVkDevice = null;
  i32                   bestScore    = -1;
  VkExtensionProperties exts[64];

  for (usize i = 0; i != devicesCount; ++i) {
    u32 extsCount = array_elems(exts);
    rend_vk_call(vkEnumerateDeviceExtensionProperties, devices[i], null, &extsCount, exts);

    i32 score = 0;
    array_for_t(g_requiredExts, String, reqExt, {
      if (!rend_vk_has_extension(exts, extsCount, *reqExt)) {
        score = -1;
        goto detectionDone;
      }
    });

    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(devices[i], &properties);

    score += rend_vk_devicetype_score_value(properties.deviceType);

  detectionDone:
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

RendVkDevice* rend_vk_device_create(VkInstance vkInstance, VkAllocationCallbacks* vkAllocHost) {
  VkPhysicalDevice vkPhysicalDevice = rend_vk_pick_physical_device(vkInstance);
  RendVkDevice*    device           = alloc_alloc_t(g_alloc_heap, RendVkDevice);
  *device                           = (RendVkDevice){
      .vkInstance         = vkInstance,
      .vkAllocHost        = vkAllocHost,
      .vkPhysicalDevice   = vkPhysicalDevice,
      .graphicsQueueIndex = rend_vk_pick_graphics_queue(vkPhysicalDevice),
  };

  vkGetPhysicalDeviceProperties(device->vkPhysicalDevice, &device->vkProperties);
  vkGetPhysicalDeviceFeatures(device->vkPhysicalDevice, &device->vkFeatures);
  vkGetPhysicalDeviceMemoryProperties(device->vkPhysicalDevice, &device->vkMemProperties);

  log_i(
      "Vulkan device created",
      log_param("deviceName", fmt_text(string_from_null_term(device->vkProperties.deviceName))),
      log_param("graphicsQueueIdx", fmt_int(device->graphicsQueueIndex)));

  return device;
}

void rend_vk_device_destroy(RendVkDevice* device) { alloc_free_t(g_alloc_heap, device); }
