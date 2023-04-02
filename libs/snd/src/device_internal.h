#pragma once
#include "core_alloc.h"
#include "core_string.h"
#include "core_time.h"

typedef struct {
  TimeSteady timeBegin; // Timestamp of the begin of this period.
                        // timeEnd = timeBegin + frameCount / snd_frame_rate.
  usize frameCount;     // Number of frames in this period.
                        // bits_aligned(frameCount, snd_frame_count_alignment).
  i16* samples;         // [frameCount * 2] Interleaved left and right channels (LRLRLR).
                        // bits_aligned_ptr(samples, snd_frame_sample_alignment).
} SndDevicePeriod;

typedef enum {
  SndDeviceState_Error,
  SndDeviceState_Idle,
  SndDeviceState_Playing,

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
