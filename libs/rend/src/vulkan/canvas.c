#include "core_alloc.h"
#include "gap_native.h"
#include "log_logger.h"

#include "canvas_internal.h"

#if defined(VOLO_LINUX)
#include <vulkan/vulkan_xcb.h>
#include <xcb/xcb.h>
#elif defined(VOLO_WIN32)
#include <Windows.h>
#include <vulkan/vulkan_win32.h>
#else
ASSERT(false, "Unsupported platform");
#endif

struct sRendVkCanvas {
  RendVkDevice* device;
  VkSurfaceKHR  vkSurface;
};

static VkSurfaceKHR rend_vk_surface_create(RendVkDevice* device, const GapWindowComp* window) {
  VkSurfaceKHR result;
#if defined(VOLO_LINUX)
  VkXcbSurfaceCreateInfoKHR createInfo = {
      .sType      = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
      .connection = (xcb_connection_t*)gap_native_app_handle(window),
      .window     = (xcb_window_t)gap_native_window_handle(window),
  };
  rend_vk_call(vkCreateXcbSurfaceKHR, vkInstance, &createInfo, vkAllocHost, &result);
#elif defined(VOLO_WIN32)
  VkWin32SurfaceCreateInfoKHR createInfo = {
      .sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
      .hinstance = (HINSTANCE)gap_native_app_handle(window),
      .hwnd      = (HWND)gap_native_window_handle(window),
  };
  rend_vk_call(
      vkCreateWin32SurfaceKHR, device->vkInstance, &createInfo, device->vkAllocHost, &result);
#endif
  return result;
}

RendVkCanvas* rend_vk_canvas_create(RendVkDevice* device, const GapWindowComp* window) {
  const GapVector size = gap_window_param(window, GapParam_WindowSize);

  RendVkCanvas* platform = alloc_alloc_t(g_alloc_heap, RendVkCanvas);
  *platform              = (RendVkCanvas){
      .device    = device,
      .vkSurface = rend_vk_surface_create(device, window),
  };

  log_i("Vulkan canvas created", log_param("size", gap_vector_fmt(size)));
  return platform;
}

void rend_vk_canvas_destroy(RendVkCanvas* canvas) {
  // Wait for device activity be done before destroying the surface.
  rend_vk_call(vkDeviceWaitIdle, canvas->device->vkDevice);

  vkDestroySurfaceKHR(canvas->device->vkInstance, canvas->vkSurface, canvas->device->vkAllocHost);

  log_i("Vulkan canvas destroyed");
  alloc_free_t(g_alloc_heap, canvas);
}

void rend_vk_canvas_resize(RendVkCanvas* canvas, const GapVector size) {
  (void)canvas;
  (void)size;

  log_i("Vulkan canvas resized", log_param("size", gap_vector_fmt(size)));
}
