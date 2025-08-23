#pragma once
#include "core/alloc.h"
#include "core/string.h"
#include "core/time.h"

typedef struct {
  /**
   * Timestamp of the begin of this period.
   * NOTE: timeEnd = timeBegin + frameCount / snd_frame_rate.
   */
  TimeSteady timeBegin;

  /**
   * Number of frames in this period.
   * Pre-condition: bits_aligned(frameCount, snd_frame_count_alignment).
   * Pre-condition: frameCount <= snd_frame_count_max.
   */
  u32 frameCount;

  /**
   * [frameCount * 2] Interleaved left and right channels (LRLRLR).
   * Pre-condition: bits_aligned_ptr(samples, snd_frame_sample_alignment).
   */
  i16* samples;
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

String         snd_device_id(const SndDevice*);
String         snd_device_backend(const SndDevice*);
SndDeviceState snd_device_state(const SndDevice*);
u64            snd_device_underruns(const SndDevice*);
String         snd_device_state_str(SndDeviceState);

bool            snd_device_begin(SndDevice*);
SndDevicePeriod snd_device_period(SndDevice*);
void            snd_device_end(SndDevice*);
