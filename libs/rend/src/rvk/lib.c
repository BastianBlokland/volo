#include "core_array.h"
#include "core_diag.h"
#include "core_dynlib.h"
#include "core_path.h"
#include "core_thread.h"
#include "gap_native.h"
#include "log_logger.h"

#include "lib_internal.h"
#include "mem_internal.h"

#define rvk_lib_vulkan_names_max 4

static const VkValidationFeatureEnableEXT g_validationEnabledFeatures[] = {
    VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT,
};

static VkApplicationInfo rvk_inst_app_info(void) {
  return (VkApplicationInfo){
      .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName   = path_stem(g_pathExecutable).ptr,
      .applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
      .pEngineName        = "volo",
      .engineVersion      = VK_MAKE_API_VERSION(0, 0, 1, 0),
      .apiVersion         = VK_MAKE_API_VERSION(0, 1, 1, 0),
  };
}

static bool rvk_inst_layer_supported(VkInterfaceLoader* loaderApi, const char* layer) {
  VkLayerProperties layers[64];
  u32               layerCount = array_elems(layers);
  rvk_api_check(
      string_lit("enumerateInstanceLayerProperties"),
      loaderApi->enumerateInstanceLayerProperties(&layerCount, layers));

  for (u32 i = 0; i != layerCount; ++i) {
    if (string_eq(string_from_null_term(layers[i].layerName), string_from_null_term(layer))) {
      return true;
    }
  }
  return false;
}

static bool rvk_inst_extension_supported(VkInterfaceLoader* loaderApi, const char* ext) {
  VkExtensionProperties exts[128];
  u32                   extCount = array_elems(exts);
  rvk_api_check(
      string_lit("enumerateInstanceExtensionProperties"),
      loaderApi->enumerateInstanceExtensionProperties(null /* layerName */, &extCount, exts));

  for (u32 i = 0; i != extCount; ++i) {
    if (string_eq(string_from_null_term(exts[i].extensionName), string_from_null_term(ext))) {
      return true;
    }
  }
  return false;
}

static VkInstance rvk_inst_create(
    VkInterfaceLoader* loaderApi, VkAllocationCallbacks* vkAlloc, const RvkLibFlags flags) {
  const VkApplicationInfo appInfo = rvk_inst_app_info();

  const char* layerNames[16];
  u32         layerCount = 0;
  if (flags & RvkLibFlags_Validation) {
    layerNames[layerCount++] = "VK_LAYER_KHRONOS_validation";
  }

  const char* extensionNames[16];
  u32         extensionCount       = 0;
  extensionNames[extensionCount++] = "VK_KHR_surface";
  switch (gap_native_wm()) {
  case GapNativeWm_Xcb:
    extensionNames[extensionCount++] = "VK_KHR_xcb_surface";
    break;
  case GapNativeWm_Win32:
    extensionNames[extensionCount++] = "VK_KHR_win32_surface";
    break;
  }
  if (flags & RvkLibFlags_Debug) {
    extensionNames[extensionCount++] = "VK_EXT_debug_utils";
  }

  VkInstanceCreateInfo createInfo = {
      .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo        = &appInfo,
      .enabledExtensionCount   = extensionCount,
      .ppEnabledExtensionNames = extensionNames,
      .enabledLayerCount       = layerCount,
      .ppEnabledLayerNames     = layerNames,
  };

  VkValidationFeaturesEXT validationFeatures;
  if (flags & RvkLibFlags_Validation) {
    validationFeatures = (VkValidationFeaturesEXT){
        .sType                         = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
        .pEnabledValidationFeatures    = g_validationEnabledFeatures,
        .enabledValidationFeatureCount = array_elems(g_validationEnabledFeatures),
    };
    createInfo.pNext = &validationFeatures;
  }

  VkInstance result;
  rvk_api_check(
      string_lit("createInstance"), loaderApi->createInstance(&createInfo, vkAlloc, &result));
  return result;
}

static int rvk_messenger_severity_mask(const RvkLibFlags flags) {
  int severity = 0;
  if (flags & RvkLibFlags_DebugVerbose) {
    severity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT;
    severity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
  }
  severity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
  severity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  return severity;
}

static int rvk_messenger_type_mask(const RvkLibFlags flags) {
  int mask = 0;
  mask |= VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
  mask |= VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
  if (flags & RvkLibFlags_DebugVerbose) {
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
  (void)userData;

  thread_ensure_init();

  const LogLevel logLevel  = rvk_msg_log_level(msgSeverity);
  const String   typeLabel = rvk_msg_type_label(msgType);
  const String   message   = string_from_null_term(callbackData->pMessage);

  log(g_logger,
      logLevel,
      "Vulkan {} message",
      log_param("type", fmt_text(typeLabel)),
      log_param("text", fmt_text(message)));

  if (msgSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
    diag_break(); // Halt when running in a debugger.
  }
  return false;
}

static void rvk_messenger_create(RvkLib* lib) {
  const VkDebugUtilsMessengerCreateInfoEXT info = {
      .sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
      .messageSeverity = rvk_messenger_severity_mask(lib->flags),
      .messageType     = rvk_messenger_type_mask(lib->flags),
      .pfnUserCallback = rvk_message_func,
      .pUserData       = null,
  };
  rvk_call(lib, createDebugUtilsMessengerEXT, lib->vkInst, &info, &lib->vkAlloc, &lib->vkMessenger);
}

static void rvk_messenger_destroy(RvkLib* lib) {
  rvk_call(lib, destroyDebugUtilsMessengerEXT, lib->vkInst, lib->vkMessenger, &lib->vkAlloc);
  lib->vkMessenger = null;
}

static u32 rvk_lib_names(String outPaths[PARAM_ARRAY_SIZE(rvk_lib_vulkan_names_max)]) {
  u32 count = 0;
#ifdef VOLO_WIN32
  outPaths[count++] = string_lit("vulkan-1.dll");
#elif VOLO_LINUX
  outPaths[count++] = string_lit("libvulkan.so.1");
  outPaths[count++] = string_lit("libvulkan.so");
#endif
  return count;
}

RvkLib* rvk_lib_create(const RendSettingsGlobalComp* set) {
  (void)set;

  RvkLib* lib = alloc_alloc_t(g_allocHeap, RvkLib);

  *lib = (RvkLib){
      .vkAlloc = rvk_mem_allocator(g_allocHeap),
  };

  String    libNames[rvk_lib_vulkan_names_max];
  const u32 libNameCount = rvk_lib_names(libNames);

  DynLibResult loadRes = dynlib_load_first(g_allocHeap, libNames, libNameCount, &lib->vulkanLib);
  if (loadRes != DynLibResult_Success) {
    const String err = dynlib_result_str(loadRes);
    diag_crash_msg("Failed to load Vulkan library: {}", fmt_text(err));
  }
  VkInterfaceLoader loaderApi;
  rvk_api_check(string_lit("loadLoader"), vkLoadLoader(lib->vulkanLib, &loaderApi));

  const bool validationDesired = (set->flags & RendGlobalFlags_Validation) != 0;
  if (validationDesired && rvk_inst_layer_supported(&loaderApi, "VK_LAYER_KHRONOS_validation")) {
    lib->flags |= RvkLibFlags_Validation;
  }
  const bool debugDesired = validationDesired || (set->flags & RendGlobalFlags_DebugGpu) != 0;
  if (debugDesired && rvk_inst_extension_supported(&loaderApi, "VK_EXT_debug_utils")) {
    lib->flags |= RvkLibFlags_Debug;
    if (set->flags & RendGlobalFlags_Verbose) {
      lib->flags |= RvkLibFlags_DebugVerbose;
    }
  }

  lib->vkInst = rvk_inst_create(&loaderApi, &lib->vkAlloc, lib->flags);
  rvk_api_check(string_lit("loadInstance"), vkLoadInstance(lib->vkInst, &loaderApi, &lib->api));

  if (lib->flags & RvkLibFlags_Debug) {
    rvk_messenger_create(lib);
  }

  log_i(
      "Vulkan library created",
      log_param("validation", fmt_bool(lib->flags & RvkLibFlags_Validation)),
      log_param("debug", fmt_bool(lib->flags & RvkLibFlags_Debug)));

  return lib;
}

void rvk_lib_destroy(RvkLib* lib) {
  if (lib->vkMessenger) {
    rvk_messenger_destroy(lib);
  }
  rvk_call(lib, destroyInstance, lib->vkInst, &lib->vkAlloc);
  dynlib_destroy(lib->vulkanLib);
  alloc_free_t(g_allocHeap, lib);

  log_d("Vulkan library destroyed");
}

void rvk_api_check(const String func, const VkResult result) {
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
