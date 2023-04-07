#include "core_array.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "repo_internal.h"

/**
 * Waveform Audio File Format.
 * Only a single continuous block of LPCM (linear pulse-code modulation) samples are supported.
 * Wav: https://en.wikipedia.org/wiki/WAV
 * Riff: https://en.wikipedia.org/wiki/Resource_Interchange_File_Format
 */

typedef enum {
  WavError_None = 0,

  WavError_Count,
} WavError;

static String wav_error_str(WavError res) {
  static const String g_msgs[] = {
      string_static("None"),
  };
  ASSERT(array_elems(g_msgs) == WavError_Count, "Incorrect number of wav-error messages");
  return g_msgs[res];
}

static void wav_load_fail(EcsWorld* world, const EcsEntityId entity, const WavError err) {
  log_e("Failed to parse Wave file", log_param("error", fmt_text(wav_error_str(err))));
  ecs_world_add_empty_t(world, entity, AssetFailedComp);
}

void asset_load_wav(EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  (void)world;
  (void)id;
  wav_load_fail(world, entity, WavError_None);
  asset_repo_source_close(src);
}
