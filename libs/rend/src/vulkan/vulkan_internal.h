#pragma once
#include "core_string.h"

#include <vulkan/vulkan.h>

/**
 * Load a Vulkan instance api by name.
 */
#define rend_vk_func_load_instance(_INSTANCE_, _API_)                                              \
  ((PFN_##_API_)rend_vk_func_load_instance_internal((_INSTANCE_), string_lit(#_API_)))

/**
 * Load a Vulkan device api by name.
 */
#define rend_vk_func_load_device(_DEVICE_, _API_)                                                  \
  ((PFN_##_API_)rend_vk_func_load_device_internal((_DEVICE_), string_lit(#_API_)))

/**
 * Call a Vulkan api and check its result.
 */
#define rend_vk_call(_API_, ...) rend_vk_check(string_lit(#_API_), _API_(__VA_ARGS__))

void*  rend_vk_func_load_instance_internal(VkInstance, String api);
void*  rend_vk_func_load_device_internal(VkDevice, String api);
void   rend_vk_check(String api, VkResult);
String rend_vk_result_str(VkResult);
String rend_vk_devicetype_str(VkPhysicalDeviceType);
String rend_vk_vendor_str(u32 vendorId);
String rend_vk_colorspace_str(VkColorSpaceKHR);

typedef struct {
  String name;
  u32    size, channels;
} RendVkFormatInfo;

RendVkFormatInfo rend_vk_format_info(VkFormat format);
