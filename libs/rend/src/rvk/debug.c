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
 * - Logs output messages from the validation layers.
 * - Provide debug names for Vulkan objects.
 * - Inserting labels into command buffers.
 *
 * Validation layers can be further configured using the 'vkconfig' utility.
 * Debian package: lunarg-vkconfig
 * On windows its included in the sdk.
 * More info: https://vulkan.lunarg.com/doc/sdk/1.2.198.1/linux/vkconfig.html
 */

struct sRvkDebug {
  RvkDebugFlags            flags;
  RvkLib*                  lib;
  RvkDevice*               dev;
  Logger*                  logger;
  VkDebugUtilsMessengerEXT vkMessenger;
};

static const char* rvk_to_null_term_scratch(const String str) {
  const Mem scratchMem = alloc_alloc(g_allocScratch, str.size + 1, 1);
  mem_cpy(scratchMem, str);
  *mem_at_u8(scratchMem, str.size) = '\0';
  return scratchMem.ptr;
}

static int rvk_messenger_severity_mask(const RvkDebugFlags flags) {
  int severity = 0;
  if (flags & RvkDebugFlags_Verbose) {
    severity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT;
    severity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
  }
  severity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
  severity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  return severity;
}

static int rvk_messenger_type_mask(const RvkDebugFlags flags) {
  int mask = 0;
  mask |= VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
  mask |= VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
  if (flags & RvkDebugFlags_Verbose) {
    mask |= VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  }
  return mask;
}

static String rvk_msg_type_label(const VkDebugUtilsMessageTypeFlagsEXT msgType) {
  if (msgType & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) {
    return string_lit("performance");
  }
  if (msgType & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) {
    return string_lit("validation");
  }
  if (msgType & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT) {
    return string_lit("general");
  }
  return string_lit("unknown");
}

static LogLevel rvk_msg_log_level(const VkDebugUtilsMessageSeverityFlagBitsEXT msgSeverity) {
  if (msgSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
    return LogLevel_Error;
  }
  if (msgSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    return LogLevel_Warn;
  }
  if (msgSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
    return LogLevel_Info;
  }
  return LogLevel_Debug;
}

static VkBool32 SYS_DECL rvk_message_func(
    VkDebugUtilsMessageSeverityFlagBitsEXT      msgSeverity,
    VkDebugUtilsMessageTypeFlagsEXT             msgType,
    const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
    void*                                       userData) {
  RvkDebug* dbg = userData;

  thread_ensure_init();

  const LogLevel logLevel  = rvk_msg_log_level(msgSeverity);
  const String   typeLabel = rvk_msg_type_label(msgType);
  const String   message   = string_from_null_term(callbackData->pMessage);

  log(dbg->logger,
      logLevel,
      "Vulkan {} debug",
      log_param("type", fmt_text(typeLabel)),
      log_param("text", fmt_text(message)));

  if (msgSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
    diag_break(); // Halt when running in a debugger.
  }
  return false;
}

static void rvk_messenger_create(RvkDebug* dbg) {
  const VkDebugUtilsMessengerCreateInfoEXT createInfo = {
      .sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
      .messageSeverity = rvk_messenger_severity_mask(dbg->flags),
      .messageType     = rvk_messenger_type_mask(dbg->flags),
      .pfnUserCallback = rvk_message_func,
      .pUserData       = dbg,
  };
  dbg->lib->api.createDebugUtilsMessengerEXT(
      dbg->lib->vkInst, &createInfo, &dbg->lib->vkAlloc, &dbg->vkMessenger);
}

static void rvk_messenger_destroy(RvkDebug* dbg) {
  dbg->lib->api.destroyDebugUtilsMessengerEXT(
      dbg->lib->vkInst, dbg->vkMessenger, &dbg->lib->vkAlloc);
}

RvkDebug* rvk_debug_create(RvkLib* lib, RvkDevice* dev, const RvkDebugFlags flags) {
  RvkDebug* debug = alloc_alloc_t(g_allocHeap, RvkDebug);

  *debug = (RvkDebug){
      .flags  = flags,
      .lib    = lib,
      .dev    = dev,
      .logger = g_logger,
  };

  rvk_messenger_create(debug);

  return debug;
}

void rvk_debug_destroy(RvkDebug* debug) {
  rvk_messenger_destroy(debug);
  alloc_free_t(g_allocHeap, debug);
}

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
    debug->lib->api.cmdBeginDebugUtilsLabelEXT(vkCmdBuffer, &label);
  }
}

void rvk_debug_label_end(RvkDebug* debug, VkCommandBuffer vkCmdBuffer) {
  if (debug) {
    debug->lib->api.cmdEndDebugUtilsLabelEXT(vkCmdBuffer);
  }
}
