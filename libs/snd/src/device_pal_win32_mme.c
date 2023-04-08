#include "core_alloc.h"
#include "core_diag.h"
#include "log_logger.h"
#include "snd_channel.h"

#include "constants_internal.h"
#include "device_internal.h"

#include <Windows.h>
#include <mmeapi.h>

typedef enum {
  SndDeviceFlags_Rendering = 1 << 0,
} SndDeviceFlags;

typedef struct sSndDevice {
  Allocator* alloc;
  String     id;

  SndDeviceState state : 8;
  SndDeviceFlags flags : 8;

  HWAVEOUT pcm;

  TimeSteady nextPeriodBeginTime;

  u64 underrunCounter;
} SndDevice;

static String mme_result_str(const MMRESULT err) {
  switch (err) {
  case MMSYSERR_INVALHANDLE:
    return string_lit("Invalid device");
  case MMSYSERR_ALLOCATED:
    return string_lit("Specified resource is already allocated");
  case MMSYSERR_BADDEVICEID:
    return string_lit("Specified device identifier is out of range");
  case MMSYSERR_NODRIVER:
    return string_lit("No device driver is present");
  case MMSYSERR_NOMEM:
    return string_lit("Unable to allocate or lock memory");
  case MMSYSERR_NOTSUPPORTED:
    return string_lit("Operation not supported");
  case WAVERR_BADFORMAT:
    return string_lit("Attempted to open with an unsupported waveform-audio format");
  case WAVERR_SYNC:
    return string_lit("Synchronous device is not allowed");
  case WAVERR_STILLPLAYING:
    return string_lit("Device is still playing");
  default:
    return string_lit("unknown error");
  }
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
  if (result != MMSYSERR_NOERROR) {
    log_e(
        "Failed to open sound-device",
        log_param("err-code", fmt_int(result)),
        log_param("err", fmt_text(mme_result_str(result))));

    return INVALID_HANDLE_VALUE;
  }
  return device;
}

static void mme_pcm_close(HWAVEOUT pcm) {
  const MMRESULT result = waveOutClose(pcm);
  if (result != MMSYSERR_NOERROR) {
    log_e(
        "Failed to close sound-device",
        log_param("err-code", fmt_int(result)),
        log_param("err", fmt_text(mme_result_str(result))));
  }
}

static void mme_pcm_reset(HWAVEOUT pcm) {
  const MMRESULT result = waveOutReset(pcm);
  if (result != MMSYSERR_NOERROR) {
    log_e(
        "Failed to reset sound-device",
        log_param("err-code", fmt_int(result)),
        log_param("err", fmt_text(mme_result_str(result))));
  }
}

SndDevice* snd_device_create(Allocator* alloc) {
  HWAVEOUT     pcm = mme_pcm_open();
  const String id  = string_lit("<unknown>");

  if (pcm != INVALID_HANDLE_VALUE) {
    log_i("MME sound device created", log_param("id", fmt_text(id)));
  }

  SndDevice* dev = alloc_alloc_t(alloc, SndDevice);
  *dev           = (SndDevice){
                .alloc = alloc,
                .id    = string_maybe_dup(alloc, id),
                .pcm   = pcm,
                .state = pcm == INVALID_HANDLE_VALUE ? SndDeviceState_Error : SndDeviceState_Idle,
  };
  return dev;
}

void snd_device_destroy(SndDevice* dev) {
  if (dev->pcm != INVALID_HANDLE_VALUE) {
    if (dev->state != SndDeviceState_Idle) {
      mme_pcm_reset(dev->pcm);
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
  diag_assert_msg(!(dev->flags & SndDeviceFlags_Rendering), "Device rendering already active");

  if (UNLIKELY(dev->state == SndDeviceState_Error)) {
    return false; // Device is in an unrecoverable error state.
  }

  return false;
}

SndDevicePeriod snd_device_period(SndDevice* dev) {
  diag_assert_msg(dev->flags & SndDeviceFlags_Rendering, "Device not currently rendering");
  return (SndDevicePeriod){
      .timeBegin  = dev->nextPeriodBeginTime,
      .frameCount = 0,
      .samples    = null,
  };
}

void snd_device_end(SndDevice* dev) {
  diag_assert_msg(dev->flags & SndDeviceFlags_Rendering, "Device not currently rendering");

  dev->flags &= ~SndDeviceFlags_Rendering;
}
