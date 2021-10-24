#pragma once
#include "core_string.h"

#include <vulkan/vulkan.h>

#define rend_vk_call(_API_, ...) rend_vk_check(string_lit(#_API_), _API_(__VA_ARGS__))

void   rend_vk_check(String api, VkResult);
String rend_vk_result_str(VkResult);
