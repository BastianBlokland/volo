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
  SndDeviceStatus_Error,
  SndDeviceStatus_Ready,
  SndDeviceStatus_FrameActive,

  SndDeviceStatus_Count,
} SndDeviceStatus;

typedef struct sSndDevice SndDevice;

SndDevice* snd_device_create(Allocator*);
void       snd_device_destroy(SndDevice*);

SndDeviceStatus snd_device_status(const SndDevice*);

void           snd_device_begin(SndDevice*);
SndDeviceFrame snd_device_frame(SndDevice*);
void           snd_device_end();

String snd_device_status_str(SndDeviceStatus);
