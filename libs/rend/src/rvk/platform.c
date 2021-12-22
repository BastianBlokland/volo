#include "core_array.h"
#include "core_diag.h"
#include "core_thread.h"
#include "log_logger.h"

#include "canvas_internal.h"
#include "device_internal.h"
#include "graphic_internal.h"
#include "platform_internal.h"

typedef enum {
  RvkWellKnownType_None,
  RvkWellKnownType_Texture,
} RvkWellKnownType;

typedef struct {
  RvkWellKnownType type;
  union {
    RvkTexture* texture;
  };
} RvkWellKnownEntry;

struct sRvkPlatform {
  RvkDevice*        dev;
  RvkWellKnownEntry wellknown[RvkWellKnownId_Count];
};

String rvk_wellknown_id_str(const RvkWellKnownId id) {
  static const String names[] = {
      string_static("MissingTexture"),
  };
  ASSERT(array_elems(names) == RvkWellKnownId_Count, "Incorrect number of names");
  return names[id];
}

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

void rvk_platform_texture_set(RvkPlatform* plat, const RvkWellKnownId id, RvkTexture* tex) {
  plat->wellknown[id].type    = RvkWellKnownType_Texture;
  plat->wellknown[id].texture = tex;
}

RvkTexture* rvk_platform_texture_get(const RvkPlatform* plat, const RvkWellKnownId id) {
  if (UNLIKELY(plat->wellknown[id].type != RvkWellKnownType_Texture)) {
    diag_crash_msg(
        "Wellknown asset '{}' cannot be found or is of the wrong type",
        fmt_text(rvk_wellknown_id_str(id)));
  }
  return plat->wellknown[id].texture;
}
