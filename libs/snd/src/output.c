#include "core_array.h"
#include "core_diag.h"
#include "core_math.h"
#include "ecs_world.h"
#include "scene_time.h"
#include "snd_output.h"
#include "snd_register.h"

#include "constants_internal.h"
#include "device_internal.h"

#define snd_output_history_frames 4096
ASSERT((snd_output_history_frames & (snd_output_history_frames - 1u)) == 0, "Non power-of-two")

typedef struct {
  f32 samples[snd_frame_channels];
} SndSoundFrame;

typedef struct {
  SndSoundFrame* frames;
  usize          frameCount;
} SndSoundView;

ecs_comp_define(SndOutputComp) {
  SndDevice* device;
  f32        volume;

  /**
   * Keep a history of the last N frames in a ring-buffer for analysis and debug purposes.
   */
  SndSoundFrame* historyBuffer;
  usize          historyCursor;
};

static void ecs_destruct_output_comp(void* data) {
  SndOutputComp* comp = data;
  snd_device_destroy(comp->device);
  alloc_free_array_t(g_alloc_heap, comp->historyBuffer, snd_output_history_frames);
}

ecs_view_define(GlobalView) {
  ecs_access_read(SceneTimeComp);
  ecs_access_maybe_write(SndOutputComp);
}

static SndOutputComp* snd_output_create(EcsWorld* world) {
  SndSoundFrame* historyBuf = alloc_array_t(g_alloc_heap, SndSoundFrame, snd_output_history_frames);
  mem_set(mem_create(historyBuf, sizeof(SndSoundFrame) * snd_output_history_frames), 0);

  return ecs_world_add_t(
      world,
      ecs_world_global(world),
      SndOutputComp,
      .device        = snd_device_create(g_alloc_heap),
      .volume        = 0.25,
      .historyBuffer = historyBuf);
}

static void snd_render_sine(SndSoundView out, const TimeSteady time, const f32 frequency) {
  const f64 stepPerSec   = 2.0f * math_pi_f64 * frequency;
  const f64 stepPerFrame = stepPerSec / snd_frame_rate;

  f64 phase = time / (f64)time_second * stepPerSec;
  for (u32 frame = 0; frame != out.frameCount; ++frame) {
    const f32 val = (f32)math_sin_f64(phase);
    phase += stepPerFrame;

    for (u32 channel = 0; channel != snd_frame_channels; ++channel) {
      out.frames[frame].samples[channel] += val;
    }
  }
}

static void snd_render(SndSoundView out, const TimeSteady time) {
  snd_render_sine(out, time, 261.63f);
  // snd_render_sine(out, time, 329.63f);
  // snd_render_sine(out, time, 392.0f);
}

static void snd_output_history_add(SndOutputComp* outputComp, const u32 channel, const f32 value) {
  outputComp->historyBuffer[outputComp->historyCursor].samples[channel] = value;
  outputComp->historyCursor = (outputComp->historyCursor + 1) & (snd_output_history_frames - 1);
}

static void snd_output_fill_device_period(
    SndOutputComp* outputComp, const SndDevicePeriod devicePeriod, const SndSoundView buffer) {
  diag_assert(devicePeriod.frameCount == buffer.frameCount);

  for (u32 frame = 0; frame != devicePeriod.frameCount; ++frame) {
    for (u32 channel = 0; channel != snd_frame_channels; ++channel) {
      const f32 val     = buffer.frames[frame].samples[channel] * outputComp->volume;
      const f32 clipped = val > 1.0 ? 1.0f : (val < -1.0 ? -1.0f : val);

      // Write to the device buffer.
      devicePeriod.samples[frame * snd_frame_channels + channel] = (i16)(clipped * i16_max);

      // Add it to the output ring-buffer for analysis / debug purposes.
      snd_output_history_add(outputComp, channel, clipped);
    }
  }
}

ecs_system_define(SndOutputUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  SndOutputComp* outputComp = ecs_view_write_t(globalItr, SndOutputComp);
  if (!outputComp) {
    outputComp = snd_output_create(world);
  }

  if (snd_device_begin(outputComp->device)) {
    const SndDevicePeriod period = snd_device_period(outputComp->device);

    SndSoundFrame      soundFrames[snd_frame_count_max] = {0};
    const SndSoundView soundBuffer = {.frames = soundFrames, .frameCount = period.frameCount};

    snd_render(soundBuffer, period.timeBegin);

    snd_output_fill_device_period(outputComp, period, soundBuffer);

    snd_device_end(outputComp->device);
  }
}

ecs_module_init(snd_output_module) {
  ecs_register_comp(SndOutputComp, .destructor = ecs_destruct_output_comp);

  ecs_register_view(GlobalView);

  ecs_register_system(SndOutputUpdateSys, ecs_view_id(GlobalView));

  ecs_order(SndOutputUpdateSys, SndOrder_Output);
}
