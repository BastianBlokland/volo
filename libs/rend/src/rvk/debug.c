#include "core_alloc.h"
#include "core_diag.h"
#include "core_thread.h"
#include "geo_color.h"
#include "log_logger.h"

#include "debug_internal.h"
#include "device_internal.h"
#include "lib_internal.h"

/**
 * Vulkan validation layer support.
 * - Provide debug names for Vulkan objects.
 * - Inserting labels into command buffers.
 *
 * Validation layers can be further configured using the 'vkconfig' utility.
 * Debian package: lunarg-vkconfig
 * On windows its included in the sdk.
 * More info: https://vulkan.lunarg.com/doc/sdk/1.2.198.1/linux/vkconfig.html
 */

struct sRvkDebug {
  RvkLib*    lib;
  RvkDevice* dev;
};

static const char* rvk_to_null_term_scratch(const String str) {
  const Mem scratchMem = alloc_alloc(g_allocScratch, str.size + 1, 1);
  mem_cpy(scratchMem, str);
  *mem_at_u8(scratchMem, str.size) = '\0';
  return scratchMem.ptr;
}

RvkDebug* rvk_debug_create(RvkLib* lib, RvkDevice* dev) {
  RvkDebug* debug = alloc_alloc_t(g_allocHeap, RvkDebug);

  *debug = (RvkDebug){
      .lib = lib,
      .dev = dev,
  };

  return debug;
}

void rvk_debug_destroy(RvkDebug* debug) { alloc_free_t(g_allocHeap, debug); }

void rvk_debug_name(
    RvkDebug* debug, const VkObjectType vkType, const u64 vkHandle, const String name) {
  if (debug) {
    const VkDebugUtilsObjectNameInfoEXT nameInfo = {
        .sType        = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        .objectType   = vkType,
        .objectHandle = vkHandle,
        .pObjectName  = rvk_to_null_term_scratch(name),
    };
    rvk_call_checked(debug->lib, setDebugUtilsObjectNameEXT, debug->dev->vkDev, &nameInfo);
  }
}

void rvk_debug_label_begin_raw(
    RvkDebug* debug, VkCommandBuffer vkCmdBuffer, const GeoColor color, const String name) {
  if (debug) {
    const VkDebugUtilsLabelEXT label = {
        .sType      = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        .pLabelName = rvk_to_null_term_scratch(name),
        .color      = {color.r, color.g, color.b, color.a},
    };
    rvk_call(debug->lib, cmdBeginDebugUtilsLabelEXT, vkCmdBuffer, &label);
  }
}

void rvk_debug_label_end(RvkDebug* debug, VkCommandBuffer vkCmdBuffer) {
  if (debug) {
    rvk_call(debug->lib, cmdEndDebugUtilsLabelEXT, vkCmdBuffer);
  }
}
