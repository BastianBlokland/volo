#include "core_array.h"
#include "core_diag.h"
#include "core_math.h"
#include "ecs_world.h"
#include "scene_time.h"
#include "snd_output.h"
#include "snd_register.h"

#include "constants_internal.h"
#include "device_internal.h"

typedef struct {
  f32 samples[snd_frame_channels];
} SndSoundFrame;

typedef struct {
  SndSoundFrame* frames;
  usize          frameCount;
} SndSoundView;

ecs_comp_define(SndOutputComp) { SndDevice* device; };

static void ecs_destruct_output_comp(void* data) {
  SndOutputComp* comp = data;
  snd_device_destroy(comp->device);
}

ecs_view_define(GlobalView) {
  ecs_access_read(SceneTimeComp);
  ecs_access_maybe_write(SndOutputComp);
}

static SndOutputComp* snd_output_create(EcsWorld* world) {
  return ecs_world_add_t(
      world, ecs_world_global(world), SndOutputComp, .device = snd_device_create(g_alloc_heap));
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
  snd_render_sine(out, time, 329.63f);
  snd_render_sine(out, time, 392.0f);
}

static void snd_output_device_period(
    const SndDevicePeriod devicePeriod, const SndSoundView buffer, const f32 volume) {
  diag_assert(devicePeriod.frameCount == buffer.frameCount);

  for (u32 frame = 0; frame != devicePeriod.frameCount; ++frame) {
    for (u32 channel = 0; channel != snd_frame_channels; ++channel) {
      const f32 val     = buffer.frames[frame].samples[channel] * volume;
      const f32 clipped = val > 1.0 ? 1.0f : (val < -1.0 ? -1.0f : val);
      devicePeriod.samples[frame * snd_frame_channels + channel] = (i16)(clipped * i16_max);
    }
  }
}

ecs_system_define(SndOutputUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  SndOutputComp* output = ecs_view_write_t(globalItr, SndOutputComp);
  if (!output) {
    output = snd_output_create(world);
  }

  if (snd_device_begin(output->device)) {
    const SndDevicePeriod period = snd_device_period(output->device);

    SndSoundFrame      soundFrames[snd_frame_count_max] = {0};
    const SndSoundView soundBuffer = {.frames = soundFrames, .frameCount = period.frameCount};

    snd_render(soundBuffer, period.timeBegin);

    const f32 volume = 0.25f;
    snd_output_device_period(period, soundBuffer, volume);

    snd_device_end(output->device);
  }
}

ecs_module_init(snd_output_module) {
  ecs_register_comp(SndOutputComp, .destructor = ecs_destruct_output_comp);

  ecs_register_view(GlobalView);

  ecs_register_system(SndOutputUpdateSys, ecs_view_id(GlobalView));

  ecs_order(SndOutputUpdateSys, SndOrder_Output);
}
