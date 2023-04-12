#include "core_float.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "scene_sound.h"
#include "snd_mixer.h"

ecs_comp_define(SndSourceComp) { SndObjectId objectId; };

ecs_view_define(InitGlobalView) { ecs_access_write(SndMixerComp); }

ecs_view_define(InitView) {
  ecs_access_read(SceneSoundComp);
  ecs_access_without(SndSourceComp);
}

ecs_system_define(SndSourceInitSys) {
  EcsView*     globalView = ecs_world_view_t(world, InitGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  SndMixerComp* mixer = ecs_view_write_t(globalItr, SndMixerComp);

  EcsView* initView = ecs_world_view_t(world, InitView);
  for (EcsIterator* itr = ecs_view_itr(initView); ecs_view_walk(itr);) {
    const EcsEntityId     entity  = ecs_view_entity(itr);
    const SceneSoundComp* sndComp = ecs_view_read_t(itr, SceneSoundComp);
    if (!ecs_entity_valid(sndComp->asset)) {
      log_e("SceneSoundComp is missing an asset");
      continue;
    }
    SndObjectId id;
    if (snd_object_new(mixer, &id) == SndResult_Success) {
      snd_object_set_asset(mixer, id, sndComp->asset);
      if (sndComp->looping) {
        snd_object_set_looping(mixer, id);
      }
      if (sndComp->pitch > f32_epsilon) {
        snd_object_set_pitch(mixer, id, sndComp->pitch);
      }
      if (sndComp->gain > f32_epsilon) {
        for (SndChannel chan = 0; chan != SndChannel_Count; ++chan) {
          snd_object_set_gain(mixer, id, chan, sndComp->gain);
        }
      }
      ecs_world_add_t(world, entity, SndSourceComp, .objectId = id);
    }
  }
}

ecs_module_init(snd_source_module) {
  ecs_register_comp(SndSourceComp);

  ecs_register_view(InitGlobalView);
  ecs_register_view(InitView);

  ecs_register_system(SndSourceInitSys, ecs_view_id(InitGlobalView), ecs_view_id(InitView));
}
