#include "core_array.h"
#include "core_diag.h"
#include "core_math.h"
#include "ecs_world.h"
#include "scene_time.h"
#include "snd_channel.h"
#include "snd_mixer.h"
#include "snd_register.h"

#include "constants_internal.h"
#include "device_internal.h"

#define snd_mixer_history_size 2048
ASSERT((snd_mixer_history_size & (snd_mixer_history_size - 1u)) == 0, "Non power-of-two")

#define snd_mixer_gain_adjust_per_frame 0.0001f

ecs_comp_define(SndMixerComp) {
  SndDevice* device;
  f32        gainActual, gainTarget;

  /**
   * Keep a history of the last N frames in a ring-buffer for analysis and debug purposes.
   */
  SndBufferFrame* historyBuffer;
  usize           historyCursor;
};

static void ecs_destruct_mixer_comp(void* data) {
  SndMixerComp* comp = data;
  snd_device_destroy(comp->device);
  alloc_free_array_t(g_alloc_heap, comp->historyBuffer, snd_mixer_history_size);
}

ecs_view_define(GlobalView) {
  ecs_access_read(SceneTimeComp);
  ecs_access_maybe_write(SndMixerComp);
}

static SndMixerComp* snd_mixer_create(EcsWorld* world) {
  SndBufferFrame* historyBuf = alloc_array_t(g_alloc_heap, SndBufferFrame, snd_mixer_history_size);
  mem_set(mem_create(historyBuf, sizeof(SndBufferFrame) * snd_mixer_history_size), 0);

  return ecs_world_add_t(
      world,
      ecs_world_global(world),
      SndMixerComp,
      .device        = snd_device_create(g_alloc_heap),
      .gainTarget    = 0.15f,
      .historyBuffer = historyBuf);
}

static void snd_mixer_render_sine(SndBuffer out, const TimeSteady time, const f32 frequency) {
  const f64 stepPerSec   = 2.0f * math_pi_f64 * frequency;
  const f64 stepPerFrame = stepPerSec / snd_frame_rate;

  f64 phase = time / (f64)time_second * stepPerSec;
  for (u32 frame = 0; frame != out.frameCount; ++frame) {
    const f32 val = (f32)math_sin_f64(phase);
    phase += stepPerFrame;

    for (SndChannel channel = 0; channel != SndChannel_Count; ++channel) {
      out.frames[frame].samples[channel] += val;
    }
  }
}

static void snd_mixer_render(SndBuffer out, const TimeSteady time) {
  snd_mixer_render_sine(out, time, 261.63f);
  snd_mixer_render_sine(out, time, 329.63f);
  snd_mixer_render_sine(out, time, 392.0f);
}

static void snd_mixer_history_set(SndMixerComp* mixer, const SndChannel channel, const f32 value) {
  mixer->historyBuffer[mixer->historyCursor].samples[channel] = value;
}

static void snd_mixer_history_advance(SndMixerComp* mixer) {
  mixer->historyCursor = (mixer->historyCursor + 1) & (snd_mixer_history_size - 1);
}

static void snd_mixer_fill_device_period(
    SndMixerComp* mixer, const SndDevicePeriod devicePeriod, const SndBuffer buffer) {
  diag_assert(devicePeriod.frameCount == buffer.frameCount);

  for (u32 frame = 0; frame != devicePeriod.frameCount; ++frame) {
    math_towards_f32(&mixer->gainActual, mixer->gainTarget, snd_mixer_gain_adjust_per_frame);

    for (SndChannel channel = 0; channel != SndChannel_Count; ++channel) {
      const f32 val = buffer.frames[frame].samples[channel] * mixer->gainActual;

      // Add it to the history ring-buffer for analysis / debug purposes.
      snd_mixer_history_set(mixer, channel, val);

      // Write to the device buffer.
      const i16 clipped = val > 1.0 ? i16_max : (val < -1.0 ? i16_min : (i16)(val * i16_max));
      devicePeriod.samples[frame * SndChannel_Count + channel] = clipped;
    }
    snd_mixer_history_advance(mixer);
  }
}

ecs_system_define(SndMixerUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  SndMixerComp* mixer = ecs_view_write_t(globalItr, SndMixerComp);
  if (!mixer) {
    mixer = snd_mixer_create(world);
  }

  if (snd_device_begin(mixer->device)) {
    const SndDevicePeriod period = snd_device_period(mixer->device);

    SndBufferFrame  soundFrames[snd_frame_count_max] = {0};
    const SndBuffer soundBuffer = {.frames = soundFrames, .frameCount = period.frameCount};

    snd_mixer_render(soundBuffer, period.timeBegin);

    snd_mixer_fill_device_period(mixer, period, soundBuffer);

    snd_device_end(mixer->device);
  }
}

ecs_module_init(snd_mixer_module) {
  ecs_register_comp(SndMixerComp, .destructor = ecs_destruct_mixer_comp);

  ecs_register_view(GlobalView);

  ecs_register_system(SndMixerUpdateSys, ecs_view_id(GlobalView));

  ecs_order(SndMixerUpdateSys, SndOrder_Mix);
}

f32  snd_mixer_gain_get(const SndMixerComp* mixer) { return mixer->gainTarget; }
void snd_mixer_gain_set(SndMixerComp* mixer, const f32 gain) { mixer->gainTarget = gain; }

String snd_mixer_device_id(const SndMixerComp* mixer) { return snd_device_id(mixer->device); }

String snd_mixer_device_state(const SndMixerComp* mixer) {
  const SndDeviceState state = snd_device_state(mixer->device);
  return snd_device_state_str(state);
}

u64 snd_mixer_device_underruns(const SndMixerComp* mixer) {
  return snd_device_underruns(mixer->device);
}

SndBufferView snd_mixer_history(const SndMixerComp* mixer) {
  return (SndBufferView){.frames = mixer->historyBuffer, .frameCount = snd_mixer_history_size};
}
