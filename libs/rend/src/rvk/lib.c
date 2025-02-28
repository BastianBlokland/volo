#include "core_array.h"
#include "core_path.h"
#include "gap_native.h"
#include "log_logger.h"

#include "lib_internal.h"
#include "mem_internal.h"

static const VkValidationFeatureEnableEXT g_validationEnabledFeatures[] = {
    VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT,
};

static VkApplicationInfo rvk_instance_app_info(void) {
  return (VkApplicationInfo){
      .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName   = path_stem(g_pathExecutable).ptr,
      .applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
      .pEngineName        = "volo",
      .engineVersion      = VK_MAKE_API_VERSION(0, 0, 1, 0),
      .apiVersion         = VK_MAKE_API_VERSION(0, 1, 1, 0),
  };
}

static bool rvk_instance_layer_supported(const char* layer) {
  VkLayerProperties layers[64];
  u32               layerCount = array_elems(layers);
  rvk_call(vkEnumerateInstanceLayerProperties, &layerCount, layers);

  for (u32 i = 0; i != layerCount; ++i) {
    if (string_eq(string_from_null_term(layers[i].layerName), string_from_null_term(layer))) {
      return true;
    }
  }
  return false;
}

static bool rvk_instance_extension_supported(const char* ext) {
  VkExtensionProperties exts[128];
  u32                   extCount = array_elems(exts);
  rvk_call(vkEnumerateInstanceExtensionProperties, null /* layerName */, &extCount, exts);

  for (u32 i = 0; i != extCount; ++i) {
    if (string_eq(string_from_null_term(exts[i].extensionName), string_from_null_term(ext))) {
      return true;
    }
  }
  return false;
}

static VkInstance rvk_instance_create(VkAllocationCallbacks* vkAlloc, const RvkLibFlags flags) {
  const VkApplicationInfo appInfo = rvk_instance_app_info();

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
  rvk_call(vkCreateInstance, &createInfo, vkAlloc, &result);
  return result;
}

RvkLib* rvk_lib_create(const RendSettingsGlobalComp* set) {
  (void)set;

  RvkLib* lib = alloc_alloc_t(g_allocHeap, RvkLib);

  *lib = (RvkLib){
      .vkAlloc = rvk_mem_allocator(g_allocHeap),
  };

  const bool validationDesired = (set->flags & RendGlobalFlags_Validation) != 0;
  if (validationDesired && rvk_instance_layer_supported("VK_LAYER_KHRONOS_validation")) {
    lib->flags |= RvkLibFlags_Validation;
  }
  const bool debugDesired = validationDesired || (set->flags & RendGlobalFlags_DebugGpu) != 0;
  if (debugDesired && rvk_instance_extension_supported("VK_EXT_debug_utils")) {
    lib->flags |= RvkLibFlags_Debug;
  }

  lib->vkInst = rvk_instance_create(&lib->vkAlloc, lib->flags);

  log_i(
      "Vulkan library created",
      log_param("validation", fmt_bool(lib->flags & RvkLibFlags_Validation)),
      log_param("debug", fmt_bool(lib->flags & RvkLibFlags_Debug)));

  return lib;
}

void rvk_lib_destroy(RvkLib* lib) {

  vkDestroyInstance(lib->vkInst, &lib->vkAlloc);
  alloc_free_t(g_allocHeap, lib);

  log_d("Vulkan library destroyed");
}
