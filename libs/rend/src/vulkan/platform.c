#include "core_array.h"
#include "core_diag.h"
#include "core_path.h"
#include "core_thread.h"
#include "gap_native.h"
#include "log_logger.h"

#include "alloc_host_internal.h"
#include "canvas_internal.h"
#include "debug_internal.h"
#include "device_internal.h"
#include "platform_internal.h"
#include "vulkan_internal.h"

typedef enum {
  RendVkPlatformFlags_None       = 0,
  RendVkPlatformFlags_Validation = 1 << 0,
} RendVkPlatformFlags;

typedef struct {
  RendVkCanvasId id;
  RendVkCanvas*  canvas;
} RendVkCanvasInfo;

struct sRendVkPlatform {
  VkAllocationCallbacks vkAllocHost;
  VkInstance            vkInstance;
  RendVkPlatformFlags   flags;
  RendVkDebug*          debug;
  RendVkDevice*         device;
  DynArray              canvases; // RendVkCanvasInfo[]
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

static void rend_vk_instance_create(RendVkPlatform* plat) {
  VkApplicationInfo appInfo = rend_vk_app_info();

  const char* layerNames[16];
  const u32   layerCount = rend_vk_required_layers(layerNames, plat->flags);

  const char* extensionNames[16];
  const u32   extensionCount = rend_vk_required_extensions(extensionNames, plat->flags);

  VkInstanceCreateInfo createInfo = {
      .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo        = &appInfo,
      .enabledExtensionCount   = extensionCount,
      .ppEnabledExtensionNames = extensionNames,
      .enabledLayerCount       = layerCount,
      .ppEnabledLayerNames     = layerNames,
  };

  rend_vk_call(vkCreateInstance, &createInfo, &plat->vkAllocHost, &plat->vkInstance);
}

static void rend_vk_instance_destroy(RendVkPlatform* plat) {
  vkDestroyInstance(plat->vkInstance, &plat->vkAllocHost);
}

static RendVkCanvas* rend_vk_canvas_lookup(RendVkPlatform* plat, const RendVkCanvasId id) {
  dynarray_for_t(&plat->canvases, RendVkCanvasInfo, info, {
    if (info->id == id) {
      return info->canvas;
    }
  });
  diag_crash_msg("No canvas found with id: {}", fmt_int(id));
}

RendVkPlatform* rend_vk_platform_create() {
  RendVkPlatform* plat = alloc_alloc_t(g_alloc_heap, RendVkPlatform);
  *plat                = (RendVkPlatform){
      .vkAllocHost = rend_vk_alloc_host_create(g_alloc_heap),
      .canvases    = dynarray_create_t(g_alloc_heap, RendVkCanvasInfo, 4),
  };

  const bool validation = rend_vk_layer_supported(g_validationLayer);
  if (validation) {
    plat->flags |= RendVkPlatformFlags_Validation;
  }
  rend_vk_instance_create(plat);

  if (validation) {
    plat->debug = rend_vk_debug_create(plat->vkInstance, &plat->vkAllocHost, g_debugFlags);
  }
  plat->device = rend_vk_device_create(plat->vkInstance, &plat->vkAllocHost, plat->debug);

  log_i("Vulkan platform created", log_param("validation", fmt_bool(validation)));
  return plat;
}

void rend_vk_platform_destroy(RendVkPlatform* plat) {

  while (plat->canvases.size) {
    rend_vk_platform_canvas_destroy(plat, dynarray_at_t(&plat->canvases, 0, RendVkCanvasInfo)->id);
  }

  rend_vk_device_destroy(plat->device);
  if (plat->debug) {
    rend_vk_debug_destroy(plat->debug);
  }
  rend_vk_instance_destroy(plat);

  log_i("Vulkan platform destroyed");

  dynarray_destroy(&plat->canvases);
  alloc_free_t(g_alloc_heap, plat);
}

RendVkCanvasId rend_vk_platform_canvas_create(RendVkPlatform* plat, const GapWindowComp* window) {
  static i64 nextCanvasId = 0;

  RendVkCanvasId id = (RendVkCanvasId)thread_atomic_add_i64(&nextCanvasId, 1);
  *dynarray_push_t(&plat->canvases, RendVkCanvasInfo) = (RendVkCanvasInfo){
      .id     = id,
      .canvas = rend_vk_canvas_create(plat->device, window),
  };
  return id;
}

void rend_vk_platform_canvas_destroy(RendVkPlatform* plat, const RendVkCanvasId id) {
  for (usize i = 0; i != plat->canvases.size; ++i) {
    RendVkCanvasInfo* canvasInfo = dynarray_at_t(&plat->canvases, i, RendVkCanvasInfo);
    if (canvasInfo->id == id) {
      rend_vk_canvas_destroy(canvasInfo->canvas);
      dynarray_remove_unordered(&plat->canvases, i, 1);
      break;
    }
  }
}

void rend_vk_platform_canvas_resize(
    RendVkPlatform* plat, const RendVkCanvasId id, const GapVector size) {
  rend_vk_canvas_resize(rend_vk_canvas_lookup(plat, id), size);
}
