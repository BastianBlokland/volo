#include "core_alloc.h"
#include "core_diag.h"
#include "log_logger.h"

#include "constants_internal.h"
#include "device_internal.h"

typedef enum {
  SndDeviceFlags_Rendering = 1 << 0,
} SndDeviceFlags;

typedef struct sSndDevice {
  Allocator* alloc;
  String     id;

  SndDeviceState state : 8;
  SndDeviceFlags flags : 8;

  TimeSteady nextPeriodBeginTime;

  u64 underrunCounter;
} SndDevice;

SndDevice* snd_device_create(Allocator* alloc) {
  const String id = string_lit("uknown");

  log_i("MME sound device created", log_param("id", fmt_text(id)));

  SndDevice* dev = alloc_alloc_t(alloc, SndDevice);
  *dev           = (SndDevice){
                .alloc = alloc,
                .id    = string_maybe_dup(alloc, id),
                .state = SndDeviceState_Idle,
  };
  return dev;
}

void snd_device_destroy(SndDevice* dev) {
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
