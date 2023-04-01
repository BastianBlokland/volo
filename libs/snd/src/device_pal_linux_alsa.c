#include "log_logger.h"

#include "constants_internal.h"
#include "device_internal.h"

#include <alsa/asoundlib.h>

static const char* snd_pcm_device = "plughw:0,0";

typedef struct sSndDevice {
  Allocator*      alloc;
  snd_pcm_t*      pcm;
  SndDeviceStatus status;
} SndDevice;

typedef struct {
  bool              valid;
  snd_pcm_uframes_t bufferSize;
} AlsaPcmConfig;

static String alsa_error_str(const int err) { return string_from_null_term(snd_strerror(err)); }

static String alsa_pcm_type_str(const snd_pcm_type_t type) {
  // clang-format off
  switch (type) {
  case SND_PCM_TYPE_HW:           return string_lit("Kernel level PCM");
  case SND_PCM_TYPE_HOOKS:        return string_lit("Hooked PCM");
  case SND_PCM_TYPE_MULTI:        return string_lit("One or more linked PCM with exclusive access to selected channels");
  case SND_PCM_TYPE_FILE:         return string_lit("File writing plugin");
  case SND_PCM_TYPE_NULL:         return string_lit("Null endpoint PCM");
  case SND_PCM_TYPE_SHM:          return string_lit("Shared memory client PCM");
  case SND_PCM_TYPE_INET:         return string_lit("INET client PCM");
  case SND_PCM_TYPE_COPY:         return string_lit("Copying plugin");
  case SND_PCM_TYPE_LINEAR:       return string_lit("Linear format conversion PCM");
  case SND_PCM_TYPE_ALAW:         return string_lit("A-Law format conversion PCM");
  case SND_PCM_TYPE_MULAW:        return string_lit("Mu-Law format conversion PCM");
  case SND_PCM_TYPE_ADPCM:        return string_lit("IMA-ADPCM format conversion PCM");
  case SND_PCM_TYPE_RATE:         return string_lit("Rate conversion PCM");
  case SND_PCM_TYPE_ROUTE:        return string_lit("Attenuated static route PCM");
  case SND_PCM_TYPE_PLUG:         return string_lit("Format adjusted PCM");
  case SND_PCM_TYPE_SHARE:        return string_lit("Sharing PCM");
  case SND_PCM_TYPE_METER:        return string_lit("Meter plugin");
  case SND_PCM_TYPE_MIX:          return string_lit("Mixing PCM");
  case SND_PCM_TYPE_DROUTE:       return string_lit("Attenuated dynamic route PCM");
  case SND_PCM_TYPE_LBSERVER:     return string_lit("Loopback server plugin");
  case SND_PCM_TYPE_LINEAR_FLOAT: return string_lit("Linear Integer <-> Linear Float format conversion PCM");
  case SND_PCM_TYPE_LADSPA:       return string_lit("LADSPA integration plugin");
  case SND_PCM_TYPE_DMIX:         return string_lit("Direct Mixing plugin");
  case SND_PCM_TYPE_JACK:         return string_lit("Jack Audio Connection Kit plugin");
  case SND_PCM_TYPE_DSNOOP:       return string_lit("Direct Snooping plugin");
  case SND_PCM_TYPE_DSHARE:       return string_lit("Direct Sharing plugin");
  case SND_PCM_TYPE_IEC958:       return string_lit("IEC958 subframe plugin");
  case SND_PCM_TYPE_SOFTVOL:      return string_lit("Soft volume plugin");
  case SND_PCM_TYPE_IOPLUG:       return string_lit("External I/O plugin");
  case SND_PCM_TYPE_EXTPLUG:      return string_lit("External filter plugin");
  case SND_PCM_TYPE_MMAP_EMUL:    return string_lit("Mmap-emulation plugin");
  default:                        return string_lit("unknown");
  }
  // clang-format on
}

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
  if ((err = snd_pcm_hw_params_set_rate_resample(pcm, hwParams, 1))) {
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

  // Prepare the device for use.
  if ((err = snd_pcm_prepare(pcm))) {
    goto Err;
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

SndDevice* snd_device_create(Allocator* alloc) {
  snd_pcm_t*    pcm    = alsa_pcm_open();
  AlsaPcmConfig config = {0};
  if (pcm) {
    config = alsa_pcm_initialize(pcm);
  }
  if (config.valid) {
    const snd_pcm_type_t  type = snd_pcm_type(pcm);
    const snd_pcm_info_t* info = alsa_pcm_info_scratch(pcm);
    const i32             card = info ? snd_pcm_info_get_card(info) : -1;
    const String id = info ? string_from_null_term(snd_pcm_info_get_id(info)) : string_empty;

    log_i(
        "Alsa sound device created",
        log_param("id", fmt_text(id)),
        log_param("card", fmt_int(card)),
        log_param("type", fmt_text(alsa_pcm_type_str(type))),
        log_param("buffer", fmt_size(config.bufferSize)));
  }

  SndDevice* dev = alloc_alloc_t(alloc, SndDevice);
  *dev           = (SndDevice){
      .alloc  = alloc,
      .pcm    = pcm,
      .status = config.valid ? SndDeviceStatus_Ready : SndDeviceStatus_Error,
  };
  return dev;
}

void snd_device_destroy(SndDevice* dev) {
  if (dev->pcm) {
    snd_pcm_close(dev->pcm);
  }

  log_i("Alsa sound device destroyed");

  alloc_free_t(dev->alloc, dev);
}

SndDeviceStatus snd_device_status(const SndDevice* dev) { return dev->status; }

void snd_device_begin(SndDevice* dev) { (void)dev; }

SndDeviceFrame snd_device_frame(SndDevice* dev) {
  (void)dev;
  return (SndDeviceFrame){
      .time         = time_steady_clock(),
      .sampleCount  = 0,
      .sampleBuffer = null,
  };
}

void snd_device_end() {}
