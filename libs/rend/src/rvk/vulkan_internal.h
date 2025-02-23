#pragma once
#include "core_string.h"
#include "vulkan_api.h"

/**
 * Load a Vulkan instance api by name.
 */
#define rvk_func_load_instance(_INSTANCE_, _API_)                                                  \
  ((PFN_##_API_)rvk_func_load_instance_internal((_INSTANCE_), string_lit(#_API_)))

/**
 * Load a Vulkan device api by name.
 */
#define rvk_func_load_device(_DEVICE_, _API_)                                                      \
  ((PFN_##_API_)rvk_func_load_device_internal((_DEVICE_), string_lit(#_API_)))

/**
 * Call a Vulkan api and check its result.
 */
#define rvk_call(_API_, ...) rvk_check(string_lit(#_API_), _API_(__VA_ARGS__))

void* rvk_func_load_instance_internal(VkInstance, String api);
void* rvk_func_load_device_internal(VkDevice, String api);
void  rvk_check(String api, VkResult);
