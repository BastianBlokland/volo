#include "asset_texture.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_math.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "repo_internal.h"

/**
 * Height texture - Collection of height values without any meta-data.
 * Supported types:
 * - r16 (16 bit unsigned integers)
 * - r32 (32 bit IEEE-754 signed floats)
 *
 * This format is commonly used for heightmaps (for example by WorldMachine or Gaea).
 * Because there is no meta-data the pixel size is assumed to be a square power-of-two.
 * NOTE: The values are assumed to have been written in the same endianness as the host.
 */

typedef enum {
  HtexType_U16, // 16 bit unsigned integers.
  HtexType_F32, // 32 bit IEEE-754 signed floats.
} HtexType;

static usize htex_pixel_size(const HtexType type) {
  switch (type) {
  case HtexType_U16:
    return sizeof(u16);
  case HtexType_F32:
    return sizeof(f32);
  }
  diag_crash();
}

static usize htex_pixel_align(const HtexType type) { return htex_pixel_size(type); }

static AssetTextureType htex_texture_type(const HtexType type) {
  switch (type) {
  case HtexType_U16:
    return AssetTextureType_U16;
  case HtexType_F32:
    return AssetTextureType_F32;
  }
  diag_crash();
}

typedef enum {
  HtexError_None = 0,
  HtexError_Corrupt,
  HtexError_Empty,
  HtexError_NonPow2,

  HtexError_Count,
} HtexError;

static String htex_error_str(const HtexError err) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("Corrupt height texture data"),
      string_static("Missing height texture data"),
      string_static("Non power-of-two size"),
  };
  ASSERT(array_elems(g_msgs) == HtexError_Count, "Incorrect number of error messages");
  return g_msgs[err];
}

static void htex_load_fail(EcsWorld* world, const EcsEntityId e, const HtexError err) {
  log_e("Failed to parse height texture", log_param("error", fmt_text(htex_error_str(err))));
  ecs_world_add_empty_t(world, e, AssetFailedComp);
}

static void htex_load(EcsWorld* world, const EcsEntityId entity, String data, const HtexType type) {
  const usize pixelSize = htex_pixel_size(type);
  if (UNLIKELY(data.size % pixelSize)) {
    htex_load_fail(world, entity, HtexError_Corrupt);
    return;
  }
  const usize pixelCount = data.size / pixelSize;
  if (UNLIKELY(!pixelCount)) {
    htex_load_fail(world, entity, HtexError_Empty);
    return;
  }
  const u32 size = (u32)math_sqrt_f64(pixelCount);
  if (UNLIKELY(size * size != pixelCount)) {
    htex_load_fail(world, entity, HtexError_NonPow2);
    return;
  }

  Mem outputMem = alloc_alloc(g_alloc_heap, pixelSize * pixelCount, htex_pixel_align(type));

  /**
   * Read the pixels into the output memory.
   * NOTE: Iterate y backwards because we're using y0 to mean the bottom of the texture and most
   * authoring tools use y0 to mean the top.
   */
  for (u32 y = size; y-- != 0;) {
    for (u32 x = 0; x != size; ++x) {
      const usize outputIndex    = y * (usize)size + x;
      const Mem   outputPixelMem = mem_slice(outputMem, outputIndex * pixelSize, pixelSize);

      // Copy the pixel data.
      // NOTE: Assumes values written in the same endianess as the host.
      mem_cpy(outputPixelMem, mem_slice(data, 0, pixelSize));

      // Advance input data.
      data = mem_consume(data, pixelSize);
    }
  }

  ecs_world_add_t(
      world,
      entity,
      AssetTextureComp,
      .type      = htex_texture_type(type),
      .channels  = AssetTextureChannels_One,
      .width     = size,
      .height    = size,
      .pixelsRaw = outputMem.ptr);
  ecs_world_add_empty_t(world, entity, AssetLoadedComp);
}

void asset_load_r16(EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  (void)id;
  htex_load(world, entity, src->data, HtexType_U16);
  asset_repo_source_close(src);
}

void asset_load_r32(EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  (void)id;
  htex_load(world, entity, src->data, HtexType_F32);
  asset_repo_source_close(src);
}
