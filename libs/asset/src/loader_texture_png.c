#include "core_array.h"
#include "core_bits.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "loader_texture_internal.h"
#include "repo_internal.h"

/**
 * Portable Network Graphics.
 * NOTE: Only 8 bit images are supported.
 * NOTE: Indexed and or interlaced images are not supported.
 * NOTE: Grayscale with alpha is not supported.
 *
 * Spec: https://www.w3.org/TR/png-3/
 */

#define png_max_chunks 64
#define png_max_width (1024 * 16)
#define png_max_height (1024 * 16)

static const String g_pngMagic = string_lit("\x89\x50\x4E\x47\x0D\x0A\x1A\x0A");

typedef struct {
  u8  type[4];
  Mem data;
} PngChunk;

typedef enum {
  PngChannels_Invalid,
  PngChannels_R    = 1,
  PngChannels_RGB  = 3,
  PngChannels_RGBA = 4,
} PngChannels;

typedef struct {
  u32 width, height;
  u8  bitDepth;
  u8  colorType;
  u8  compressionMethod, filterMethod, interlaceMethod;
} PngHeader;

typedef enum {
  PngError_None = 0,
  PngError_MagicMismatch,
  PngError_Truncated,
  PngError_Malformed,
  PngError_ChunkLimitExceeded,
  PngError_ChunkChecksumFailed,
  PngError_HeaderChunkMissing,
  PngError_EndChunkMissing,
  PngError_UnsupportedColorType,
  PngError_UnsupportedCompression,
  PngError_UnsupportedFilter,
  PngError_UnsupportedInterlacing,
  PngError_UnsupportedBitDepth,
  PngError_UnsupportedSize,

  PngError_Count,
} PngError;

static String png_error_str(const PngError err) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("Data is not a png file"),
      string_static("Truncated png data"),
      string_static("Malformed png data"),
      string_static("Png exceeds chunk limit"),
      string_static("Png chunk checksum failed"),
      string_static("Png header chunk missing"),
      string_static("Png end chunk missing"),
      string_static("Unsupported png color-type (only R, RGB, and RGBA supported)"),
      string_static("Unsupported png compression method"),
      string_static("Unsupported png filter method"),
      string_static("Unsupported png interlace method (only non-interlaced is supported)"),
      string_static("Unsupported image bit depth (only 8 bit supported)"),
      string_static("Unsupported image size"),
  };
  ASSERT(array_elems(g_msgs) == PngError_Count, "Incorrect number of png-error messages");
  return g_msgs[err];
}

static bool png_chunk_match(const PngChunk* chunk, const String type) {
  return mem_eq(array_mem(chunk->type), type);
}

static const PngChunk* png_chunk_find(const PngChunk chunks[], const u32 count, const String type) {
  for (u32 i = 0; i != count; ++i) {
    if (png_chunk_match(&chunks[i], type)) {
      return &chunks[i];
    }
  }
  return null;
}

static u32 png_read_chunks(Mem d, PngChunk out[PARAM_ARRAY_SIZE(png_max_chunks)], PngError* err) {
  // Read magic bytes.
  if (UNLIKELY(!string_starts_with(d, g_pngMagic))) {
    *err = PngError_MagicMismatch;
    return 0;
  }
  d = mem_consume(d, g_pngMagic.size);

  // Read all chunks.
  u32 chunkCount = 0;
  while (!string_is_empty(d)) {
    if (UNLIKELY(chunkCount == png_max_chunks)) {
      *err = PngError_ChunkLimitExceeded;
      return 0;
    }
    if (UNLIKELY(d.size < 4)) {
      *err = PngError_Truncated;
      return 0;
    }
    PngChunk* chunk = &out[chunkCount++];

    // Read length.
    u32 length;
    d = mem_consume_be_u32(d, &length);

    if (UNLIKELY(d.size < (length + 8))) {
      *err = PngError_Truncated;
      return 0;
    }
    const Mem typeAndDataMem = mem_slice(d, 0, length + 4);

    // Read type.
    mem_cpy(array_mem(chunk->type), mem_slice(d, 0, 4));
    d = mem_consume(d, sizeof(chunk->type));

    // Read data.
    chunk->data = mem_slice(d, 0, length);
    d           = mem_consume(d, length);

    // Read checksum.
    u32 crc;
    d = mem_consume_be_u32(d, &crc);

    // Validate checksum.
    if (UNLIKELY(crc != bits_crc_32(0, typeAndDataMem))) {
      *err = PngError_ChunkChecksumFailed;
      return 0;
    }
  }
  return chunkCount;
}

static void png_read_header(const PngChunk* chunk, PngHeader* out, PngError* err) {
  Mem d = chunk->data;
  if (UNLIKELY(d.size != 13)) {
    *err = PngError_Malformed;
    return;
  }
  d = mem_consume_be_u32(d, &out->width);
  d = mem_consume_be_u32(d, &out->height);
  d = mem_consume_u8(d, &out->bitDepth);
  d = mem_consume_u8(d, &out->colorType);
  d = mem_consume_u8(d, &out->compressionMethod);
  d = mem_consume_u8(d, &out->filterMethod);
  d = mem_consume_u8(d, &out->interlaceMethod);
}

static PngChannels png_channels(const PngHeader* header) {
  switch (header->colorType) {
  case 0:
    return PngChannels_R;
  case 2:
    return PngChannels_RGB;
  case 6:
    return PngChannels_RGBA;
  default:
    return PngChannels_Invalid;
  }
}

static void png_load_fail(EcsWorld* w, const EcsEntityId e, const String id, const PngError err) {
  log_e(
      "Failed to parse Png texture",
      log_param("id", fmt_text(id)),
      log_param("entity", ecs_entity_fmt(e)),
      log_param("error", fmt_text(png_error_str(err))));
  ecs_world_add_empty_t(w, e, AssetFailedComp);
}

static bool png_is_normalmap(const String id) {
  static const String g_patterns[] = {
      string_static("*_nrm*"),
      string_static("*_normal*"),
  };
  array_for_t(g_patterns, String, pattern) {
    if (string_match_glob(id, *pattern, StringMatchFlags_IgnoreCase)) {
      return true;
    }
  }
  return false;
}

static bool png_is_lossless(const String id) {
  return string_match_glob(id, string_lit("*_lossless*"), StringMatchFlags_IgnoreCase);
}

void asset_load_tex_png(
    EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {

  PngError  err = PngError_None;
  PngChunk  chunks[png_max_chunks];
  const u32 chunkCount = png_read_chunks(src->data, chunks, &err);
  if (UNLIKELY(err)) {
    png_load_fail(world, entity, id, err);
    goto Ret;
  }
  if (UNLIKELY(!chunkCount || !png_chunk_match(&chunks[0], string_lit("IHDR")))) {
    png_load_fail(world, entity, id, PngError_HeaderChunkMissing);
    goto Ret;
  }
  if (UNLIKELY(!chunkCount || !png_chunk_match(&chunks[chunkCount - 1], string_lit("IEND")))) {
    png_load_fail(world, entity, id, PngError_EndChunkMissing);
    goto Ret;
  }

  PngHeader header;
  png_read_header(&chunks[0], &header, &err);
  if (UNLIKELY(err)) {
    png_load_fail(world, entity, id, err);
    goto Ret;
  }
  const PngChannels channels = png_channels(&header);
  if (UNLIKELY(!channels)) {
    png_load_fail(world, entity, id, PngError_UnsupportedColorType);
    goto Ret;
  }
  if (UNLIKELY(!header.width || !header.height)) {
    png_load_fail(world, entity, id, PngError_UnsupportedSize);
    goto Ret;
  }
  if (UNLIKELY(header.width > png_max_width || header.height > png_max_height)) {
    png_load_fail(world, entity, id, PngError_UnsupportedSize);
    goto Ret;
  }
  if (UNLIKELY(header.bitDepth != 8)) {
    png_load_fail(world, entity, id, PngError_UnsupportedBitDepth);
    goto Ret;
  }
  if (UNLIKELY(header.compressionMethod)) {
    png_load_fail(world, entity, id, PngError_UnsupportedCompression);
    goto Ret;
  }
  if (UNLIKELY(header.filterMethod)) {
    png_load_fail(world, entity, id, PngError_UnsupportedFilter);
    goto Ret;
  }
  if (UNLIKELY(header.interlaceMethod)) {
    png_load_fail(world, entity, id, PngError_UnsupportedInterlacing);
    goto Ret;
  }

  AssetTextureFlags flags = AssetTextureFlags_GenerateMips;
  if (png_is_normalmap(id)) {
    // Normal maps are in linear space (and thus not sRGB).
    flags |= AssetTextureFlags_NormalMap;
  } else if (png_chunk_find(chunks, chunkCount, string_lit("sRGB")) && channels >= 3) {
    flags |= AssetTextureFlags_Srgb;
  }
  if (png_is_lossless(id)) {
    flags |= AssetTextureFlags_Lossless;
  }

  // TODO: Implement png data parsing.
  png_load_fail(world, entity, id, PngError_Truncated);

Ret:
  asset_repo_source_close(src);
}
