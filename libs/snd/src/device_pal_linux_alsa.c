#include "core_bits.h"
#include "core_diag.h"
#include "core_dynlib.h"
#include "core_math.h"
#include "core_thread.h"
#include "log_logger.h"
#include "snd_channel.h"

#include "constants_internal.h"
#include "device_internal.h"

#include <alsa/asoundlib.h>

/**
 * Alsa PCM playback sound device implementation.
 *
 * Use a simple double-buffering strategy where we use (at least) two periods, one playing on the
 * device and one being recorded.
 */

#define snd_alsa_device_name "default"
#define snd_alsa_period_desired_count 2
#define snd_alsa_period_frames 2048
#define snd_alsa_period_samples (snd_alsa_period_frames * SndChannel_Count)
#define snd_alsa_period_time (snd_alsa_period_frames * time_second / snd_frame_rate)

ASSERT(bits_aligned(snd_alsa_period_frames, snd_frame_count_alignment), "Invalid sample alignment");
ASSERT(snd_alsa_period_frames <= snd_frame_count_max, "FrameCount exceeds maximum");

typedef struct {
  bool valid;
  u32  periodCount;
  u32  bufferSize;
} AlsaPcmConfig;

typedef enum {
  SndDeviceFlags_Rendering = 1 << 0,
} SndDeviceFlags;

typedef struct sSndDevice {
  Allocator* alloc;
  DynLib*    asoundLib;

  String id;

  SndDeviceState state : 8;
  SndDeviceFlags flags : 8;

  snd_pcm_t*    pcm;
  AlsaPcmConfig pcmConfig;

  TimeSteady nextPeriodBeginTime;

  u64        underrunCounter;
  TimeSteady underrunLastReportTime;

  /**
   * Buffer for rendering the period samples into.
   *
   * TODO: We can avoid copying from our rendering-buffer to the device buffer if the device
   * supports mmap-ing the buffer, however not all devices support this so we need to keep the
   * copying path as a fallback.
   */
  ALIGNAS(snd_frame_sample_alignment)
  i16 periodRenderingBuffer[snd_alsa_period_samples];
} SndDevice;

typedef enum {
  AlsaPcmStatus_Underrun, // Device buffer under-run has occurred.
  AlsaPcmStatus_Busy,     // No period is available for recording.
  AlsaPcmStatus_Ready,    // A period is available for recording.
  AlsaPcmStatus_Error,    // Device has encountered an error.
} AlsaPcmStatus;

typedef enum {
  AlsaPcmWriteResult_Success,
  AlsaPcmWriteResult_Underrun, // Device buffer under-run was detected while writing.
  AlsaPcmWriteResult_Error,
} AlsaPcmWriteResult;

static String alsa_error_str(const int err) { return string_from_null_term(snd_strerror(err)); }

static void alsa_error_handler(
    const char* file, const i32 line, const char* func, const i32 err, const char* fmt, ...) {

  va_list arg;
  va_start(arg, fmt);

  char         msgBuf[128];
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
  const i32  err = snd_pcm_open(&pcm, snd_alsa_device_name, SND_PCM_STREAM_PLAYBACK, 0);
  if (err < 0) {
    log_e(
        "Failed to open sound-device",
        log_param("name", fmt_text(string_from_null_term(snd_alsa_device_name))),
        log_param("err-code", fmt_int(err)),
        log_param("err", fmt_text(alsa_error_str(err))));
    return null;
  }
  return pcm;
}

static snd_pcm_info_t* alsa_pcm_info_scratch(snd_pcm_t* pcm) {
  snd_pcm_info_t* info = alloc_alloc(g_alloc_scratch, snd_pcm_info_sizeof(), sizeof(void*)).ptr;
  const i32       ret  = snd_pcm_info(pcm, info);
  if (ret < 0) {
    log_e("Failed to retrieve sound-device info", log_param("err", fmt_text(alsa_error_str(ret))));
    return null;
  }
  return info;
}

static AlsaPcmConfig alsa_pcm_initialize(snd_pcm_t* pcm) {
  i32 err = 0;

  // Configure the hardware parameters.
  const usize          hwParamsSize = snd_pcm_hw_params_sizeof();
  snd_pcm_hw_params_t* hwParams     = alloc_alloc(g_alloc_scratch, hwParamsSize, sizeof(void*)).ptr;
  if ((err = snd_pcm_hw_params_any(pcm, hwParams)) < 0) {
    goto Err;
  }
  if ((err = snd_pcm_hw_params_set_rate_resample(pcm, hwParams, true)) < 0) {
    goto Err;
  }
  if ((err = snd_pcm_hw_params_set_access(pcm, hwParams, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
    goto Err;
  }
  if ((err = snd_pcm_hw_params_set_format(pcm, hwParams, SND_PCM_FORMAT_S16_LE)) < 0) {
    goto Err;
  }
  if ((err = snd_pcm_hw_params_set_channels(pcm, hwParams, SndChannel_Count)) < 0) {
    goto Err;
  }
  u32 frameRate = snd_frame_rate;
  if ((err = snd_pcm_hw_params_set_rate_near(pcm, hwParams, &frameRate, 0)) < 0) {
    goto Err;
  }
  if (frameRate != snd_frame_rate) {
    log_e("Sound-device frame-rate not supported");
    goto Err;
  }
  u32 periodCount = snd_alsa_period_desired_count;
  if ((err = snd_pcm_hw_params_set_periods_near(pcm, hwParams, &periodCount, 0)) < 0) {
    goto Err;
  }
  snd_pcm_uframes_t periodSize = snd_alsa_period_frames;
  if ((err = snd_pcm_hw_params_set_period_size_near(pcm, hwParams, &periodSize, 0)) < 0) {
    goto Err;
  }
  if (periodSize != snd_alsa_period_frames) {
    log_e("Sound-device period-size not supported");
    goto Err;
  }

  // Apply the hardware parameters.
  if ((err = snd_pcm_hw_params(pcm, hwParams)) < 0) {
    goto Err;
  }

  // Retrieve the config.
  snd_pcm_uframes_t bufferSize;
  if ((err = snd_pcm_hw_params_get_buffer_size(hwParams, &bufferSize)) < 0) {
    goto Err;
  }
  snd_pcm_uframes_t minTransferAlign;
  if ((err = snd_pcm_hw_params_get_min_align(hwParams, &minTransferAlign)) < 0) {
    goto Err;
  }
  if (minTransferAlign > (snd_frame_count_alignment * SndChannel_Count)) {
    log_e("Sound-device requires stronger frame alignment then we support");
    goto Err;
  }
  return (AlsaPcmConfig){
      .valid       = true,
      .periodCount = periodCount,
      .bufferSize  = (u32)bufferSize,
  };

Err:;
  const String errName = err < 0 ? alsa_error_str(err) : string_lit("unkown");
  log_e(
      "Failed to setup sound-device",
      log_param("err-code", fmt_int(err)),
      log_param("err", fmt_text(errName)));
  return (AlsaPcmConfig){0};
}

static bool alsa_pcm_prepare(snd_pcm_t* pcm) {
  i32 err;
  if (UNLIKELY(err = snd_pcm_prepare(pcm))) {
    log_e(
        "Failed to prepare sound-device",
        log_param("err-code", fmt_int(err)),
        log_param("err", fmt_text(alsa_error_str(err))));
    return false;
  }
  return true; // Ready for playing.
}

static AlsaPcmStatus alsa_pcm_query(snd_pcm_t* pcm) {
  const snd_pcm_sframes_t avail = snd_pcm_avail_update(pcm);
  if (UNLIKELY(avail < 0)) {
    const i32 err = (i32)avail;
    if (err == -EPIPE) {
      return AlsaPcmStatus_Underrun;
    }
    log_e(
        "Failed to query sound-device",
        log_param("err-code", fmt_int(err)),
        log_param("err", fmt_text(alsa_error_str((i32)avail))));
    return AlsaPcmStatus_Error;
  }
  return avail < snd_alsa_period_frames ? AlsaPcmStatus_Busy : AlsaPcmStatus_Ready;
}

static AlsaPcmWriteResult
alsa_pcm_write(snd_pcm_t* pcm, i16 buf[PARAM_ARRAY_SIZE(snd_alsa_period_samples)]) {
  const snd_pcm_sframes_t written = snd_pcm_writei(pcm, buf, snd_alsa_period_frames);
  if (written < 0 || (snd_pcm_uframes_t)written != snd_alsa_period_frames) {
    const i32 err = (i32)written;
    if (err == -EPIPE) {
      return AlsaPcmWriteResult_Underrun;
    }
    log_e(
        "Failed to write to sound-device",
        log_param("err-code", fmt_int(err)),
        log_param("err", fmt_text(alsa_error_str(err))));
    return AlsaPcmWriteResult_Error;
  }
  return AlsaPcmWriteResult_Success;
}

static void snd_device_report_underrun(SndDevice* device) {
  ++device->underrunCounter;

  const TimeSteady timeNow = time_steady_clock();
  if ((timeNow - device->underrunLastReportTime) > time_second) {
    log_d("Sound-device buffer underrun", log_param("counter", fmt_int(device->underrunCounter)));
    device->underrunLastReportTime = timeNow;
  }
}

static SndDevice* snd_device_create_error(Allocator* alloc) {
  SndDevice* dev = alloc_alloc_t(alloc, SndDevice);

  *dev = (SndDevice){
      .alloc = alloc,
      .id    = string_maybe_dup(alloc, string_lit("<error>")),
      .state = SndDeviceState_Error,
  };

  return dev;
}

SndDevice* snd_device_create(Allocator* alloc) {
  DynLib*      asoundLib;
  DynLibResult asoundRes = dynlib_load(alloc, string_lit("libasound.so"), &asoundLib);
  if (asoundRes != DynLibResult_Success) {
    const String err = dynlib_result_str(asoundRes);
    log_w("Failed to Alsa library ('libasound.so')", log_param("err", fmt_text(err)));
    return snd_device_create_error(alloc);
  }
  log_i("Alsa library loaded", log_param("path", fmt_path(dynlib_path(asoundLib))));

  alsa_init();

  snd_pcm_t*    pcm       = alsa_pcm_open();
  AlsaPcmConfig pcmConfig = {0};
  if (pcm) {
    pcmConfig = alsa_pcm_initialize(pcm);
  }

  String id;
  if (pcmConfig.valid) {
    const snd_pcm_type_t  type = snd_pcm_type(pcm);
    const snd_pcm_info_t* info = alsa_pcm_info_scratch(pcm);
    const i32             card = info ? snd_pcm_info_get_card(info) : -1;
    id = info ? string_from_null_term(snd_pcm_info_get_id(info)) : string_lit("<unknown>");

    log_i(
        "Alsa sound device created",
        log_param("id", fmt_text(id)),
        log_param("card", fmt_int(card)),
        log_param("type", fmt_text(string_from_null_term(snd_pcm_type_name(type)))),
        log_param("period-count", fmt_int(pcmConfig.periodCount)),
        log_param("period-frames", fmt_int(snd_alsa_period_frames)),
        log_param("period-time", fmt_duration(snd_alsa_period_time)),
        log_param("device-buffer", fmt_size(pcmConfig.bufferSize)));
  } else {
    id = string_lit("<error>");
  }

  SndDevice* dev = alloc_alloc_t(alloc, SndDevice);
  *dev           = (SndDevice){
                .alloc     = alloc,
                .asoundLib = asoundLib,
                .id        = string_maybe_dup(alloc, id),
                .pcm       = pcm,
                .pcmConfig = pcmConfig,
                .state     = pcmConfig.valid ? SndDeviceState_Idle : SndDeviceState_Error,
  };
  return dev;
}

void snd_device_destroy(SndDevice* dev) {
  if (dev->asoundLib) {
    dynlib_destroy(dev->asoundLib);
  }
  if (dev->pcm) {
    snd_pcm_close(dev->pcm);
  }
  string_maybe_free(dev->alloc, dev->id);
  alloc_free_t(dev->alloc, dev);

  log_i("Alsa sound device destroyed");
}

String snd_device_id(const SndDevice* dev) { return dev->id; }

String snd_device_backend(const SndDevice* dev) {
  (void)dev;
  return string_lit("alsa");
}

SndDeviceState snd_device_state(const SndDevice* dev) { return dev->state; }

u64 snd_device_underruns(const SndDevice* dev) { return dev->underrunCounter; }

bool snd_device_begin(SndDevice* dev) {
  diag_assert_msg(!(dev->flags & SndDeviceFlags_Rendering), "Device rendering already active");

StartPlayingIfIdle:
  if (dev->state == SndDeviceState_Idle) {
    if (alsa_pcm_prepare(dev->pcm)) {
      dev->nextPeriodBeginTime = time_steady_clock();
      dev->state               = SndDeviceState_Playing;
    } else {
      dev->state = SndDeviceState_Error;
    }
  }

  if (UNLIKELY(dev->state == SndDeviceState_Error)) {
    return false; // Device is in an unrecoverable error state.
  }

  // Query the device-status to check if there's a period ready for rendering.
  switch (alsa_pcm_query(dev->pcm)) {
  case AlsaPcmStatus_Underrun:
    snd_device_report_underrun(dev);
    dev->state = SndDeviceState_Idle; // PCM ran out of samples in the buffer; Restart the playback.
    goto StartPlayingIfIdle;
  case AlsaPcmStatus_Busy:
    return false; // No period available for rendering.
  case AlsaPcmStatus_Ready:
    dev->flags |= SndDeviceFlags_Rendering;
    return true; // Period can be rendered.
  case AlsaPcmStatus_Error:
    dev->state = SndDeviceState_Error;
    return false;
  }
  UNREACHABLE;
}

SndDevicePeriod snd_device_period(SndDevice* dev) {
  diag_assert_msg(dev->flags & SndDeviceFlags_Rendering, "Device not currently rendering");
  return (SndDevicePeriod){
      .timeBegin  = dev->nextPeriodBeginTime,
      .frameCount = snd_alsa_period_frames,
      .samples    = dev->periodRenderingBuffer,
  };
}

void snd_device_end(SndDevice* dev) {
  diag_assert_msg(dev->flags & SndDeviceFlags_Rendering, "Device not currently rendering");

  switch (alsa_pcm_write(dev->pcm, dev->periodRenderingBuffer)) {
  case AlsaPcmWriteResult_Success:
    dev->nextPeriodBeginTime += snd_alsa_period_time;
    break;
  case AlsaPcmWriteResult_Underrun:
    snd_device_report_underrun(dev);
    dev->state = SndDeviceState_Idle; // Playback stopped due to an underrun.
    break;
  case AlsaPcmWriteResult_Error:
    dev->state = SndDeviceState_Error;
    break;
  }
  dev->flags &= ~SndDeviceFlags_Rendering;
}
