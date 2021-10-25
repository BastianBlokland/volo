#include "core_alloc.h"
#include "log_logger.h"

#include "debug_internal.h"

struct sRendVkDebug {
  Logger*                  logger;
  VkInstance               vkInstance;
  VkAllocationCallbacks*   vkAllocHost;
  VkDebugUtilsMessengerEXT vkMessenger;
  RendVkDebugFlags         flags;
};

static int rend_messenger_severity_mask(const RendVkDebugFlags flags) {
  int severity = 0;
  if (flags & RendVkDebugFlags_Verbose) {
    severity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT;
  }
  severity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
  severity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  return severity;
}

static int rend_messenger_type_mask() {
  return VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
         VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
         VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
}

static String rend_msg_type_label(const VkDebugUtilsMessageTypeFlagsEXT msgType) {
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

static LogLevel rend_msg_log_level(const VkDebugUtilsMessageSeverityFlagBitsEXT msgSeverity) {
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

static VkBool32 rend_vk_message_func(
    VkDebugUtilsMessageSeverityFlagBitsEXT      msgSeverity,
    VkDebugUtilsMessageTypeFlagsEXT             msgType,
    const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
    void*                                       userData) {
  RendVkDebug* dbg = userData;

  const LogLevel logLevel  = rend_msg_log_level(msgSeverity);
  const String   typeLabel = rend_msg_type_label(msgType);
  const String   message   = string_from_null_term(callbackData->pMessage);

  log(dbg->logger,
      logLevel,
      "Vulkan {} debug",
      log_param("type", fmt_text(typeLabel)),
      log_param("message", fmt_text(message)));

  return false;
}

static void rend_vk_messenger_create(RendVkDebug* dbg) {
  VkDebugUtilsMessengerCreateInfoEXT createInfo = {
      .sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
      .messageSeverity = rend_messenger_severity_mask(dbg->flags),
      .messageType     = rend_messenger_type_mask(),
      .pfnUserCallback = rend_vk_message_func,
      .pUserData       = dbg,
  };
  rend_vk_func_load_instance(dbg->vkInstance, vkCreateDebugUtilsMessengerEXT)(
      dbg->vkInstance, &createInfo, dbg->vkAllocHost, &dbg->vkMessenger);
}

static void rend_vk_messenger_destroy(RendVkDebug* dbg) {
  rend_vk_func_load_instance(dbg->vkInstance, vkDestroyDebugUtilsMessengerEXT)(
      dbg->vkInstance, dbg->vkMessenger, dbg->vkAllocHost);
}

RendVkDebug* rend_vk_debug_create(
    VkInstance vkInstance, VkAllocationCallbacks* vkAllocHost, const RendVkDebugFlags flags) {

  RendVkDebug* debug = alloc_alloc_t(g_alloc_heap, RendVkDebug);
  *debug             = (RendVkDebug){
      .logger      = g_logger,
      .vkInstance  = vkInstance,
      .vkAllocHost = vkAllocHost,
      .flags       = flags,
  };
  rend_vk_messenger_create(debug);

  log_i("Vulkan debug handler", log_param("verbose", fmt_bool(flags & RendVkDebugFlags_Verbose)));

  return debug;
}

void rend_vk_debug_destroy(RendVkDebug* debug) {
  rend_vk_messenger_destroy(debug);

  log_i("Vulkan debug destroyed");

  alloc_free_t(g_alloc_heap, debug);
}
