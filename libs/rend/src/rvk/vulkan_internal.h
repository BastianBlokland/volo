#pragma once
#include "core_string.h"
#include "vulkan_api.h"

/**
 * Call a Vulkan api and check its result.
 */
#define rvk_call(_API_, _FUNC_, ...) rvk_check(string_lit(#_FUNC_), (_API_)._FUNC_(__VA_ARGS__))

void rvk_check(String func, VkResult);
