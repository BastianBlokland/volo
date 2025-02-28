#include "core_alloc.h"
#include "core_diag.h"
#include "log_logger.h"

#include "vulkan_internal.h"

static const char* rvk_to_null_term_scratch(const String str) {
  const Mem scratchMem = alloc_alloc(g_allocScratch, str.size + 1, 1);
  mem_cpy(scratchMem, str);
  *mem_at_u8(scratchMem, str.size) = '\0';
  return scratchMem.ptr;
}

void* rvk_func_load_instance_internal(VkInstance inst, const String api) {
  const PFN_vkVoidFunction res = vkGetInstanceProcAddr(inst, rvk_to_null_term_scratch(api));
  if (UNLIKELY(!res)) {
    diag_crash_msg("Vulkan failed to load instance api: {}", fmt_text(api));
  }
  return (void*)res;
}

void* rvk_func_load_device_internal(VkDevice inst, const String api) {
  const PFN_vkVoidFunction res = vkGetDeviceProcAddr(inst, rvk_to_null_term_scratch(api));
  if (UNLIKELY(!res)) {
    diag_crash_msg("Vulkan failed to load device api: {}", fmt_text(api));
  }
  return (void*)res;
}

void rvk_check(const String func, const VkResult result) {
  if (LIKELY(result == VK_SUCCESS)) {
    return;
  }
  if (result == VK_INCOMPLETE) {
    log_w("Vulkan {}: Result incomplete", log_param("func", fmt_text(func)));
    return;
  }
  diag_crash_msg(
      "Vulkan {}: [{}] {}", fmt_text(func), fmt_int(result), fmt_text(vkResultStr(result)));
}
