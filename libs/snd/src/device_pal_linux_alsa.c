#include "core_bits.h"
#include "core_diag.h"
#include "core_dynlib.h"
#include "core_math.h"
#include "core_thread.h"
#include "log_logger.h"
#include "snd_channel.h"

#include "constants_internal.h"
#include "device_internal.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>

/**
 * 'Advanced Linux Sound Architecture' (ALSA) PCM playback sound device (https://alsa-project.org/).
 * For debian based systems: apt install libasound2
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

typedef enum {
  AlsaPcmStream_Playback = 0,
  AlsaPcmStream_Capture  = 1,
} AlsaPcmStream;

typedef enum {
  AlsaPcmAccess_MmapInterleaved    = 0,
  AlsaPcmAccess_MmapNonInterleaved = 1,
  AlsaPcmAccess_MmapComplex        = 2,
  AlsaPcmAccess_RwInterleaved      = 3,
  AlsaPcmAccess_RwNonInterleaved   = 4,
} AlsaPcmAccess;

typedef enum {
  AlsaPcmFormat_S16Le = 2, // Signed 16 bit little endian.
} AlsaPcmFormat;

typedef struct sAlsaPcm         AlsaPcm;
typedef struct sAlsaPcmInfo     AlsaPcmInfo;
typedef struct sAlsaPcmHwParams AlsaPcmHwParams;
typedef int                     AlsaPcmType;
typedef unsigned long           AlsaUFrames;
typedef long                    AlsaSFrames;

typedef void (*AlsaErrorHandler)(
    const char* file, int line, const char* function, int err, const char* fmt, ...);

typedef struct {
  DynLib* asound;
  // clang-format off
  const char* (SYS_DECL* strerror)(int errnum);
  int         (SYS_DECL* lib_error_set_handler)(AlsaErrorHandler);
  int         (SYS_DECL* pcm_open)(AlsaPcm**, const char* name, AlsaPcmStream, int mode);
  int         (SYS_DECL* pcm_close)(AlsaPcm*);
  AlsaPcmType (SYS_DECL* pcm_type)(AlsaPcm*);
  const char* (SYS_DECL* pcm_type_name)(AlsaPcmType);
  int         (SYS_DECL* pcm_prepare)(AlsaPcm*);
  AlsaSFrames (SYS_DECL* pcm_avail_update)(AlsaPcm*);
  AlsaSFrames (SYS_DECL* pcm_writei)(AlsaPcm*, const void* buffer, AlsaUFrames size);
  size_t      (SYS_DECL* pcm_info_sizeof)(void);
  int         (SYS_DECL* pcm_info)(AlsaPcm*, AlsaPcmInfo*);
  int         (SYS_DECL* pcm_info_get_card)(const AlsaPcmInfo*);
  const char* (SYS_DECL* pcm_info_get_id)(const AlsaPcmInfo*);
  size_t      (SYS_DECL* pcm_hw_params_sizeof)(void);
  int         (SYS_DECL* pcm_hw_params_any)(AlsaPcm*, AlsaPcmHwParams*);
  int         (SYS_DECL* pcm_hw_params)(AlsaPcm*, AlsaPcmHwParams*);
  int         (SYS_DECL* pcm_hw_params_get_min_align)(const AlsaPcmHwParams*, AlsaUFrames* val);
  int         (SYS_DECL* pcm_hw_params_get_buffer_size)(const AlsaPcmHwParams*, AlsaUFrames* val);
  int         (SYS_DECL* pcm_hw_params_set_rate_resample)(AlsaPcm*, AlsaPcmHwParams*, unsigned int val);
  int         (SYS_DECL* pcm_hw_params_set_access)(AlsaPcm*, AlsaPcmHwParams*, AlsaPcmAccess val);
  int         (SYS_DECL* pcm_hw_params_set_format)(AlsaPcm*, AlsaPcmHwParams*, AlsaPcmFormat val);
  int         (SYS_DECL* pcm_hw_params_set_channels)(AlsaPcm*, AlsaPcmHwParams*, unsigned int val);
  int         (SYS_DECL* pcm_hw_params_set_rate_near)(AlsaPcm*, AlsaPcmHwParams*, unsigned int* val, int* dir);
  int         (SYS_DECL* pcm_hw_params_set_periods_near)(AlsaPcm*, AlsaPcmHwParams*, unsigned int* val, int* dir);
  int         (SYS_DECL* pcm_hw_params_set_period_size_near)(AlsaPcm*, AlsaPcmHwParams*, AlsaUFrames* val, int* dir);
  // clang-format on
} AlsaLib;

typedef struct {
  u32 periodCount;
  u32 bufferSize; // In frames.
} AlsaPcmConfig;

typedef struct sSndDevice {
  Allocator* alloc;
  AlsaLib    alsa;
  String     id;

  SndDeviceState state;

  AlsaPcm*      pcm;
  AlsaPcmConfig pcmConfig;

  TimeSteady nextPeriodBeginTime;

  u64        underrunCounter;
  TimeSteady underrunLastReportTime;

  i16* renderBuffer;
  u32  renderFrames, renderFramesMax;
} SndDevice;

typedef enum {
  AlsaPcmError_None,
  AlsaPcmError_Underrun, // Device buffer under-run has occurred.
  AlsaPcmError_Unknown,  // Device has encountered an unknown error.
} AlsaPcmError;

typedef struct {
  AlsaPcmError error;
  u32          availableFrames;
} AlsaPcmStatus;

static bool alsa_lib_init(AlsaLib* lib, Allocator* alloc) {
  DynLibResult loadRes = dynlib_load(alloc, string_lit("libasound.so"), &lib->asound);
  if (loadRes != DynLibResult_Success) {
    const String err = dynlib_result_str(loadRes);
    log_w("Failed to load Alsa library ('libasound.so')", log_param("err", fmt_text(err)));
    return false;
  }
  log_i("Alsa library loaded", log_param("path", fmt_path(dynlib_path(lib->asound))));

#define ALSA_LOAD_SYM(_NAME_)                                                                      \
  do {                                                                                             \
    const String symName = string_lit("snd_" #_NAME_);                                             \
    lib->_NAME_          = dynlib_symbol(lib->asound, symName);                                    \
    if (!lib->_NAME_) {                                                                            \
      log_w("Alsa symbol '{}' missing", log_param("sym", fmt_text(symName)));                      \
      return false;                                                                                \
    }                                                                                              \
  } while (false)

  ALSA_LOAD_SYM(strerror);
  ALSA_LOAD_SYM(lib_error_set_handler);
  ALSA_LOAD_SYM(pcm_open);
  ALSA_LOAD_SYM(pcm_close);
  ALSA_LOAD_SYM(pcm_type);
  ALSA_LOAD_SYM(pcm_type_name);
  ALSA_LOAD_SYM(pcm_prepare);
  ALSA_LOAD_SYM(pcm_avail_update);
  ALSA_LOAD_SYM(pcm_writei);
  ALSA_LOAD_SYM(pcm_info_sizeof);
  ALSA_LOAD_SYM(pcm_info);
  ALSA_LOAD_SYM(pcm_info_get_card);
  ALSA_LOAD_SYM(pcm_info_get_id);
  ALSA_LOAD_SYM(pcm_hw_params_sizeof);
  ALSA_LOAD_SYM(pcm_hw_params_any);
  ALSA_LOAD_SYM(pcm_hw_params);
  ALSA_LOAD_SYM(pcm_hw_params_get_min_align);
  ALSA_LOAD_SYM(pcm_hw_params_get_buffer_size);
  ALSA_LOAD_SYM(pcm_hw_params_set_rate_resample);
  ALSA_LOAD_SYM(pcm_hw_params_set_access);
  ALSA_LOAD_SYM(pcm_hw_params_set_format);
  ALSA_LOAD_SYM(pcm_hw_params_set_channels);
  ALSA_LOAD_SYM(pcm_hw_params_set_rate_near);
  ALSA_LOAD_SYM(pcm_hw_params_set_periods_near);
  ALSA_LOAD_SYM(pcm_hw_params_set_period_size_near);

#undef ALSA_LIB_SYM
  return true;
}

static String alsa_error_str(const SndDevice* dev, const int err) {
  return string_from_null_term(dev->alsa.strerror(err));
}

static const SndDevice* g_sndErrorHandlerDev;
static ThreadSpinLock   g_sndErrorHandlerLock;

static void alsa_error_handler(
    const char* file, const i32 line, const char* func, const i32 err, const char* fmt, ...) {

  String errName = string_lit("<unknown>");
  thread_spinlock_lock(&g_sndErrorHandlerLock);
  if (g_sndErrorHandlerDev) {
    errName = alsa_error_str(g_sndErrorHandlerDev, err);
  }
  thread_spinlock_unlock(&g_sndErrorHandlerLock);

  va_list arg;
  va_start(arg, fmt);

  char         msgBuf[128];
  const i32    msgLength = vsnprintf(msgBuf, sizeof(msgBuf), fmt, arg);
  const String msg       = {.ptr = msgBuf, .size = math_clamp_i32(msgLength, 0, sizeof(msgBuf))};

  log_e(
      "Alsa error: {}",
      log_param("msg", fmt_text(msg)),
      log_param("err", fmt_text(errName)),
      log_param("file", fmt_text(string_from_null_term(file))),
      log_param("line", fmt_int(line)),
      log_param("func", fmt_text(string_from_null_term(func))));

  va_end(arg);
}

static void alsa_error_handler_init(SndDevice* dev) {
  thread_spinlock_lock(&g_sndErrorHandlerLock);
  dev->alsa.lib_error_set_handler(&alsa_error_handler);
  if (!g_sndErrorHandlerDev) {
    g_sndErrorHandlerDev = dev;
  }
  thread_spinlock_unlock(&g_sndErrorHandlerLock);
}

static void alsa_error_handler_teardown(SndDevice* dev) {
  thread_spinlock_lock(&g_sndErrorHandlerLock);
  if (g_sndErrorHandlerDev == dev) {
    g_sndErrorHandlerDev = null;
  }
  thread_spinlock_unlock(&g_sndErrorHandlerLock);
}

static bool alsa_pcm_open(SndDevice* dev) {
  const i32 err = dev->alsa.pcm_open(&dev->pcm, snd_alsa_device_name, AlsaPcmStream_Playback, 0);
  if (err < 0) {
    log_e(
        "Failed to open sound-device",
        log_param("name", fmt_text(string_from_null_term(snd_alsa_device_name))),
        log_param("err-code", fmt_int(err)),
        log_param("err", fmt_text(alsa_error_str(dev, err))));
    return false;
  }
  return true;
}

static AlsaPcmInfo* alsa_pcm_info_scratch(SndDevice* dev) {
  diag_assert(dev->pcm);

  AlsaPcmInfo* info = alloc_alloc(g_allocScratch, dev->alsa.pcm_info_sizeof(), sizeof(void*)).ptr;
  const i32    ret  = dev->alsa.pcm_info(dev->pcm, info);
  if (ret < 0) {
    const String errName = alsa_error_str(dev, ret);
    log_e("Failed to retrieve sound-device info", log_param("err", fmt_text(errName)));
    return null;
  }
  return info;
}

static bool alsa_pcm_configure(SndDevice* dev) {
  AlsaPcm* pcm = dev->pcm;
  diag_assert(pcm);

  i32 err = 0;

  // Configure the hardware parameters.
  const usize      hwParamsSize = dev->alsa.pcm_hw_params_sizeof();
  AlsaPcmHwParams* hwParams     = alloc_alloc(g_allocScratch, hwParamsSize, sizeof(void*)).ptr;
  if ((err = dev->alsa.pcm_hw_params_any(pcm, hwParams)) < 0) {
    goto Err;
  }
  if ((err = dev->alsa.pcm_hw_params_set_rate_resample(pcm, hwParams, true)) < 0) {
    goto Err;
  }
  if ((err = dev->alsa.pcm_hw_params_set_access(pcm, hwParams, AlsaPcmAccess_RwInterleaved)) < 0) {
    goto Err;
  }
  if ((err = dev->alsa.pcm_hw_params_set_format(pcm, hwParams, AlsaPcmFormat_S16Le)) < 0) {
    goto Err;
  }
  if ((err = dev->alsa.pcm_hw_params_set_channels(pcm, hwParams, SndChannel_Count)) < 0) {
    goto Err;
  }
  u32 frameRate = snd_frame_rate;
  if ((err = dev->alsa.pcm_hw_params_set_rate_near(pcm, hwParams, &frameRate, 0)) < 0) {
    goto Err;
  }
  if (frameRate != snd_frame_rate) {
    log_e("Sound-device frame-rate not supported");
    goto Err;
  }
  u32 periodCount = snd_alsa_period_desired_count;
  if ((err = dev->alsa.pcm_hw_params_set_periods_near(pcm, hwParams, &periodCount, 0)) < 0) {
    goto Err;
  }
  AlsaUFrames periodSize = snd_alsa_period_frames;
  if ((err = dev->alsa.pcm_hw_params_set_period_size_near(pcm, hwParams, &periodSize, 0)) < 0) {
    goto Err;
  }
  if (periodSize != snd_alsa_period_frames) {
    log_e("Sound-device period-size not supported");
    goto Err;
  }

  // Apply the hardware parameters.
  if ((err = dev->alsa.pcm_hw_params(pcm, hwParams)) < 0) {
    goto Err;
  }

  // Retrieve the config.
  AlsaUFrames bufferSize;
  if ((err = dev->alsa.pcm_hw_params_get_buffer_size(hwParams, &bufferSize)) < 0) {
    goto Err;
  }
  AlsaUFrames minTransferAlign;
  if ((err = dev->alsa.pcm_hw_params_get_min_align(hwParams, &minTransferAlign)) < 0) {
    goto Err;
  }
  if (minTransferAlign > (snd_frame_count_alignment * SndChannel_Count)) {
    log_e("Sound-device requires stronger frame alignment then we support");
    goto Err;
  }
  dev->pcmConfig = (AlsaPcmConfig){
      .periodCount = periodCount,
      .bufferSize  = (u32)bufferSize,
  };
  return true;

Err:;
  const String errName = err < 0 ? alsa_error_str(dev, err) : string_lit("unkown");
  log_e(
      "Failed to setup sound-device",
      log_param("err-code", fmt_int(err)),
      log_param("err", fmt_text(errName)));
  return false;
}

static bool alsa_pcm_prepare(SndDevice* dev) {
  i32 err;
  if (UNLIKELY(err = dev->alsa.pcm_prepare(dev->pcm))) {
    log_e(
        "Failed to prepare sound-device",
        log_param("err-code", fmt_int(err)),
        log_param("err", fmt_text(alsa_error_str(dev, err))));
    return false;
  }
  return true; // Ready for playing.
}

static AlsaPcmStatus alsa_pcm_query(SndDevice* dev) {
  const AlsaSFrames avail = dev->alsa.pcm_avail_update(dev->pcm);
  if (UNLIKELY(avail < 0)) {
    const i32 err = (i32)avail;
    if (err == -EPIPE) {
      return (AlsaPcmStatus){.error = AlsaPcmError_Underrun};
    }
    log_e(
        "Failed to query sound-device",
        log_param("err-code", fmt_int(err)),
        log_param("err", fmt_text(alsa_error_str(dev, (i32)avail))));
    return (AlsaPcmStatus){.error = AlsaPcmError_Unknown};
  }
  return (AlsaPcmStatus){.availableFrames = (u32)avail};
}

static AlsaPcmError alsa_pcm_write(SndDevice* dev, const i16* buf, const u32 bufFrameCount) {
  const AlsaSFrames written = dev->alsa.pcm_writei(dev->pcm, buf, bufFrameCount);
  if (written < 0 || (AlsaUFrames)written != bufFrameCount) {
    const i32 err = (i32)written;
    if (err == -EPIPE) {
      return AlsaPcmError_Underrun;
    }
    log_e(
        "Failed to write to sound-device",
        log_param("err-code", fmt_int(err)),
        log_param("err", fmt_text(alsa_error_str(dev, err))));
    return AlsaPcmError_Unknown;
  }
  return AlsaPcmError_None;
}

static void snd_device_report_underrun(SndDevice* device) {
  ++device->underrunCounter;

  const TimeSteady timeNow = time_steady_clock();
  if ((timeNow - device->underrunLastReportTime) > time_second) {
    log_w("Sound-device buffer underrun", log_param("counter", fmt_int(device->underrunCounter)));
    device->underrunLastReportTime = timeNow;
  }
}

SndDevice* snd_device_create(Allocator* alloc) {
  SndDevice* dev = alloc_alloc_t(alloc, SndDevice);
  *dev           = (SndDevice){.alloc = alloc, .state = SndDeviceState_Error};

  if (!alsa_lib_init(&dev->alsa, alloc)) {
    return dev; // Failed to initialize alsa library.
  }
  alsa_error_handler_init(dev);

  if (!alsa_pcm_open(dev)) {
    return dev; // Failed to open pcm device.
  }
  if (!alsa_pcm_configure(dev)) {
    return dev; // Failed to configure pcm device.
  }

  const AlsaPcmType  type = dev->alsa.pcm_type(dev->pcm);
  const AlsaPcmInfo* info = alsa_pcm_info_scratch(dev);
  const i32          card = info ? dev->alsa.pcm_info_get_card(info) : -1;
  if (info) {
    dev->id = string_maybe_dup(alloc, string_from_null_term(dev->alsa.pcm_info_get_id(info)));
  }
  dev->state = SndDeviceState_Idle;

  dev->renderFramesMax = math_min(dev->pcmConfig.bufferSize, snd_frame_count_max);

  const usize renderBufferSize = dev->renderFramesMax * SndChannel_Count * sizeof(i16);
  dev->renderBuffer = alloc_alloc(alloc, renderBufferSize, snd_frame_sample_alignment).ptr;

  log_i(
      "Alsa sound device created",
      log_param("id", fmt_text(dev->id)),
      log_param("card", fmt_int(card)),
      log_param("type", fmt_text(string_from_null_term(dev->alsa.pcm_type_name(type)))),
      log_param("period-count", fmt_int(dev->pcmConfig.periodCount)),
      log_param("period-frames", fmt_int(snd_alsa_period_frames)),
      log_param("period-time", fmt_duration(snd_alsa_period_time)),
      log_param("device-buffer", fmt_size(dev->pcmConfig.bufferSize)));

  return dev;
}

void snd_device_destroy(SndDevice* dev) {
  if (dev->pcm) {
    dev->alsa.pcm_close(dev->pcm);
  }
  alsa_error_handler_teardown(dev);
  if (dev->alsa.asound) {
    dynlib_destroy(dev->alsa.asound);
  }
  string_maybe_free(dev->alloc, dev->id);
  if (dev->renderBuffer) {
    const usize renderBufferSize = dev->renderFramesMax * SndChannel_Count * sizeof(i16);
    alloc_free(dev->alloc, mem_create(dev->renderBuffer, renderBufferSize));
  }
  alloc_free_t(dev->alloc, dev);

  log_i("Alsa sound device destroyed");
}

String snd_device_id(const SndDevice* dev) {
  return string_is_empty(dev->id) ? string_lit("unknown") : dev->id;
}

String snd_device_backend(const SndDevice* dev) {
  (void)dev;
  return string_lit("alsa");
}

SndDeviceState snd_device_state(const SndDevice* dev) { return dev->state; }

u64 snd_device_underruns(const SndDevice* dev) { return dev->underrunCounter; }

bool snd_device_begin(SndDevice* dev) {
  diag_assert_msg(!dev->renderFrames, "Device rendering already active");

StartPlayingIfIdle:
  if (dev->state == SndDeviceState_Idle) {
    if (alsa_pcm_prepare(dev)) {
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
  const AlsaPcmStatus status = alsa_pcm_query(dev);
  switch (status.error) {
  case AlsaPcmError_None:
    if (!status.availableFrames) {
      return false; // No frames available for rendering.
    }
    dev->renderFrames = math_min(status.availableFrames, dev->renderFramesMax);
    return true; // Frames ready for rendering.
  case AlsaPcmError_Underrun:
    snd_device_report_underrun(dev);
    dev->state = SndDeviceState_Idle; // PCM ran out of samples in the buffer; Restart the playback.
    goto StartPlayingIfIdle;
  case AlsaPcmError_Unknown:
    dev->state = SndDeviceState_Error;
    return false;
  }
  UNREACHABLE;
}

SndDevicePeriod snd_device_period(SndDevice* dev) {
  diag_assert_msg(dev->renderFrames, "Device not currently rendering");
  return (SndDevicePeriod){
      .timeBegin  = dev->nextPeriodBeginTime,
      .frameCount = dev->renderFrames,
      .samples    = dev->renderBuffer,
  };
}

void snd_device_end(SndDevice* dev) {
  diag_assert_msg(dev->renderFrames, "Device not currently rendering");

  switch (alsa_pcm_write(dev, dev->renderBuffer, dev->renderFrames)) {
  case AlsaPcmError_None:
    dev->nextPeriodBeginTime += dev->renderFrames * time_second / snd_frame_rate;
    break;
  case AlsaPcmError_Underrun:
    snd_device_report_underrun(dev);
    dev->state = SndDeviceState_Idle; // Playback stopped due to an underrun.
    break;
  case AlsaPcmError_Unknown:
    dev->state = SndDeviceState_Error;
    break;
  }
  dev->renderFrames = 0;
}
