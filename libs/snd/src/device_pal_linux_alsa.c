#include "log_logger.h"

#include "constants_internal.h"
#include "device_internal.h"

#include <alsa/asoundlib.h>

static const char* snd_pcm_device = "default";

typedef struct sSndDevice {
  Allocator*      alloc;
  snd_pcm_t*      pcm;
  SndDeviceStatus status;
} SndDevice;

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
  const int  err = snd_pcm_open(&pcm, snd_pcm_device, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
  if (err != 0) {
    log_e(
        "Failed to open sound-device",
        log_param("name", fmt_text(string_from_null_term(snd_pcm_device))),
        log_param("error-code", fmt_int(err)),
        log_param("error", fmt_text(alsa_error_str(err))));
    return null;
  }
  return pcm;
}

SndDevice* snd_device_create(Allocator* alloc) {
  SndDevice* dev = alloc_alloc_t(alloc, SndDevice);
  *dev           = (SndDevice){
      .alloc = alloc,
      .pcm   = alsa_pcm_open(),
  };
  if (dev->pcm) {
    const snd_pcm_type_t type = snd_pcm_type(dev->pcm);

    log_i("Alsa sound device created", log_param("type", fmt_text(alsa_pcm_type_str(type))));

    // TODO: https://soundprogramming.net/programming/alsa-tutorial-1-initialization/
    dev->status = SndDeviceStatus_Ready;

  } else {
    dev->status = SndDeviceStatus_InitFailed;
  }
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
