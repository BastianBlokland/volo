#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_winutils.h"
#include "log_logger.h"
#include "snd_channel.h"

#include "constants_internal.h"
#include "device_internal.h"

#include <Windows.h>
#include <mmeapi.h>

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

typedef struct sSndDevice {
  Allocator* alloc;
  String     id;
  HWAVEOUT   pcm;

  SndDeviceState state : 8;
  u8             activePeriod;
  TimeSteady     nextPeriodBeginTime;

  u64        underrunCounter;
  TimeSteady underrunLastReportTime;

  WAVEHDR periodHeaders[snd_mme_period_count];

  ALIGNAS(snd_frame_sample_alignment)
  i16 periodBuffer[snd_mme_period_samples * snd_mme_period_count];
} SndDevice;

static String mme_result_str_scratch(const MMRESULT result) {
  wchar_t buffer[MAXERRORLENGTH];
  if (waveOutGetErrorText(result, buffer, array_elems(buffer)) != MMSYSERR_NOERROR) {
    return string_lit("Unknown error occurred");
  }
  return winutils_from_widestr_scratch(buffer, wcslen(buffer));
}

static HWAVEOUT mme_pcm_open() {
  const WAVEFORMATEX format = {
      .wFormatTag      = WAVE_FORMAT_PCM,
      .nChannels       = SndChannel_Count,
      .nSamplesPerSec  = snd_frame_rate,
      .nAvgBytesPerSec = snd_frame_rate * SndChannel_Count * snd_frame_sample_depth / 8,
      .nBlockAlign     = SndChannel_Count * snd_frame_sample_depth / 8,
      .wBitsPerSample  = snd_frame_sample_depth,
  };
  HWAVEOUT       device;
  const MMRESULT result = waveOutOpen(&device, WAVE_MAPPER, &format, null, null, CALLBACK_NULL);
  if (UNLIKELY(result != MMSYSERR_NOERROR)) {
    log_e(
        "Failed to open sound-device",
        log_param("err-code", fmt_int(result)),
        log_param("err", fmt_text(mme_result_str_scratch(result))));

    return INVALID_HANDLE_VALUE;
  }
  return device;
}

static void mme_pcm_close(HWAVEOUT pcm) {
  const MMRESULT result = waveOutClose(pcm);
  if (UNLIKELY(result != MMSYSERR_NOERROR)) {
    log_e(
        "Failed to close sound-device",
        log_param("err-code", fmt_int(result)),
        log_param("err", fmt_text(mme_result_str_scratch(result))));
  }
}

static void mme_pcm_reset(HWAVEOUT pcm) {
  const MMRESULT result = waveOutReset(pcm);
  if (UNLIKELY(result != MMSYSERR_NOERROR)) {
    log_e(
        "Failed to reset sound-device",
        log_param("err-code", fmt_int(result)),
        log_param("err", fmt_text(mme_result_str_scratch(result))));
  }
}

static bool mme_pcm_write(HWAVEOUT pcm, WAVEHDR* periodHeader) {
  const MMRESULT result = waveOutWrite(pcm, periodHeader, sizeof(WAVEHDR));
  if (UNLIKELY(result != MMSYSERR_NOERROR)) {
    log_e(
        "Failed to write to sound-device",
        log_param("err-code", fmt_int(result)),
        log_param("err", fmt_text(mme_result_str_scratch(result))));
    return false;
  }
  return true;
}

static String mme_pcm_name_scratch(HWAVEOUT pcm) {
  (void)pcm; // TODO: Get the name of the specified device instead of hardcoding 'WAVE_MAPPER'.
  WAVEOUTCAPS    capabilities;
  const MMRESULT result = waveOutGetDevCaps(WAVE_MAPPER, &capabilities, sizeof(WAVEOUTCAPS));
  if (UNLIKELY(result != MMSYSERR_NOERROR)) {
    log_e(
        "Failed get capabilities of sound-device",
        log_param("err-code", fmt_int(result)),
        log_param("err", fmt_text(mme_result_str_scratch(result))));
    return string_lit("<error>");
  }
  const usize pcmNameChars = wcslen(capabilities.szPname);
  if (UNLIKELY(!pcmNameChars)) {
    return string_empty;
  }
  return winutils_from_widestr_scratch(capabilities.szPname, pcmNameChars);
}

SndDevice* snd_device_create(Allocator* alloc) {
  HWAVEOUT pcm = mme_pcm_open();

  String id;
  if (pcm != INVALID_HANDLE_VALUE) {
    id = mme_pcm_name_scratch(pcm);

    log_i(
        "MME sound device created",
        log_param("id", fmt_text(id)),
        log_param("period-count", fmt_int(snd_mme_period_count)),
        log_param("period-frames", fmt_int(snd_mme_period_frames)),
        log_param("period-time", fmt_duration(snd_mme_period_time)));
  } else {
    id = string_lit("<error>");
  }

  SndDevice* dev = alloc_alloc_t(alloc, SndDevice);
  *dev           = (SndDevice){
      .alloc        = alloc,
      .id           = string_maybe_dup(alloc, id),
      .pcm          = pcm,
      .state        = pcm == INVALID_HANDLE_VALUE ? SndDeviceState_Error : SndDeviceState_Idle,
      .activePeriod = sentinel_u8,
  };

  if (pcm != INVALID_HANDLE_VALUE) {
    // Initialize the period buffers.
    for (u32 period = 0; period != snd_mme_period_count; ++period) {
      WAVEHDR* periodHeader        = &dev->periodHeaders[period];
      periodHeader->lpData         = (void*)&dev->periodBuffer[snd_mme_period_samples * period];
      periodHeader->dwBufferLength = snd_mme_period_samples * snd_frame_sample_depth / 8;
      if (UNLIKELY(waveOutPrepareHeader(pcm, periodHeader, sizeof(WAVEHDR)) != MMSYSERR_NOERROR)) {
        dev->state = SndDeviceState_Error;
      }
      periodHeader->dwFlags |= WHDR_DONE; // Mark the period as ready for use.
    }
  }

  return dev;
}

static void snd_device_report_underrun(SndDevice* device) {
  ++device->underrunCounter;

  const TimeSteady timeNow = time_steady_clock();
  if ((timeNow - device->underrunLastReportTime) > time_second) {
    log_d("Sound-device buffer underrun", log_param("counter", fmt_int(device->underrunCounter)));
    device->underrunLastReportTime = timeNow;
  }
}

void snd_device_destroy(SndDevice* dev) {
  if (dev->pcm != INVALID_HANDLE_VALUE) {
    if (dev->state == SndDeviceState_Playing) {
      mme_pcm_reset(dev->pcm);
    }
    for (u32 period = 0; period != snd_mme_period_count; ++period) {
      WAVEHDR* periodHeader = &dev->periodHeaders[period];
      waveOutUnprepareHeader(dev->pcm, periodHeader, sizeof(WAVEHDR));
    }
    mme_pcm_close(dev->pcm);
  }
  string_maybe_free(dev->alloc, dev->id);
  alloc_free_t(dev->alloc, dev);

  log_i("MME sound device destroyed");
}

String snd_device_id(const SndDevice* dev) { return dev->id; }

SndDeviceState snd_device_state(const SndDevice* dev) { return dev->state; }

u64 snd_device_underruns(const SndDevice* dev) { return dev->underrunCounter; }

bool snd_device_begin(SndDevice* dev) {
  diag_assert_msg(sentinel_check(dev->activePeriod), "Device rendering already active");

  if (UNLIKELY(dev->state == SndDeviceState_Error)) {
    return false; // Device is in an unrecoverable error state.
  }

  // Check if the device has underrun.
  if (LIKELY(dev->state == SndDeviceState_Playing)) {
    bool anyPeriodBusy = false;
    for (u32 period = 0; period != snd_mme_period_count; ++period) {
      if (!(dev->periodHeaders[period].dwFlags & WHDR_DONE)) {
        anyPeriodBusy = true;
      }
    }
    if (!anyPeriodBusy) {
      snd_device_report_underrun(dev);
      dev->state = SndDeviceState_Idle;
    }
  }

  // Find a period that is ready to be rendered.
  for (u32 period = 0; period != snd_mme_period_count; ++period) {
    if (dev->periodHeaders[period].dwFlags & WHDR_DONE) {
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

  if (UNLIKELY(mme_pcm_write(dev->pcm, &dev->periodHeaders[dev->activePeriod]))) {
    dev->nextPeriodBeginTime += snd_mme_period_time;
  } else {
    mme_pcm_reset(dev->pcm);
    dev->state = SndDeviceState_Error;
  }
  dev->activePeriod = sentinel_u8;
}
