#include "core_float.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "scene_sound.h"
#include "scene_time.h"
#include "snd_mixer.h"

ecs_comp_define(SndSourceComp) { SndObjectId objectId; };
ecs_comp_define(SndSourceBrokenComp);

ecs_view_define(UpdateGlobalView) {
  ecs_access_write(SndMixerComp);
  ecs_access_read(SceneTimeSettingsComp);
  ecs_access_without(SndSourceBrokenComp);
}

ecs_view_define(UpdateView) {
  ecs_access_read(SceneSoundComp);
  ecs_access_maybe_read(SndSourceComp);
}

ecs_system_define(SndSourceUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, UpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  SndMixerComp*                m            = ecs_view_write_t(globalItr, SndMixerComp);
  const SceneTimeSettingsComp* timeSettings = ecs_view_read_t(globalItr, SceneTimeSettingsComp);

  f32 timeScale;
  if (timeSettings->flags & SceneTimeFlags_Paused) {
    timeScale = 0.0;
  } else {
    timeScale = timeSettings->scale;
  }

  EcsView* updateView = ecs_world_view_t(world, UpdateView);
  for (EcsIterator* itr = ecs_view_itr(updateView); ecs_view_walk(itr);) {
    const EcsEntityId     entity     = ecs_view_entity(itr);
    const SceneSoundComp* sndComp    = ecs_view_read_t(itr, SceneSoundComp);
    const SndSourceComp*  sourceComp = ecs_view_read_t(itr, SndSourceComp);

    if (!sourceComp) {
      if (!ecs_entity_valid(sndComp->asset)) {
        log_e("SceneSoundComp is missing an asset");
        ecs_world_add_empty_t(world, entity, SndSourceBrokenComp);
        continue;
      }
      SndObjectId id;
      if (snd_object_new(m, &id) == SndResult_Success) {
        snd_object_set_asset(m, id, sndComp->asset);
        if (sndComp->looping) {
          snd_object_set_looping(m, id);
        }
        sourceComp = ecs_world_add_t(world, entity, SndSourceComp, .objectId = id);
      } else {
        continue; // Failed to create a sound-object; retry next tick.
      }
    }

    snd_object_set_pitch(m, sourceComp->objectId, sndComp->pitch * timeScale);
    for (SndChannel chan = 0; chan != SndChannel_Count; ++chan) {
      snd_object_set_gain(m, sourceComp->objectId, chan, sndComp->gain);
    }
  }
}

ecs_module_init(snd_source_module) {
  ecs_register_comp(SndSourceComp);
  ecs_register_comp_empty(SndSourceBrokenComp);

  ecs_register_view(UpdateGlobalView);
  ecs_register_view(UpdateView);

  ecs_register_system(SndSourceUpdateSys, ecs_view_id(UpdateGlobalView), ecs_view_id(UpdateView));
}
