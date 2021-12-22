#include "core_array.h"
#include "core_diag.h"
#include "core_thread.h"
#include "log_logger.h"

#include "canvas_internal.h"
#include "device_internal.h"
#include "graphic_internal.h"
#include "platform_internal.h"

struct sRvkPlatform {
  RvkDevice* dev;
};

RvkPlatform* rvk_platform_create() {
  RvkPlatform* plat = alloc_alloc_t(g_alloc_heap, RvkPlatform);
  *plat             = (RvkPlatform){
      .dev = rvk_device_create(),
  };
  return plat;
}

void rvk_platform_destroy(RvkPlatform* plat) {
  rvk_device_destroy(plat->dev);
  alloc_free_t(g_alloc_heap, plat);
}

RvkDevice* rvk_platform_device(const RvkPlatform* plat) { return plat->dev; }

void rvk_platform_update(RvkPlatform* plat) { rvk_device_update(plat->dev); }

void rvk_platform_wait_idle(const RvkPlatform* plat) { rvk_device_wait_idle(plat->dev); }

RvkCanvas* rvk_platform_canvas_create(RvkPlatform* plat, const GapWindowComp* window) {
  return rvk_canvas_create(plat->dev, window);
}
