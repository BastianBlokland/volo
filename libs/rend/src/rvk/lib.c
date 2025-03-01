#include "core_array.h"
#include "core_diag.h"
#include "core_dynlib.h"
#include "core_path.h"
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
  }

  lib->vkInst = rvk_inst_create(&loaderApi, &lib->vkAlloc, lib->flags);
  rvk_api_check(string_lit("loadInstance"), vkLoadInstance(lib->vkInst, &loaderApi, &lib->api));

  log_i(
      "Vulkan library created",
      log_param("validation", fmt_bool(lib->flags & RvkLibFlags_Validation)),
      log_param("debug", fmt_bool(lib->flags & RvkLibFlags_Debug)));

  return lib;
}

void rvk_lib_destroy(RvkLib* lib) {

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
