#include "core_array.h"
#include "core_bits.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "repo_internal.h"

/**
 * Portable Network Graphics.
 *
 * Spec: https://www.w3.org/TR/png-3/
 */

#define png_max_chunks 64

static const String g_pngMagic = string_lit("\x89\x50\x4E\x47\x0D\x0A\x1A\x0A");

typedef struct {
  u8  type[4];
  Mem data;
} PngChunk;

typedef enum {
  PngError_None = 0,
  PngError_MagicMismatch,
  PngError_Truncated,
  PngError_ChunkLimitExceeded,
  PngError_ChunkChecksumFailed,
  PngError_HeaderChunkMissing,
  PngError_EndChunkMissing,

  PngError_Count,
} PngError;

static String png_error_str(const PngError err) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("Data is not a png file"),
      string_static("Truncated png data"),
      string_static("Png exceeds chunk limit"),
      string_static("Png chunk checksum failed"),
      string_static("Png header chunk missing"),
      string_static("Png end chunk missing"),
  };
  ASSERT(array_elems(g_msgs) == PngError_Count, "Incorrect number of png-error messages");
  return g_msgs[err];
}

static bool png_chunk_match(const PngChunk* chunk, const String type) {
  return mem_eq(array_mem(chunk->type), type);
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

static void png_load_fail(EcsWorld* w, const EcsEntityId e, const String id, const PngError err) {
  log_e(
      "Failed to parse Png texture",
      log_param("id", fmt_text(id)),
      log_param("entity", ecs_entity_fmt(e)),
      log_param("error", fmt_text(png_error_str(err))));
  ecs_world_add_empty_t(w, e, AssetFailedComp);
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

  // TODO: Implement png parsing.
  (void)chunkCount;
  png_load_fail(world, entity, id, PngError_Truncated);

Ret:
  asset_repo_source_close(src);
}
