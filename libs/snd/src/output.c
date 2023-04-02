#include "core_math.h"
#include "ecs_world.h"
#include "scene_time.h"
#include "snd_output.h"
#include "snd_register.h"

#include "constants_internal.h"
#include "device_internal.h"

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

static void snd_output_sine(const SndDevicePeriod period, const f32 frequency) {
  const f64 stepPerSec   = 2.0f * math_pi_f64 * frequency;
  const f64 stepPerFrame = stepPerSec / snd_frame_rate;

  f64 phase = period.time / (f64)time_second * stepPerSec;
  for (u32 frame = 0; frame != period.frameCount; ++frame) {
    const i16 val = (i16)(math_sin_f64(phase) * i16_max);

    period.samples[frame * snd_frame_channels + 0] = val; // Left sample.
    period.samples[frame * snd_frame_channels + 1] = val; // Right sample.
    phase += stepPerFrame;
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

    snd_output_sine(period, 440);
    snd_device_end(output->device);
  }
}

ecs_module_init(snd_output_module) {
  ecs_register_comp(SndOutputComp, .destructor = ecs_destruct_output_comp);

  ecs_register_view(GlobalView);

  ecs_register_system(SndOutputUpdateSys, ecs_view_id(GlobalView));

  ecs_order(SndOutputUpdateSys, SndOrder_Output);
}
