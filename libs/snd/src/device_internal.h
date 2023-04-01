#pragma once
#include "core_alloc.h"
#include "core_string.h"
#include "core_time.h"

typedef struct {
  TimeSteady time;
  usize      sampleCount;
  i16*       sampleBuffer; // Interleaved left and right channels (LRLRLR).
} SndDeviceFrame;

typedef enum {
  SndDeviceState_Error,
  SndDeviceState_Idle,
  SndDeviceState_Playing,
  SndDeviceState_FrameActive,

  SndDeviceState_Count,
} SndDeviceState;

typedef struct sSndDevice SndDevice;

SndDevice* snd_device_create(Allocator*);
void       snd_device_destroy(SndDevice*);

SndDeviceState snd_device_state(const SndDevice*);
String         snd_device_state_str(SndDeviceState);

bool           snd_device_begin(SndDevice*);
SndDeviceFrame snd_device_frame(SndDevice*);
void           snd_device_end(SndDevice*);
