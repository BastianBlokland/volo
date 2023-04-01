#include "core_annotation.h"
#include "core_diag.h"
#include "log_logger.h"

#include "constants_internal.h"
#include "device_internal.h"

#include <alsa/asoundlib.h>

static const char* snd_pcm_device = "plughw:0,0";

typedef struct sSndDevice {
  Allocator*     alloc;
  snd_pcm_t*     pcm;
  SndDeviceState state;
} SndDevice;

typedef struct {
  bool              valid;
  snd_pcm_uframes_t bufferSize;
} AlsaPcmConfig;

static String alsa_error_str(const int err) { return string_from_null_term(snd_strerror(err)); }

static snd_pcm_t* alsa_pcm_open() {
  snd_pcm_t* pcm = null;
  const i32  err = snd_pcm_open(&pcm, snd_pcm_device, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
  if (err) {
    log_e(
        "Failed to open sound-device",
        log_param("name", fmt_text(string_from_null_term(snd_pcm_device))),
        log_param("error-code", fmt_int(err)),
        log_param("error", fmt_text(alsa_error_str(err))));
    return null;
  }
  return pcm;
}

static snd_pcm_info_t* alsa_pcm_info_scratch(snd_pcm_t* pcm) {
  snd_pcm_info_t* info = alloc_alloc(g_alloc_scratch, snd_pcm_info_sizeof(), sizeof(void*)).ptr;
  const i32       err  = snd_pcm_info(pcm, info);
  if (err) {
    log_e(
        "Failed to retrieve sound-device info",
        log_param("error-code", fmt_int(err)),
        log_param("error", fmt_text(alsa_error_str(err))));
    return null;
  }
  return info;
}

static AlsaPcmConfig alsa_pcm_initialize(snd_pcm_t* pcm) {
  i32           err    = 0;
  AlsaPcmConfig result = {0};

  // Configure the hardware parameters.
  const usize          hwParamsSize = snd_pcm_hw_params_sizeof();
  snd_pcm_hw_params_t* hwParams     = alloc_alloc(g_alloc_scratch, hwParamsSize, sizeof(void*)).ptr;
  if ((err = snd_pcm_hw_params_any(pcm, hwParams))) {
    goto Err;
  }
  if ((err = snd_pcm_hw_params_set_rate_resample(pcm, hwParams, true))) {
    goto Err;
  }
  if ((err = snd_pcm_hw_params_set_access(pcm, hwParams, SND_PCM_ACCESS_RW_INTERLEAVED))) {
    goto Err;
  }
  if ((err = snd_pcm_hw_params_set_format(pcm, hwParams, SND_PCM_FORMAT_S16_LE))) {
    goto Err;
  }
  if ((err = snd_pcm_hw_params_set_channels(pcm, hwParams, snd_sample_channels))) {
    goto Err;
  }
  u32 actualSampleRate = snd_sample_frequency;
  if ((err = snd_pcm_hw_params_set_rate_near(pcm, hwParams, &actualSampleRate, 0))) {
    goto Err;
  }
  if (actualSampleRate != snd_sample_frequency) {
    log_e(
        "Sound-device does not support frequency {}",
        log_param("frequency", fmt_int(snd_sample_frequency)));
    goto Err;
  }

  // Apply the hardware parameters.
  if ((err = snd_pcm_hw_params(pcm, hwParams))) {
    goto Err;
  }

  // Retrieve buffer config.
  if ((err = snd_pcm_hw_params_get_buffer_size(hwParams, &result.bufferSize))) {
    goto Err;
  }
  if (snd_pcm_hw_params_get_sbits(hwParams) != 16) {
    log_e(
        "Sound-device does not support bit-depth {}",
        log_param("depth", fmt_int(snd_sample_depth)));
  }

  result.valid = true;
  return result;

Err:
  log_e(
      "Failed to setup sound-device",
      log_param("error-code", fmt_int(err)),
      log_param("error", fmt_text(err ? alsa_error_str(err) : string_lit("unknown"))));
  return result;
}

static bool alsa_pcm_prepare(snd_pcm_t* pcm) {
  i32 err;
  if (UNLIKELY(err = snd_pcm_prepare(pcm))) {
    goto Err;
  }
  return true; // Ready for playing.

Err:
  log_e(
      "Failed to prepare sound-device",
      log_param("error-code", fmt_int(err)),
      log_param("error", fmt_text(err ? alsa_error_str(err) : string_lit("unknown"))));
  return false;
}

SndDevice* snd_device_create(Allocator* alloc) {
  snd_pcm_t*    pcm    = alsa_pcm_open();
  AlsaPcmConfig config = {0};
  if (pcm) {
    config = alsa_pcm_initialize(pcm);
  }
  SndDeviceState state = SndDeviceState_Error;
  if (config.valid) {
    const snd_pcm_type_t  type     = snd_pcm_type(pcm);
    const snd_pcm_state_t pcmState = snd_pcm_state(pcm);
    const snd_pcm_info_t* info     = alsa_pcm_info_scratch(pcm);
    const i32             card     = info ? snd_pcm_info_get_card(info) : -1;
    const String id = info ? string_from_null_term(snd_pcm_info_get_id(info)) : string_empty;

    if (pcmState == SND_PCM_STATE_PREPARED || pcmState == SND_PCM_STATE_RUNNING) {
      state = SndDeviceState_Playing;
    } else {
      state = SndDeviceState_Idle;
    }

    log_i(
        "Alsa sound device created",
        log_param("id", fmt_text(id)),
        log_param("card", fmt_int(card)),
        log_param("type", fmt_text(string_from_null_term(snd_pcm_type_name(type)))),
        log_param("state", fmt_text(string_from_null_term(snd_pcm_state_name(pcmState)))),
        log_param("buffer", fmt_size(config.bufferSize)));
  }

  SndDevice* dev = alloc_alloc_t(alloc, SndDevice);
  *dev           = (SndDevice){.alloc = alloc, .pcm = pcm, .state = state};
  return dev;
}

void snd_device_destroy(SndDevice* dev) {
  if (dev->pcm) {
    snd_pcm_close(dev->pcm);
  }

  log_i("Alsa sound device destroyed");

  alloc_free_t(dev->alloc, dev);
}

SndDeviceState snd_device_state(const SndDevice* dev) { return dev->state; }

bool snd_device_begin(SndDevice* dev) {
  switch (dev->state) {
  case SndDeviceState_Error:
    return false;
  case SndDeviceState_Idle: {
    if (alsa_pcm_prepare(dev->pcm)) {
      break;
    } else {
      dev->state = SndDeviceState_Error;
      return false;
    }
  }
  case SndDeviceState_Playing:
    break;
  case SndDeviceState_FrameActive:
    diag_assert_fail("Unable to begin a new sound device frame: Frame already active");
  case SndDeviceState_Count:
    UNREACHABLE
  }

  dev->state = SndDeviceState_FrameActive;
  return true;
}

SndDeviceFrame snd_device_frame(SndDevice* dev) {
  diag_assert(dev->state == SndDeviceState_FrameActive);

  // TODO: retrieve frame data.

  return (SndDeviceFrame){
      .time         = time_steady_clock(),
      .sampleCount  = 0,
      .sampleBuffer = null,
  };
}

void snd_device_end(SndDevice* dev) {
  diag_assert(dev->state == SndDeviceState_FrameActive);

  // TODO: Submit frame.

  dev->state = SndDeviceState_Playing;
}
