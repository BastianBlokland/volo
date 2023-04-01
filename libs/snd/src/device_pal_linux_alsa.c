#include "core_annotation.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_thread.h"
#include "log_logger.h"

#include "constants_internal.h"
#include "device_internal.h"

#include <alsa/asoundlib.h>

/**
 * Alsa PCM playback sound device implementation.
 *
 * Use a simple double-buffering strategy where we use two periods, one playing on the device and
 * one being recorded. There's many strategies we can explore to reduce latency in the future.
 */

static const char* snd_pcm_device = "plughw:0,0";

#define snd_alsa_period_count 2
#define snd_alsa_period_frames 2048

typedef struct sSndDevice {
  Allocator*     alloc;
  snd_pcm_t*     pcm;
  SndDeviceState state;
  i16*           activeMapping;
} SndDevice;

typedef struct {
  bool              valid;
  snd_pcm_uframes_t bufferSize;
} AlsaPcmConfig;

static String alsa_error_str(const int err) { return string_from_null_term(snd_strerror(err)); }

static void alsa_error_handler(
    const char* file, const i32 line, const char* func, const i32 err, const char* fmt, ...) {

  va_list arg;
  va_start(arg, fmt);

  char         msgBuf[64];
  const i32    msgLength = vsnprintf(msgBuf, sizeof(msgBuf), fmt, arg);
  const String msg       = {.ptr = msgBuf, .size = math_clamp_i32(msgLength, 0, sizeof(msgBuf))};

  log_e(
      "Alsa error: {}",
      log_param("msg", fmt_text(msg)),
      log_param("err", fmt_text(alsa_error_str(err))),
      log_param("file", fmt_text(string_from_null_term(file))),
      log_param("line", fmt_int(line)),
      log_param("func", fmt_text(string_from_null_term(func))));

  va_end(arg);
}

static void alsa_init() {
  static ThreadSpinLock g_initLock;
  static bool           g_initialized;
  if (LIKELY(g_initialized)) {
    return;
  }
  thread_spinlock_lock(&g_initLock);
  if (!g_initialized) {
    snd_lib_error_set_handler(&alsa_error_handler);
    g_initialized = true;
  }
  thread_spinlock_unlock(&g_initLock);
}

static snd_pcm_t* alsa_pcm_open() {
  snd_pcm_t* pcm = null;
  const i32  err = snd_pcm_open(&pcm, snd_pcm_device, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
  if (err) {
    log_e(
        "Failed to open sound-device",
        log_param("name", fmt_text(string_from_null_term(snd_pcm_device))),
        log_param("err", fmt_text(alsa_error_str(err))));
    return null;
  }
  return pcm;
}

static snd_pcm_info_t* alsa_pcm_info_scratch(snd_pcm_t* pcm) {
  snd_pcm_info_t* info = alloc_alloc(g_alloc_scratch, snd_pcm_info_sizeof(), sizeof(void*)).ptr;
  const i32       err  = snd_pcm_info(pcm, info);
  if (err) {
    log_e("Failed to retrieve sound-device info", log_param("err", fmt_text(alsa_error_str(err))));
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
  if ((err = snd_pcm_hw_params_set_channels(pcm, hwParams, snd_channel_count))) {
    goto Err;
  }
  u32 sampleFreq = snd_sample_frequency;
  if ((err = snd_pcm_hw_params_set_rate_near(pcm, hwParams, &sampleFreq, 0))) {
    goto Err;
  }
  if (sampleFreq != snd_sample_frequency) {
    log_e("Sound-device sample frequency not supported");
    goto Err;
  }
  if ((err = snd_pcm_hw_params_set_periods(pcm, hwParams, snd_alsa_period_count, 0))) {
    goto Err;
  }
  snd_pcm_uframes_t periodSize = snd_alsa_period_frames;
  if ((err = snd_pcm_hw_params_set_period_size_near(pcm, hwParams, &periodSize, 0))) {
    goto Err;
  }
  if (periodSize != snd_alsa_period_frames) {
    log_e("Sound-device period-size not supported");
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
  log_e("Failed to setup sound-device", log_param("err", fmt_text(alsa_error_str(err))));
  return result;
}

static bool alsa_pcm_prepare(snd_pcm_t* pcm) {
  i32 err;
  if (UNLIKELY(err = snd_pcm_prepare(pcm))) {
    goto Err;
  }
  return true; // Ready for playing.

Err:
  log_e("Failed to prepare sound-device", log_param("err", fmt_text(alsa_error_str(err))));
  return false;
}

static u32 alsa_pcm_available_frames(snd_pcm_t* pcm) {
  const snd_pcm_sframes_t avail = snd_pcm_avail_update(pcm);
  if (UNLIKELY(avail < 0)) {
    log_e("Failed to query sound-device", log_param("err", fmt_text(alsa_error_str((i32)avail))));
    return 0;
  }
  return (u32)avail;
}

static i16* alsa_pcm_period_map(snd_pcm_t* pcm) {
  const snd_pcm_channel_area_t* areas;
  snd_pcm_uframes_t             offset;
  snd_pcm_uframes_t             frames = snd_alsa_period_frames;
  i32                           err;
  if ((err = snd_pcm_mmap_begin(pcm, &areas, &offset, &frames))) {
    log_e("Failed to map from sound-device", log_param("err", fmt_text(alsa_error_str(err))));
    return null;
  }
  diag_assert(areas[0].step == sizeof(i16) * snd_channel_count);
  diag_assert(areas[1].step == sizeof(i16) * snd_channel_count);
  diag_assert(offset == 0); // Expect to start on a period boundary.
  diag_assert(areas[1].addr == bits_ptr_offset(areas[0].addr, sizeof(i16))); // Expect interleaved.
  return (i16*)areas[0].addr;
}

static bool alsa_pcm_period_commit(snd_pcm_t* pcm) {
  const snd_pcm_uframes_t offset    = 0;                      // Start on a period boundary.
  const snd_pcm_uframes_t frames    = snd_alsa_period_frames; // Commit a whole period.
  const snd_pcm_sframes_t committed = snd_pcm_mmap_commit(pcm, offset, frames);
  if (committed < 0 || (snd_pcm_uframes_t)committed != frames) {
    const i32 err = (i32)committed;
    log_e("Failed to commit to sound-device", log_param("err", fmt_text(alsa_error_str(err))));
    return false;
  }
  return true;
}

SndDevice* snd_device_create(Allocator* alloc) {
  alsa_init();

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
      dev->state = SndDeviceState_Playing;
      break;
    } else {
      dev->state = SndDeviceState_Error;
      return false;
    }
  }
  case SndDeviceState_Playing:
    break;
  case SndDeviceState_PeriodActive:
    diag_assert_fail("Unable to begin a new sound device period: Already active");
  case SndDeviceState_Count:
    UNREACHABLE
  }

  const u32 availableFrames = alsa_pcm_available_frames(dev->pcm);
  if (availableFrames < snd_alsa_period_frames) {
    return false; // TODO: Does it make any sense to render less then a full period?
  }
  dev->activeMapping = alsa_pcm_period_map(dev->pcm);
  if (UNLIKELY(!dev->activeMapping)) {
    dev->state = SndDeviceState_Error;
    return false;
  }
  dev->state = SndDeviceState_PeriodActive;
  return true;
}

SndDevicePeriod snd_device_period(SndDevice* dev) {
  diag_assert(dev->state == SndDeviceState_PeriodActive);
  diag_assert(dev->activeMapping != null);

  // TODO: Compute period timestamp.
  return (SndDevicePeriod){
      .time       = time_steady_clock(),
      .frameCount = snd_alsa_period_frames,
      .samples    = dev->activeMapping,
  };
}

void snd_device_end(SndDevice* dev) {
  diag_assert(dev->state == SndDeviceState_PeriodActive);

  if (alsa_pcm_period_commit(dev->pcm)) {
    dev->state = SndDeviceState_Playing;
  } else {
    dev->state = SndDeviceState_Error;
  }
  dev->activeMapping = null;
}
