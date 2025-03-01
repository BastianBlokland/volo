#include "core_diag.h"
#include "log_logger.h"

#include "vulkan_internal.h"

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
