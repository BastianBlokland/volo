#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_dynlib.h"
#include "core_winutils.h"
#include "log_logger.h"
#include "snd_channel.h"

#include "constants_internal.h"
#include "device_internal.h"

#include <Windows.h>

/**
 * Win32 Multimedia 'WaveOut' sound device implementation.
 *
 * Use a simple double-buffering strategy where we use two periods, one playing on the device and
 * one being recorded.
 */

#define snd_mme_period_count 2
#define snd_mme_period_frames 2048
#define snd_mme_period_samples (snd_mme_period_frames * SndChannel_Count)
#define snd_mme_period_time (snd_mme_period_frames * time_second / snd_frame_rate)

ASSERT(bits_aligned(snd_mme_period_frames, snd_frame_count_alignment), "Invalid sample alignment");
ASSERT(snd_mme_period_frames <= snd_frame_count_max, "FrameCount exceeds maximum");

#define mme_wavemapper_id ((UINT)~0)
#define mme_header_flag_done (1 << 0)

typedef UINT               MmeResult;
typedef struct sMmeWaveOut MmeWaveOut;

// NOTE: Needs to match 'struct tagWAVEOUTCAPSW' from 'mmeapi.h'.
typedef struct {
  WORD    mid;           // Manufacturer ID.
  WORD    pid;           // Product ID.
  UINT    driverVersion; // Version of the driver.
  wchar_t name[32];      // Product name (null terminated string).
  DWORD   formats;       // Formats supported.
  WORD    channels;      // Number of sources supported.
  WORD    reserved1;     // Padding.
  DWORD   support;       // Functionality supported by driver.
} MmeWaveOutCaps;

// NOTE: Needs to match 'struct wavehdr_tag' from 'mmeapi.h'.
typedef struct {
  WORD  formatTag;      // Format type.
  WORD  channels;       // Number of channels (i.e. mono, stereo...).
  DWORD samplesPerSec;  // Sample rate.
  DWORD avgBytesPerSec; // For buffer estimation.
  WORD  blockAlign;     // Block size of data.
  WORD  bitsPerSample;  // Number of bits per sample of mono data .
  WORD  size;           // Count in bytes of the size of extra information (after size).
} MmeWaveFormat;

// NOTE: Needs to match 'struct wavehdr_tag' from 'mmeapi.h'.
typedef struct sMmeWaveHeader {
  char*                  data;          // Pointer to locked data buffer.
  DWORD                  bufferLength;  // Length of data buffer.
  DWORD                  bytesRecorded; // Used for input only.
  DWORD_PTR              user;          // For client's use.
  DWORD                  flags;         // Assorted flags (see defines).
  DWORD                  loops;         // Loop control counter.
  struct sMmeWaveHeader* nex;           // Reserved for driver.
  DWORD_PTR              reserved;      // Reserved for driver.
} MmeWaveHeader;

typedef struct {
  DynLib* winmm;
  // clang-format off
  MmeResult (SYS_DECL* waveOutGetErrorTextW)(MmeResult, wchar_t* buffer, UINT bufferSize);
  MmeResult (SYS_DECL* waveOutGetDevCapsW)(UINT deviceId, MmeWaveOutCaps*, UINT capsStructSize);
  MmeResult (SYS_DECL* waveOutOpen)(MmeWaveOut**, UINT deviceId, const MmeWaveFormat*, DWORD_PTR callback, DWORD_PTR instance, DWORD flags);
  MmeResult (SYS_DECL* waveOutClose)(MmeWaveOut*);
  MmeResult (SYS_DECL* waveOutReset)(MmeWaveOut*);
  MmeResult (SYS_DECL* waveOutPrepareHeader)(MmeWaveOut*, MmeWaveHeader*, UINT headerStructSize);
  MmeResult (SYS_DECL* waveOutUnprepareHeader)(MmeWaveOut*, MmeWaveHeader*, UINT headerStructSize);
  MmeResult (SYS_DECL* waveOutWrite)(MmeWaveOut*, MmeWaveHeader*, UINT headerStructSize);
  // clang-format on
} MmeLib;

typedef struct sSndDevice {
  Allocator* alloc;
  MmeLib     mme;
  String     id;

  MmeWaveOut* pcm;

  SndDeviceState state : 8;
  u8             activePeriod;
  TimeSteady     nextPeriodBeginTime;

  u64        underrunCounter;
  TimeSteady underrunLastReportTime;

  MmeWaveHeader periodHeaders[snd_mme_period_count];

  ALIGNAS(snd_frame_sample_alignment)
  i16 periodBuffer[snd_mme_period_samples * snd_mme_period_count];
} SndDevice;

static bool mme_lib_init(MmeLib* lib, Allocator* alloc) {
  DynLibResult loadRes = dynlib_load(alloc, string_lit("Winmm.dll"), &lib->winmm);
  if (loadRes != DynLibResult_Success) {
    const String err = dynlib_result_str(loadRes);
    log_w("Failed to load Win32 MME library ('Winmm.dll')", log_param("err", fmt_text(err)));
    return false;
  }
  log_i("MME library loaded", log_param("path", fmt_path(dynlib_path(lib->winmm))));

#define MME_LOAD_SYM(_NAME_)                                                                       \
  do {                                                                                             \
    lib->_NAME_ = dynlib_symbol(lib->winmm, string_lit(#_NAME_));                                  \
    if (!lib->_NAME_) {                                                                            \
      log_w("MME symbol '{}' missing", log_param("sym", fmt_text(string_lit(#_NAME_))));           \
      return false;                                                                                \
    }                                                                                              \
  } while (false)

  MME_LOAD_SYM(waveOutGetErrorTextW);
  MME_LOAD_SYM(waveOutGetDevCapsW);
  MME_LOAD_SYM(waveOutOpen);
  MME_LOAD_SYM(waveOutClose);
  MME_LOAD_SYM(waveOutReset);
  MME_LOAD_SYM(waveOutPrepareHeader);
  MME_LOAD_SYM(waveOutUnprepareHeader);
  MME_LOAD_SYM(waveOutWrite);

#undef MME_LOAD_SYM
  return true;
}

static String mme_result_str_scratch(SndDevice* dev, const MmeResult result) {
  wchar_t buffer[256];
  if (dev->mme.waveOutGetErrorTextW(result, buffer, array_elems(buffer)) != 0) {
    return string_lit("Unknown error occurred");
  }
  return winutils_from_widestr_scratch(buffer, wcslen(buffer));
}

static bool mme_pcm_open(SndDevice* dev) {
  const MmeWaveFormat format = {
      .formatTag      = 1 /* PCM */,
      .channels       = SndChannel_Count,
      .samplesPerSec  = snd_frame_rate,
      .avgBytesPerSec = snd_frame_rate * SndChannel_Count * snd_frame_sample_depth / 8,
      .blockAlign     = SndChannel_Count * snd_frame_sample_depth / 8,
      .bitsPerSample  = snd_frame_sample_depth,
  };
  const MmeResult result = dev->mme.waveOutOpen(&dev->pcm, mme_wavemapper_id, &format, 0, 0, 0);
  if (UNLIKELY(result != 0)) {
    log_e(
        "Failed to open sound-device",
        log_param("err-code", fmt_int(result)),
        log_param("err", fmt_text(mme_result_str_scratch(dev, result))));

    dev->pcm = null;
    return false;
  }
  return true;
}

static void mme_pcm_close(SndDevice* dev) {
  const MmeResult result = dev->mme.waveOutClose(dev->pcm);
  if (UNLIKELY(result != 0)) {
    log_e(
        "Failed to close sound-device",
        log_param("err-code", fmt_int(result)),
        log_param("err", fmt_text(mme_result_str_scratch(dev, result))));
  }
}

static void mme_pcm_reset(SndDevice* dev) {
  const MmeResult result = dev->mme.waveOutReset(dev->pcm);
  if (UNLIKELY(result != 0)) {
    log_e(
        "Failed to reset sound-device",
        log_param("err-code", fmt_int(result)),
        log_param("err", fmt_text(mme_result_str_scratch(dev, result))));
  }
}

static bool mme_pcm_write(SndDevice* dev, MmeWaveHeader* periodHeader) {
  const MmeResult result = dev->mme.waveOutWrite(dev->pcm, periodHeader, sizeof(MmeWaveHeader));
  if (UNLIKELY(result != 0)) {
    log_e(
        "Failed to write to sound-device",
        log_param("err-code", fmt_int(result)),
        log_param("err", fmt_text(mme_result_str_scratch(dev, result))));
    return false;
  }
  return true;
}

static String mme_pcm_name_scratch(SndDevice* dev) {
  (void)dev; // TODO: Get the name of the specified device instead of hardcoding wavemapper.
  MmeWaveOutCaps  caps;
  const MmeResult result = dev->mme.waveOutGetDevCapsW(mme_wavemapper_id, &caps, sizeof(caps));
  if (UNLIKELY(result != 0)) {
    log_e(
        "Failed get capabilities of sound-device",
        log_param("err-code", fmt_int(result)),
        log_param("err", fmt_text(mme_result_str_scratch(dev, result))));
    return string_lit("<error>");
  }
  const usize pcmNameChars = wcslen(caps.name);
  if (UNLIKELY(!pcmNameChars)) {
    return string_empty;
  }
  return winutils_from_widestr_scratch(caps.name, pcmNameChars);
}

SndDevice* snd_device_create(Allocator* alloc) {
  SndDevice* dev = alloc_alloc_t(alloc, SndDevice);
  *dev = (SndDevice){.alloc = alloc, .state = SndDeviceState_Error, .activePeriod = sentinel_u8};

  if (!mme_lib_init(&dev->mme, alloc)) {
    return dev; // Failed to initialize Win32 Multimedia library.
  }
  if (!mme_pcm_open(dev)) {
    return dev; // Failed to open pcm device.
  }
  dev->id    = string_maybe_dup(alloc, mme_pcm_name_scratch(dev));
  dev->state = SndDeviceState_Idle;

  // Initialize the period buffers.
  for (u32 period = 0; period != snd_mme_period_count; ++period) {
    MmeWaveHeader* header = &dev->periodHeaders[period];
    header->data          = (void*)&dev->periodBuffer[snd_mme_period_samples * period];
    header->bufferLength  = snd_mme_period_samples * snd_frame_sample_depth / 8;
    if (UNLIKELY(dev->mme.waveOutPrepareHeader(dev->pcm, header, sizeof(MmeWaveHeader)) != 0)) {
      dev->state = SndDeviceState_Error;
    }
    header->flags |= mme_header_flag_done; // Mark the period as ready for use.
  }

  log_i(
      "MME sound device created",
      log_param("id", fmt_text(dev->id)),
      log_param("period-count", fmt_int(snd_mme_period_count)),
      log_param("period-frames", fmt_int(snd_mme_period_frames)),
      log_param("period-time", fmt_duration(snd_mme_period_time)));

  return dev;
}

static bool snd_device_detect_underrun(SndDevice* device) {
  bool anyPeriodBusy = false;
  for (u32 period = 0; period != snd_mme_period_count; ++period) {
    if (!(device->periodHeaders[period].flags & mme_header_flag_done)) {
      anyPeriodBusy = true;
    }
  }
  return !anyPeriodBusy;
}

static void snd_device_report_underrun(SndDevice* device) {
  ++device->underrunCounter;

  const TimeSteady timeNow = time_steady_clock();
  if ((timeNow - device->underrunLastReportTime) > time_second) {
    log_w("Sound-device buffer underrun", log_param("counter", fmt_int(device->underrunCounter)));
    device->underrunLastReportTime = timeNow;
  }
}

void snd_device_destroy(SndDevice* dev) {
  if (dev->pcm) {
    if (dev->state == SndDeviceState_Playing) {
      mme_pcm_reset(dev);
    }
    for (u32 period = 0; period != snd_mme_period_count; ++period) {
      MmeWaveHeader* periodHeader = &dev->periodHeaders[period];
      dev->mme.waveOutUnprepareHeader(dev->pcm, periodHeader, sizeof(MmeWaveHeader));
    }
    mme_pcm_close(dev);
  }
  if (dev->mme.winmm) {
    dynlib_destroy(dev->mme.winmm);
  }
  string_maybe_free(dev->alloc, dev->id);
  alloc_free_t(dev->alloc, dev);

  log_i("MME sound device destroyed");
}

String snd_device_id(const SndDevice* dev) {
  if (string_is_empty(dev->id)) {
    return dev->state == SndDeviceState_Error ? string_lit("<error>") : string_lit("<unknown>");
  }
  return dev->id;
}

String snd_device_backend(const SndDevice* dev) {
  (void)dev;
  return string_lit("mme-waveout");
}

SndDeviceState snd_device_state(const SndDevice* dev) { return dev->state; }

u64 snd_device_underruns(const SndDevice* dev) { return dev->underrunCounter; }

bool snd_device_begin(SndDevice* dev) {
  diag_assert_msg(sentinel_check(dev->activePeriod), "Device rendering already active");

  if (UNLIKELY(dev->state == SndDeviceState_Error)) {
    return false; // Device is in an unrecoverable error state.
  }

  // Check if the device has underrun.
  if (LIKELY(dev->state == SndDeviceState_Playing)) {
    // Detect if the device has underrun.
    if (snd_device_detect_underrun(dev)) {
      snd_device_report_underrun(dev);
      dev->state = SndDeviceState_Idle;
    }
  }

  // Find a period that is ready to be rendered.
  for (u32 period = 0; period != snd_mme_period_count; ++period) {
    if (dev->periodHeaders[period].flags & mme_header_flag_done) {
      // Start playback if we're not playing yet.
      if (UNLIKELY(dev->state == SndDeviceState_Idle)) {
        dev->nextPeriodBeginTime = time_steady_clock();
        dev->state               = SndDeviceState_Playing;
      }
      dev->activePeriod = period;
      return true; // Period can be rendered.
    }
  }

  return false; // No period available for rendering.
}

SndDevicePeriod snd_device_period(SndDevice* dev) {
  diag_assert_msg(!sentinel_check(dev->activePeriod), "Device not currently rendering");
  return (SndDevicePeriod){
      .timeBegin  = dev->nextPeriodBeginTime,
      .frameCount = snd_mme_period_frames,
      .samples    = &dev->periodBuffer[dev->activePeriod * snd_mme_period_samples],
  };
}

void snd_device_end(SndDevice* dev) {
  diag_assert_msg(!sentinel_check(dev->activePeriod), "Device not currently rendering");

  if (UNLIKELY(mme_pcm_write(dev, &dev->periodHeaders[dev->activePeriod]))) {
    dev->nextPeriodBeginTime += snd_mme_period_time;

    // Detect if we where too late in writing an additional period.
    if (snd_device_detect_underrun(dev)) {
      snd_device_report_underrun(dev);
      dev->state = SndDeviceState_Idle;
    }
  } else {
    mme_pcm_reset(dev);
    dev->state = SndDeviceState_Error;
  }
  dev->activePeriod = sentinel_u8;
}
