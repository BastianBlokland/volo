#include "core_diag.h"

#include "vulkan_internal.h"

void rend_vk_check(const String api, const VkResult result) {
  if (UNLIKELY(result)) {
    diag_crash_msg(
        "Vulkan {}: [{}] {}", fmt_text(api), fmt_int(result), fmt_text(rend_vk_result_str(result)));
  }
}

String rend_vk_result_str(const VkResult result) {
#define RET_STR(NAME)                                                                              \
  case VK_##NAME:                                                                                  \
    return string_lit(#NAME)

  switch (result) {
    RET_STR(SUCCESS);
    RET_STR(NOT_READY);
    RET_STR(TIMEOUT);
    RET_STR(EVENT_SET);
    RET_STR(EVENT_RESET);
    RET_STR(INCOMPLETE);
    RET_STR(ERROR_OUT_OF_HOST_MEMORY);
    RET_STR(ERROR_OUT_OF_DEVICE_MEMORY);
    RET_STR(ERROR_INITIALIZATION_FAILED);
    RET_STR(ERROR_DEVICE_LOST);
    RET_STR(ERROR_MEMORY_MAP_FAILED);
    RET_STR(ERROR_LAYER_NOT_PRESENT);
    RET_STR(ERROR_EXTENSION_NOT_PRESENT);
    RET_STR(ERROR_FEATURE_NOT_PRESENT);
    RET_STR(ERROR_INCOMPATIBLE_DRIVER);
    RET_STR(ERROR_TOO_MANY_OBJECTS);
    RET_STR(ERROR_FORMAT_NOT_SUPPORTED);
    RET_STR(ERROR_FRAGMENTED_POOL);
    RET_STR(ERROR_UNKNOWN);
    RET_STR(ERROR_OUT_OF_POOL_MEMORY);
    RET_STR(ERROR_INVALID_EXTERNAL_HANDLE);
    RET_STR(ERROR_FRAGMENTATION);
    RET_STR(ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS);
    RET_STR(ERROR_SURFACE_LOST_KHR);
    RET_STR(ERROR_NATIVE_WINDOW_IN_USE_KHR);
    RET_STR(SUBOPTIMAL_KHR);
    RET_STR(ERROR_OUT_OF_DATE_KHR);
    RET_STR(ERROR_INCOMPATIBLE_DISPLAY_KHR);
    RET_STR(ERROR_VALIDATION_FAILED_EXT);
    RET_STR(ERROR_INVALID_SHADER_NV);
    RET_STR(ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT);
    RET_STR(ERROR_NOT_PERMITTED_EXT);
    RET_STR(ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT);
    RET_STR(THREAD_IDLE_KHR);
    RET_STR(THREAD_DONE_KHR);
    RET_STR(OPERATION_DEFERRED_KHR);
    RET_STR(OPERATION_NOT_DEFERRED_KHR);
    RET_STR(PIPELINE_COMPILE_REQUIRED_EXT);
  default:
    return string_lit("UNKNOWN");
  }
#undef ERROR_STR
}
