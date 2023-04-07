#include "asset_sound.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "repo_internal.h"

/**
 * Waveform Audio File Format.
 * Only a single continuous block of LPCM (linear pulse-code modulation) samples are supported.
 * Wav: https://en.wikipedia.org/wiki/WAV
 * Riff: https://en.wikipedia.org/wiki/Resource_Interchange_File_Format
 */

#define wav_max_channels 2

typedef struct {
  String tag;
  Mem    data;
} WavChunk;

typedef struct {
  u16 formatType;
  u16 channels;
  u32 samplesPerSec;
  u32 avgBytesPerSec;
  u16 blockAlign;
  u16 bitsPerSample;
} WavFormat;

typedef enum {
  WavFormatType_PCM = 1,
} WavFormatType;

typedef enum {
  WavError_None                       = 0,
  WavError_RiffChunkMalformed         = 1,
  WavError_RiffChunkTruncated         = 2,
  WavError_RiffChunkMissingPadding    = 3,
  WavError_RiffUnsupportedRootChunk   = 4,
  WavError_RiffUnsupportedChunkList   = 5,
  WavError_FormatChunkMissing         = 6,
  WavError_FormatChunkMalformed       = 7,
  WavError_FormatTypeUnsupported      = 8,
  WavError_ChannelCountExceedsMaximum = 9,

  WavError_Count,
} WavError;

static String wav_error_str(WavError res) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("Malformed RIFF chunk"),
      string_static("Truncated RIFF chunk"),
      string_static("RIFF chunk is missing padding"),
      string_static("Unsupported root RIFF chunk"),
      string_static("Unsupported RIFF chunk list (Only 'WAVE' is supported)"),
      string_static("Format chunk missing"),
      string_static("Format chunk malformed"),
      string_static("Format type unsupported (Only 'PCM' is supported)"),
      string_static("Channel count exceeds the maximum"),
  };
  ASSERT(array_elems(g_msgs) == WavError_Count, "Incorrect number of wav-error messages");
  return g_msgs[res];
}

static Mem wav_consume_tag(Mem data, String* out) {
  static const u32 g_tagSize = 4;
  if (UNLIKELY(data.size < 4)) {
    *out = string_empty;
    return data;
  }
  *out = mem_slice(data, 0, g_tagSize);
  return mem_consume(data, g_tagSize);
}

static Mem wav_consume_chunk(Mem data, WavChunk* out, WavError* err) {
  if (UNLIKELY(data.size < 8)) {
    *err = WavError_RiffChunkMalformed;
    return data;
  }
  u32 chunkSize;
  data = wav_consume_tag(data, &out->tag);
  data = mem_consume_le_u32(data, &chunkSize);
  if (UNLIKELY(data.size < chunkSize)) {
    *err = WavError_RiffChunkTruncated;
    return data;
  }
  out->data = mem_slice(data, 0, chunkSize);

  data = mem_consume(data, chunkSize);
  if (chunkSize % 2) {
    if (!data.size) {
      *err = WavError_RiffChunkMissingPadding;
      return data;
    }
    data = mem_consume(data, 1);
  }
  return data;
}

static Mem wav_consume_chunk_list(
    Mem       data,
    DynArray* out, // WavChunk[], needs to be already initialized.
    WavError* err) {
  if (UNLIKELY(data.size < 4)) {
    *err = WavError_RiffChunkMalformed;
    return data;
  }
  String identifier;
  data = wav_consume_tag(data, &identifier);
  if (UNLIKELY(!string_eq(identifier, string_lit("WAVE")))) {
    *err = WavError_RiffUnsupportedChunkList;
    return data;
  }
  while (data.size) {
    WavChunk chunk;
    data = wav_consume_chunk(data, &chunk, err);
    if (*err) {
      return data;
    }
    *dynarray_push_t(out, WavChunk) = chunk;
  }
  return data;
}

static WavChunk* wav_chunk(DynArray* chunks, const String tag) {
  dynarray_for_t(chunks, WavChunk, chunk) {
    if (string_starts_with(chunk->tag, tag)) {
      return chunk;
    }
  }
  return null;
}

static void wav_read_format(DynArray* chunks, WavFormat* out, WavError* err) {
  WavChunk* chunk = wav_chunk(chunks, string_lit("fmt"));
  if (UNLIKELY(!chunk)) {
    *err = WavError_FormatChunkMissing;
    return;
  }
  Mem data = chunk->data;
  if (data.size < 16) {
    *err = WavError_FormatChunkMalformed;
    return;
  }
  data = mem_consume_le_u16(data, &out->formatType);
  data = mem_consume_le_u16(data, &out->channels);
  data = mem_consume_le_u32(data, &out->samplesPerSec);
  data = mem_consume_le_u32(data, &out->avgBytesPerSec);
  data = mem_consume_le_u16(data, &out->blockAlign);
  data = mem_consume_le_u16(data, &out->bitsPerSample);
}

static void wav_load_succeed(EcsWorld* world, const EcsEntityId entity) {
  ecs_world_add_empty_t(world, entity, AssetLoadedComp);
  AssetSoundComp* result = ecs_world_add_t(world, entity, AssetSoundComp);
  (void)result;
}

static void wav_load_fail(EcsWorld* world, const EcsEntityId entity, const WavError err) {
  log_e("Failed to parse Wave file", log_param("error", fmt_text(wav_error_str(err))));
  ecs_world_add_empty_t(world, entity, AssetFailedComp);
}

void asset_load_wav(EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  (void)id;

  WavError err    = WavError_None;
  DynArray chunks = dynarray_create_t(g_alloc_heap, WavChunk, 8);
  WavChunk rootChunk;
  wav_consume_chunk(src->data, &rootChunk, &err);
  if (err) {
    wav_load_fail(world, entity, err);
    goto End;
  }
  if (!string_eq(rootChunk.tag, string_lit("RIFF"))) {
    wav_load_fail(world, entity, WavError_RiffUnsupportedRootChunk);
    goto End;
  }
  wav_consume_chunk_list(rootChunk.data, &chunks, &err);
  if (err) {
    wav_load_fail(world, entity, err);
    goto End;
  }
  WavFormat format;
  wav_read_format(&chunks, &format, &err);
  if (err) {
    wav_load_fail(world, entity, err);
    goto End;
  }
  if (format.formatType != WavFormatType_PCM) {
    wav_load_fail(world, entity, WavError_FormatTypeUnsupported);
    goto End;
  }
  if (format.channels > wav_max_channels) {
    wav_load_fail(world, entity, WavError_ChannelCountExceedsMaximum);
    goto End;
  }
  wav_load_succeed(world, entity);

End:
  dynarray_destroy(&chunks);
  asset_repo_source_close(src);
}
