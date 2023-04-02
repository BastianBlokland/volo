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

#define snd_mixer_history_frames 4096
ASSERT((snd_mixer_history_frames & (snd_mixer_history_frames - 1u)) == 0, "Non power-of-two")

ecs_comp_define(SndMixerComp) {
  SndDevice* device;
  f32        volume;

  /**
   * Keep a history of the last N frames in a ring-buffer for analysis and debug purposes.
   */
  SndMixerFrame* historyBuffer;
  usize          historyCursor;
};

static void ecs_destruct_mixer_comp(void* data) {
  SndMixerComp* comp = data;
  snd_device_destroy(comp->device);
  alloc_free_array_t(g_alloc_heap, comp->historyBuffer, snd_mixer_history_frames);
}

ecs_view_define(GlobalView) {
  ecs_access_read(SceneTimeComp);
  ecs_access_maybe_write(SndMixerComp);
}

static SndMixerComp* snd_mixer_create(EcsWorld* world) {
  SndMixerFrame* historyBuf = alloc_array_t(g_alloc_heap, SndMixerFrame, snd_mixer_history_frames);
  mem_set(mem_create(historyBuf, sizeof(SndMixerFrame) * snd_mixer_history_frames), 0);

  return ecs_world_add_t(
      world,
      ecs_world_global(world),
      SndMixerComp,
      .device        = snd_device_create(g_alloc_heap),
      .volume        = 0.1f,
      .historyBuffer = historyBuf);
}

static void snd_mixer_render_sine(SndMixerView out, const TimeSteady time, const f32 frequency) {
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

static void snd_mixer_render(SndMixerView out, const TimeSteady time) {
  snd_mixer_render_sine(out, time, 261.63f);
  // snd_mixer_render_sine(out, time, 329.63f);
  // snd_mixer_render_sine(out, time, 392.0f);
}

static void snd_mixer_history_add(SndMixerComp* mixer, const SndChannel channel, const f32 value) {
  mixer->historyBuffer[mixer->historyCursor].samples[channel] = value;
  mixer->historyCursor = (mixer->historyCursor + 1) & (snd_mixer_history_frames - 1);
}

static void snd_mixer_fill_device_period(
    SndMixerComp* mixer, const SndDevicePeriod devicePeriod, const SndMixerView buffer) {
  diag_assert(devicePeriod.frameCount == buffer.frameCount);

  for (u32 frame = 0; frame != devicePeriod.frameCount; ++frame) {
    for (SndChannel channel = 0; channel != SndChannel_Count; ++channel) {
      const f32 val     = buffer.frames[frame].samples[channel] * mixer->volume;
      const f32 clipped = val > 1.0 ? 1.0f : (val < -1.0 ? -1.0f : val);

      // Write to the device buffer.
      devicePeriod.samples[frame * SndChannel_Count + channel] = (i16)(clipped * i16_max);

      // Add it to the history ring-buffer for analysis / debug purposes.
      snd_mixer_history_add(mixer, channel, clipped);
    }
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

    SndMixerFrame      soundFrames[snd_frame_count_max] = {0};
    const SndMixerView soundBuffer = {.frames = soundFrames, .frameCount = period.frameCount};

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

SndMixerView snd_mixer_history(const SndMixerComp* mixer) {
  return (SndMixerView){.frames = mixer->historyBuffer, .frameCount = snd_mixer_history_frames};
}

f32 snd_mixer_sample(const SndMixerView view, const SndChannel channel, const f32 frac) {
  diag_assert(frac >= 0.0 && frac <= 1.0f);
  diag_assert(view.frameCount >= 2);

  /**
   * Linear interpolation between the two closest samples.
   * NOTE: We can explore other methods that preserve the curve better, like Hermite interpolation.
   */

  const f32 index = frac * (view.frameCount - 1);
  const f32 edgeA = math_min(view.frameCount - 2, math_round_down_f32(index));
  const f32 edgeB = edgeA + 1.0f;
  const f32 valA  = view.frames[(usize)edgeA].samples[channel];
  const f32 valB  = view.frames[(usize)edgeB].samples[channel];
  return math_lerp(valA, valB, index - edgeA);
}
