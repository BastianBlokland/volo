#include "asset_texture.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_math.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "repo_internal.h"

/**
 * Float texture - Simple collection of 32 bit IEEE-754 floats without any meta-data.
 * This format is commonly used for heightmaps (for example by WorldMachine or Gaea).
 * Because there is no meta-data the pixel size is assumed to be a square power-of-two.
 * NOTE: The floats are assumed to have been written in the same endianness as the host.
 */

typedef enum {
  FtexType_32, // 32 bit floats.
} FtexType;

static usize ftex_pixel_size(const FtexType type) {
  switch (type) {
  case FtexType_32:
    return sizeof(f32);
  }
  diag_crash();
}

static usize ftex_pixel_align(const FtexType type) { return ftex_pixel_size(type); }

static AssetTextureType ftex_texture_type(const FtexType type) {
  switch (type) {
  case FtexType_32:
    return AssetTextureType_F32;
  }
  diag_crash();
}

typedef enum {
  FtexError_None = 0,
  FtexError_Corrupt,
  FtexError_NonPow2,

  FtexError_Count,
} FtexError;

static String ftex_error_str(const FtexError err) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("Corrupt float texture data"),
      string_static("Non power-of-two size"),
  };
  ASSERT(array_elems(g_msgs) == FtexError_Count, "Incorrect number of error messages");
  return g_msgs[err];
}

static void ftex_load_fail(EcsWorld* world, const EcsEntityId e, const FtexError err) {
  log_e("Failed to parse float texture", log_param("error", fmt_text(ftex_error_str(err))));
  ecs_world_add_empty_t(world, e, AssetFailedComp);
}

static void ftex_load(EcsWorld* world, const EcsEntityId entity, String data, const FtexType type) {
  const usize pixelSize = ftex_pixel_size(type);
  if (data.size % pixelSize) {
    ftex_load_fail(world, entity, FtexError_Corrupt);
    return;
  }
  const usize pixelCount = data.size / pixelSize;
  const u32   size       = (u32)math_sqrt_f64(pixelCount);
  if (size * size != pixelCount) {
    ftex_load_fail(world, entity, FtexError_NonPow2);
    return;
  }

  Mem outputMem = alloc_alloc(g_alloc_heap, pixelSize * pixelCount, ftex_pixel_align(type));

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
      // NOTE: Assumes IEEE-754 floats with the same endianess as the host.
      mem_cpy(outputPixelMem, mem_slice(data, 0, pixelSize));

      // Advance input data.
      data = mem_consume(data, pixelSize);
    }
  }

  ecs_world_add_t(
      world,
      entity,
      AssetTextureComp,
      .type      = ftex_texture_type(type),
      .channels  = AssetTextureChannels_One,
      .width     = size,
      .height    = size,
      .pixelsRaw = outputMem.ptr);
  ecs_world_add_empty_t(world, entity, AssetLoadedComp);
}

void asset_load_r32(EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  (void)id;
  ftex_load(world, entity, src->data, FtexType_32);
  asset_repo_source_close(src);
}
