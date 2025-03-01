#include "core_alloc.h"
#include "geo_color.h"

#include "debug_internal.h"
#include "device_internal.h"
#include "lib_internal.h"

static const char* rvk_to_null_term_scratch(const String str) {
  const Mem scratchMem = alloc_alloc(g_allocScratch, str.size + 1, 1);
  mem_cpy(scratchMem, str);
  *mem_at_u8(scratchMem, str.size) = '\0';
  return scratchMem.ptr;
}

void rvk_debug_name(
    RvkDevice* dev, const VkObjectType vkType, const u64 vkHandle, const String name) {
  if (dev->lib->flags & RvkLibFlags_Debug) {
    const VkDebugUtilsObjectNameInfoEXT nameInfo = {
        .sType        = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        .objectType   = vkType,
        .objectHandle = vkHandle,
        .pObjectName  = rvk_to_null_term_scratch(name),
    };
    rvk_call_checked(dev->lib, setDebugUtilsObjectNameEXT, dev->vkDev, &nameInfo);
  }
}

void rvk_debug_label_begin_raw(
    RvkDevice* dev, VkCommandBuffer vkCmdBuffer, const GeoColor color, const String name) {
  if (dev->lib->flags & RvkLibFlags_Debug) {
    const VkDebugUtilsLabelEXT label = {
        .sType      = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        .pLabelName = rvk_to_null_term_scratch(name),
        .color      = {color.r, color.g, color.b, color.a},
    };
    rvk_call(dev->lib, cmdBeginDebugUtilsLabelEXT, vkCmdBuffer, &label);
  }
}

void rvk_debug_label_end(RvkDevice* dev, VkCommandBuffer vkCmdBuffer) {
  if (dev->lib->flags & RvkLibFlags_Debug) {
    rvk_call(dev->lib, cmdEndDebugUtilsLabelEXT, vkCmdBuffer);
  }
}
