#include "core_alloc.h"
#include "log_logger.h"

#include "canvas_internal.h"

struct sRendVkCanvas {
  u32 dummy;
};

RendVkCanvas* rend_vk_canvas_create(const GapVector size) {
  RendVkCanvas* platform = alloc_alloc_t(g_alloc_heap, RendVkCanvas);
  *platform              = (RendVkCanvas){
      .dummy = 42,
  };

  log_i("Vulkan canvas created", log_param("size", gap_vector_fmt(size)));
  return platform;
}

void rend_vk_canvas_destroy(RendVkCanvas* canvas) {
  log_i("Vulkan canvas destroyed");
  alloc_free_t(g_alloc_heap, canvas);
}

void rend_vk_canvas_resize(RendVkCanvas* canvas, const GapVector size) {
  (void)canvas;
  (void)size;

  log_i("Vulkan canvas resized", log_param("size", gap_vector_fmt(size)));
}
