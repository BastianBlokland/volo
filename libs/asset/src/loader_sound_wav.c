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

#define wav_channels_max 2
#define wav_frames_min 64
#define wav_frames_max (1024 * 1024 * 64)

typedef struct {
  String tag;
  Mem    data;
} WavChunk;

typedef struct {
  u16 formatType;
  u16 channels;    // mono = 1, stereo = 2.
  u32 frameRate;   // eg. 44100.
  u32 byteRate;    // frameRate * channels * sampleDepth / 8.
  u16 frameSize;   // channels * sampleDepth / 8
  u16 sampleDepth; // eg. 16 bits.
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
  WavError_DataChunkMissing           = 10,
  WavError_FrameCountUnsupported      = 11,
  WavError_SampleDepthUnsupported     = 12,

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
      string_static("Data chunk missing"),
      string_static("Unsupported frame-count"),
      string_static("Unsupported sample-depth"),
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
    if (string_eq(chunk->tag, tag)) {
      return chunk;
    }
  }
  return null;
}

static void wav_read_format(DynArray* chunks, WavFormat* out, WavError* err) {
  WavChunk* chunk = wav_chunk(chunks, string_lit("fmt "));
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
  data = mem_consume_le_u32(data, &out->frameRate);
  data = mem_consume_le_u32(data, &out->byteRate);
  data = mem_consume_le_u16(data, &out->frameSize);
  data = mem_consume_le_u16(data, &out->sampleDepth);
}

static void
wav_read_frame_count(const WavFormat format, DynArray* chunks, u32* outFrameCount, WavError* err) {
  WavChunk* chunk = wav_chunk(chunks, string_lit("data"));
  if (UNLIKELY(!chunk)) {
    *err = WavError_DataChunkMissing;
    return;
  }
  *outFrameCount = (u32)chunk->data.size / format.frameSize;
}

static void wav_read_samples(
    const WavFormat format,
    DynArray*       chunks,
    const u32       frameCount,
    f32*            outSamples,
    WavError*       err) {
  WavChunk* chunk = wav_chunk(chunks, string_lit("data"));
  if (UNLIKELY(!chunk)) {
    *err = WavError_DataChunkMissing;
    return;
  }
  if (format.sampleDepth != 16) {
    *err = WavError_SampleDepthUnsupported;
    return;
  }
  static const f32 g_i16MaxInv = 1.0f / i16_max;
  // Assumes the host system is using little-endian byte-order and 2's complement integers.
  i16*      data        = chunk->data.ptr;
  const u32 sampleCount = frameCount * format.channels;
  for (u32 i = 0; i != sampleCount; ++i) {
    outSamples[i] = (f32)data[i] * g_i16MaxInv;
  }
}

static void wav_load_succeed(
    EcsWorld*         world,
    const EcsEntityId entity,
    const WavFormat   format,
    const u32         frameCount,
    const f32*        samples) {
  ecs_world_add_empty_t(world, entity, AssetLoadedComp);
  ecs_world_add_t(
      world,
      entity,
      AssetSoundComp,
      .frameChannels = (u8)format.channels,
      .frameCount    = frameCount,
      .frameRate     = format.frameRate,
      .samples       = samples);
}

static void
wav_load_fail(EcsWorld* world, const EcsEntityId entity, const String id, const WavError err) {
  log_e(
      "Failed to parse Wave file",
      log_param("id", fmt_text(id)),
      log_param("error", fmt_text(wav_error_str(err))));
  ecs_world_add_empty_t(world, entity, AssetFailedComp);
}

void asset_load_wav(EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  WavError err        = WavError_None;
  DynArray chunks     = dynarray_create_t(g_allocHeap, WavChunk, 8);
  f32*     samples    = null;
  u32      frameCount = 0;
  WavChunk rootChunk;
  wav_consume_chunk(src->data, &rootChunk, &err);
  if (err) {
    wav_load_fail(world, entity, id, err);
    goto End;
  }
  if (!string_eq(rootChunk.tag, string_lit("RIFF"))) {
    wav_load_fail(world, entity, id, WavError_RiffUnsupportedRootChunk);
    goto End;
  }
  wav_consume_chunk_list(rootChunk.data, &chunks, &err);
  if (err) {
    wav_load_fail(world, entity, id, err);
    goto End;
  }
  WavFormat format;
  wav_read_format(&chunks, &format, &err);
  if (err) {
    wav_load_fail(world, entity, id, err);
    goto End;
  }
  if (format.formatType != WavFormatType_PCM) {
    wav_load_fail(world, entity, id, WavError_FormatTypeUnsupported);
    goto End;
  }
  if (format.channels > wav_channels_max) {
    wav_load_fail(world, entity, id, WavError_ChannelCountExceedsMaximum);
    goto End;
  }
  wav_read_frame_count(format, &chunks, &frameCount, &err);
  if (err) {
    wav_load_fail(world, entity, id, err);
    goto End;
  }
  if (frameCount < wav_frames_min || frameCount > wav_frames_max) {
    wav_load_fail(world, entity, id, WavError_FrameCountUnsupported);
    goto End;
  }
  samples = alloc_array_t(g_allocHeap, f32, frameCount * format.channels);
  wav_read_samples(format, &chunks, frameCount, samples, &err);
  if (err) {
    wav_load_fail(world, entity, id, err);
    goto End;
  }

  wav_load_succeed(world, entity, format, frameCount, samples);
  samples = null; // Moved into the result component, which will take ownership.

End:
  dynarray_destroy(&chunks);
  if (samples) {
    alloc_free_array_t(g_allocHeap, samples, frameCount * format.channels);
  }
  asset_repo_source_close(src);
}
