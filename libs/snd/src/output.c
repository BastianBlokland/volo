#include "ecs_world.h"
#include "scene_time.h"
#include "snd_output.h"
#include "snd_register.h"

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
    const SndDeviceFrame frame = snd_device_frame(output->device);
    (void)frame;
    snd_device_end(output->device);
  }
}

ecs_module_init(snd_output_module) {
  ecs_register_comp(SndOutputComp, .destructor = ecs_destruct_output_comp);

  ecs_register_view(GlobalView);

  ecs_register_system(SndOutputUpdateSys, ecs_view_id(GlobalView));

  ecs_order(SndOutputUpdateSys, SndOrder_Output);
}
