#pragma once
#include "core_alloc.h"
#include "core_string.h"
#include "core_time.h"

typedef struct {
  TimeSteady time;
  usize      frameCount;
  i16*       samples; // [frameCount * 2] Interleaved left and right channels (LRLRLR).
} SndDevicePeriod;

typedef enum {
  SndDeviceState_Error,
  SndDeviceState_Idle,
  SndDeviceState_Playing,
  SndDeviceState_PeriodActive,

  SndDeviceState_Count,
} SndDeviceState;

typedef struct sSndDevice SndDevice;

SndDevice* snd_device_create(Allocator*);
void       snd_device_destroy(SndDevice*);

SndDeviceState snd_device_state(const SndDevice*);
String         snd_device_state_str(SndDeviceState);

bool            snd_device_begin(SndDevice*);
SndDevicePeriod snd_device_period(SndDevice*);
void            snd_device_end(SndDevice*);
