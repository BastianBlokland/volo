#include "core_alloc.h"
#include "core_diag.h"
#include "log_logger.h"

#include "vulkan_internal.h"

static const char* rend_to_null_term_scratch(String api) {
  const Mem scratchMem = alloc_alloc(g_alloc_scratch, api.size + 1, 1);
  mem_cpy(scratchMem, api);
  *mem_at_u8(scratchMem, api.size) = '\0';
  return scratchMem.ptr;
}

void* rend_vk_func_load_instance_internal(VkInstance instance, String api) {
  const PFN_vkVoidFunction res = vkGetInstanceProcAddr(instance, rend_to_null_term_scratch(api));
  if (UNLIKELY(!res)) {
    diag_crash_msg("Vulkan failed to load instance api: {}", fmt_text(api));
  }
  return res;
}

void* rend_vk_func_load_device_internal(VkDevice device, String api) {
  const PFN_vkVoidFunction res = vkGetDeviceProcAddr(device, rend_to_null_term_scratch(api));
  if (UNLIKELY(!res)) {
    diag_crash_msg("Vulkan failed to load device api: {}", fmt_text(api));
  }
  return res;
}

void rend_vk_check(const String api, const VkResult result) {
  if (LIKELY(result == VK_SUCCESS)) {
    return;
  }
  if (result == VK_INCOMPLETE) {
    log_w("Vulkan {}: Result incomplete", log_param("api", fmt_text(api)));
    return;
  }
  diag_crash_msg(
      "Vulkan {}: [{}] {}", fmt_text(api), fmt_int(result), fmt_text(rend_vk_result_str(result)));
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

String rend_vk_devicetype_str(const VkPhysicalDeviceType type) {
  switch (type) {
  case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
    return string_lit("integrated");
  case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
    return string_lit("discrete");
  case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
    return string_lit("virtual");
  case VK_PHYSICAL_DEVICE_TYPE_CPU:
    return string_lit("cpu");
  default:
    return string_lit("other");
  }
}

String rend_vk_vendor_str(const uint32_t vendorId) {
  switch (vendorId) {
  case 0x1002:
    return string_lit("AMD");
  case 0x1010:
    return string_lit("ImgTec");
  case 0x10DE:
    return string_lit("NVIDIA");
  case 0x13B5:
    return string_lit("ARM");
  case 0x5143:
    return string_lit("Qualcomm");
  case 0x8086:
    return string_lit("INTEL");
  default:
    return string_lit("other");
  }
}
