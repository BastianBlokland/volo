#include "log_logger.h"

#include "constants_internal.h"
#include "device_internal.h"

#include <alsa/asoundlib.h>

typedef struct sSndDevice {
  Allocator* alloc;
} SndDevice;

SndDevice* snd_device_create(Allocator* alloc) {
  SndDevice* dev = alloc_alloc_t(alloc, SndDevice);
  *dev           = (SndDevice){
      .alloc = alloc,
  };

  log_i("Alsa sound device created");

  return dev;
}

void snd_device_destroy(SndDevice* dev) {
  log_i("Alsa sound device destroyed");

  alloc_free_t(dev->alloc, dev);
}

SndDeviceStatus snd_device_status(const SndDevice* dev) {
  (void)dev;
  return SndDeviceStatus_Ready;
}

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
