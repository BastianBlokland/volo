#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_math.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "import_texture_internal.h"
#include "loader_texture_internal.h"
#include "manager_internal.h"
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

static AssetTextureType htex_texture_type(const HtexType type) {
  switch (type) {
  case HtexType_U16:
    return AssetTextureType_u16;
  case HtexType_F32:
    return AssetTextureType_f32;
  }
  diag_crash();
}

typedef enum {
  HtexError_None = 0,
  HtexError_Corrupt,
  HtexError_Empty,
  HtexError_NonPow2,
  HtexError_ImportFailed,

  HtexError_Count,
} HtexError;

static String htex_error_str(const HtexError err) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("Corrupt height texture data"),
      string_static("Missing height texture data"),
      string_static("Non power-of-two size"),
      string_static("Import failed"),
  };
  ASSERT(array_elems(g_msgs) == HtexError_Count, "Incorrect number of error messages");
  return g_msgs[err];
}

static void
htex_load_fail(EcsWorld* world, const EcsEntityId entity, const String id, const HtexError err) {
  log_e(
      "Failed to parse height texture",
      log_param("id", fmt_text(id)),
      log_param("entity", ecs_entity_fmt(entity)),
      log_param("error", fmt_text(htex_error_str(err))));
  ecs_world_add_empty_t(world, entity, AssetFailedComp);
}

static void htex_load(
    EcsWorld*                 world,
    const AssetImportEnvComp* importEnv,
    const EcsEntityId         entity,
    const String              id,
    String                    data,
    const HtexType            type) {

  const AssetTextureType pixelType = htex_texture_type(type);
  const usize            pixelSize = asset_texture_type_stride(pixelType, 1);
  if (UNLIKELY(data.size % pixelSize)) {
    htex_load_fail(world, entity, id, HtexError_Corrupt);
    return;
  }
  const usize pixelCount = data.size / pixelSize;
  if (UNLIKELY(!pixelCount)) {
    htex_load_fail(world, entity, id, HtexError_Empty);
    return;
  }
  const u32 size = (u32)math_sqrt_f64(pixelCount);
  if (UNLIKELY(size * size != pixelCount)) {
    htex_load_fail(world, entity, id, HtexError_NonPow2);
    return;
  }

  Mem pixelMem = alloc_alloc(g_allocHeap, pixelSize * pixelCount, pixelSize);

  /**
   * Read the pixels.
   * NOTE: Iterate y backwards because we're using y0 to mean the bottom of the texture and most
   * authoring tools use y0 to mean the top.
   */
  const usize rowStride = size * pixelSize;
  for (u32 y = size; y-- != 0;) {
    const usize pixelRowIndex = y * (usize)size;
    const Mem   pixelRowMem   = mem_slice(pixelMem, pixelRowIndex * pixelSize, rowStride);

    // Copy the pixel data.
    // NOTE: Assumes values written in the same endianess as the host.
    mem_cpy(pixelRowMem, mem_slice(data, 0, rowStride));

    // Advance input data.
    data = mem_consume(data, rowStride);
  }

  AssetImportTexture import = {
      .flags        = AssetImportTextureFlags_None,
      .width        = size,
      .height       = size,
      .orgChannels  = 1,
      .orgPixelType = pixelType,
      .orgWidth     = size,
      .orgHeight    = size,
      .orgLayers    = 1,
  };
  if (!asset_import_texture(importEnv, id, &import)) {
    htex_load_fail(world, entity, id, HtexError_ImportFailed);
    alloc_free(g_allocHeap, pixelMem);
    return;
  }

  if (import.width != import.orgWidth || import.height != import.orgHeight) {
    const Mem newMem = alloc_alloc(g_allocHeap, import.width * import.height * type, type);

    asset_texture_convert(
        pixelMem,
        import.orgWidth,
        import.orgHeight,
        1 /* srcChannels */,
        pixelType,
        newMem,
        import.width,
        import.height,
        1 /* dstChannels */,
        pixelType);

    alloc_free(g_allocHeap, pixelMem);
    pixelMem = newMem;
  }

  if (import.trans & AssetImportTextureTrans_FlipY) {
    asset_texture_flip_y(pixelMem, import.width, import.height, 1, pixelType);
  }

  AssetTextureFlags flags = AssetTextureFlags_None;
  if (import.flags & AssetImportTextureFlags_Lossless) {
    flags |= AssetTextureFlags_Lossless;
  }

  AssetTextureComp* texComp = ecs_world_add_t(world, entity, AssetTextureComp);
  *texComp                  = asset_texture_create(
      pixelMem,
      import.width,
      import.height,
      1 /* channels */,
      1 /* layers */,
      1 /* mips */,
      import.mips,
      pixelType,
      flags);

  ecs_world_add_empty_t(world, entity, AssetLoadedComp);
  asset_cache(world, entity, g_assetTexMeta, mem_create(texComp, sizeof(AssetTextureComp)));

  alloc_free(g_allocHeap, pixelMem);
}

void asset_load_tex_height16(
    EcsWorld*                 world,
    const AssetImportEnvComp* importEnv,
    const String              id,
    const EcsEntityId         entity,
    AssetSource*              src) {

  htex_load(world, importEnv, entity, id, src->data, HtexType_U16);
  asset_repo_source_close(src);
}

void asset_load_tex_height32(
    EcsWorld*                 world,
    const AssetImportEnvComp* importEnv,
    const String              id,
    const EcsEntityId         entity,
    AssetSource*              src) {

  htex_load(world, importEnv, entity, id, src->data, HtexType_F32);
  asset_repo_source_close(src);
}
