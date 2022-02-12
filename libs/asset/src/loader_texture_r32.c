#include "asset_texture.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_math.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "repo_internal.h"

/**
 * Raw32 - Simple collection of 32 bit signed floats without any meta-data.
 * This format is commonly used for heightmaps (for example by WorldMachine).
 * Because there is no meta-data the pixel size is assumed to be a square power-of-two.
 * NOTE: The floats are assumed to have been written in the same endianness as the host.
 */

typedef enum {
  Raw32Error_None = 0,
  Raw32Error_Corrupt,
  Raw32Error_NonPow2,

  Raw32Error_Count,
} Raw32Error;

static String raw32_error_str(const Raw32Error err) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("Corrupt raw32 data"),
      string_static("Non power-of-two size"),
  };
  ASSERT(array_elems(g_msgs) == Raw32Error_Count, "Incorrect number of raw32-error messages");
  return g_msgs[err];
}

static void raw32_load_fail(EcsWorld* world, const EcsEntityId entity, const Raw32Error err) {
  log_e("Failed to parse Raw32 texture", log_param("error", fmt_text(raw32_error_str(err))));
  ecs_world_add_empty_t(world, entity, AssetFailedComp);
}

void asset_load_r32(EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  (void)id;

  if (src->data.size % sizeof(f32)) {
    raw32_load_fail(world, entity, Raw32Error_Corrupt);
    goto Error;
  }
  const usize pixelCount = src->data.size / sizeof(f32);
  const u32   size       = (u32)math_sqrt_f64(pixelCount);
  if (size * size != pixelCount) {
    raw32_load_fail(world, entity, Raw32Error_NonPow2);
    goto Error;
  }
  /**
   * TODO: This assumes that the floats have been written using the same endianness as the host.
   */
  Mem pixelMem = alloc_alloc(g_alloc_heap, sizeof(f32) * pixelCount, alignof(f32));
  mem_cpy(pixelMem, src->data);

  asset_repo_source_close(src);
  ecs_world_add_t(
      world,
      entity,
      AssetTextureComp,
      .type      = AssetTextureType_Float,
      .channels  = AssetTextureChannels_One,
      .width     = size,
      .height    = size,
      .pixelsRaw = pixelMem.ptr);
  ecs_world_add_empty_t(world, entity, AssetLoadedComp);
  return;

Error:
  asset_repo_source_close(src);
}
