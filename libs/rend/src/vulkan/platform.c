#include "core_array.h"
#include "core_path.h"
#include "gap_native.h"
#include "log_logger.h"

#include "alloc_host_internal.h"
#include "debug_internal.h"
#include "device_internal.h"
#include "platform_internal.h"
#include "vulkan_internal.h"

typedef enum {
  RendVkPlatformFlags_None       = 0,
  RendVkPlatformFlags_Validation = 1 << 0,
} RendVkPlatformFlags;

struct sRendVkPlatform {
  VkAllocationCallbacks vkAllocHost;
  VkInstance            vkInstance;
  RendVkPlatformFlags   flags;
  RendVkDebug*          debug;
  RendVkDevice*         device;
};

#define rend_debug_verbose false

static const RendVkDebugFlags g_debugFlags      = rend_debug_verbose ? RendVkDebugFlags_Verbose : 0;
static const String           g_validationLayer = string_static("VK_LAYER_KHRONOS_validation");
static const String           g_validationExt   = string_static("VK_EXT_debug_utils");

static VkApplicationInfo rend_vk_app_info() {
  return (VkApplicationInfo){
      .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName   = path_stem(g_path_executable).ptr,
      .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
      .pEngineName        = "volo",
      .engineVersion      = VK_MAKE_VERSION(0, 1, 0),
      .apiVersion         = VK_API_VERSION_1_1,
  };
}

static bool rend_vk_layer_supported(const String layer) {
  VkLayerProperties availableLayers[32];
  u32               availableLayerCount = array_elems(availableLayers);
  rend_vk_call(vkEnumerateInstanceLayerProperties, &availableLayerCount, availableLayers);

  for (u32 i = 0; i != availableLayerCount; ++i) {
    if (string_eq(layer, string_from_null_term(availableLayers[i].layerName))) {
      return true;
    }
  }
  return false;
}

static u32 rend_vk_required_layers(const char** output, const RendVkPlatformFlags flags) {
  u32 i = 0;
  if (flags & RendVkPlatformFlags_Validation) {
    output[i++] = g_validationLayer.ptr;
  }
  return i;
}

static u32 rend_vk_required_extensions(const char** output, const RendVkPlatformFlags flags) {
  u32 i       = 0;
  output[i++] = VK_KHR_SURFACE_EXTENSION_NAME;
  switch (gap_native_wm()) {
  case GapNativeWm_Xcb:
    output[i++] = "VK_KHR_xcb_surface";
    break;
  case GapNativeWm_Win32:
    output[i++] = "VK_KHR_win32_surface";
    break;
  }
  if (flags & RendVkPlatformFlags_Validation) {
    output[i++] = g_validationExt.ptr;
  }
  return i;
}

static void rend_vk_instance_create(RendVkPlatform* ctx) {
  VkApplicationInfo appInfo = rend_vk_app_info();

  const char* layerNames[16];
  const u32   layerCount = rend_vk_required_layers(layerNames, ctx->flags);

  const char* extensionNames[16];
  const u32   extensionCount = rend_vk_required_extensions(extensionNames, ctx->flags);

  VkInstanceCreateInfo createInfo = {
      .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo        = &appInfo,
      .enabledExtensionCount   = extensionCount,
      .ppEnabledExtensionNames = extensionNames,
      .enabledLayerCount       = layerCount,
      .ppEnabledLayerNames     = layerNames,
  };

  rend_vk_call(vkCreateInstance, &createInfo, &ctx->vkAllocHost, &ctx->vkInstance);
}

static void rend_vk_instance_destroy(RendVkPlatform* ctx) {
  vkDestroyInstance(ctx->vkInstance, &ctx->vkAllocHost);
}

RendVkPlatform* rend_vk_platform_create() {
  RendVkPlatform* ctx = alloc_alloc_t(g_alloc_heap, RendVkPlatform);
  *ctx                = (RendVkPlatform){
      .vkAllocHost = rend_vk_alloc_host_create(g_alloc_heap),
  };

  const bool validation = rend_vk_layer_supported(g_validationLayer);
  if (validation) {
    ctx->flags |= RendVkPlatformFlags_Validation;
  }
  rend_vk_instance_create(ctx);

  if (validation) {
    ctx->debug = rend_vk_debug_create(ctx->vkInstance, &ctx->vkAllocHost, g_debugFlags);
  }
  ctx->device = rend_vk_device_create(ctx->vkInstance, &ctx->vkAllocHost, ctx->debug);

  log_i("Vulkan platform created", log_param("validation", fmt_bool(validation)));
  return ctx;
}

void rend_vk_platform_destroy(RendVkPlatform* ctx) {
  rend_vk_device_destroy(ctx->device);
  if (ctx->debug) {
    rend_vk_debug_destroy(ctx->debug);
  }
  rend_vk_instance_destroy(ctx);

  log_i("Vulkan platform destroyed");

  alloc_free_t(g_alloc_heap, ctx);
}
