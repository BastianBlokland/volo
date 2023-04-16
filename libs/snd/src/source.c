#include "core_diag.h"
#include "core_float.h"
#include "core_math.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "scene_sound.h"
#include "scene_time.h"
#include "scene_transform.h"
#include "snd_mixer.h"

ASSERT(sizeof(EcsEntityId) == sizeof(u64), "EntityId's have to be interpretable as 64bit integers");

ecs_comp_define(SndSourceComp) { SndObjectId objectId; };
ecs_comp_define(SndSourceFailedComp);

typedef struct {
  GeoVector pos;
  GeoVector rightDir;
} SndListener;

ecs_view_define(ListenerView) {
  ecs_access_with(SceneSoundListenerComp);
  ecs_access_read(SceneTransformComp);
}

static SndListener snd_listener(EcsWorld* world) {
  EcsView*     listenerView = ecs_world_view_t(world, ListenerView);
  EcsIterator* listenerItr  = ecs_view_first(listenerView);
  if (listenerItr) {
    const SceneTransformComp* trans = ecs_view_read_t(listenerItr, SceneTransformComp);
    return (SndListener){
        .pos      = trans->position,
        .rightDir = geo_quat_rotate(trans->rotation, geo_right),
    };
  }
  return (SndListener){.rightDir = geo_right};
}

static void snd_source_update_constant(
    SndMixerComp* m, const SceneSoundComp* soundComp, const SndSourceComp* sourceComp) {

  snd_object_set_pitch(m, sourceComp->objectId, soundComp->pitch);
  for (SndChannel chan = 0; chan != SndChannel_Count; ++chan) {
    snd_object_set_gain(m, sourceComp->objectId, chan, soundComp->gain);
  }
}

static void snd_source_update_spatial(
    SndMixerComp*         m,
    const SceneSoundComp* sndComp,
    const SndSourceComp*  srcComp,
    const GeoVector       sourcePos,
    const SndListener*    listener,
    const f32             timeScale) {
  const GeoVector toSource = geo_vector_sub(sourcePos, listener->pos);
  const f32       dist     = geo_vector_mag(toSource);
  const GeoVector dir      = dist < f32_epsilon ? geo_forward : geo_vector_div(toSource, dist);
  const f32       pan      = geo_vector_dot(dir, listener->rightDir); // LR pan, -1 0 +1

  snd_object_set_pitch(m, srcComp->objectId, sndComp->pitch * timeScale);

  const f32 leftAttenuation = math_clamp_f32((-pan + 1.0f) * 0.5f, 0, 1);
  snd_object_set_gain(m, srcComp->objectId, SndChannel_Left, sndComp->gain * leftAttenuation);

  const f32 rightAttenuation = math_clamp_f32((pan + 1.0f) * 0.5f, 0, 1);
  snd_object_set_gain(m, srcComp->objectId, SndChannel_Right, sndComp->gain * rightAttenuation);
}

ecs_view_define(UpdateGlobalView) {
  ecs_access_write(SndMixerComp);
  ecs_access_read(SceneTimeSettingsComp);
}

ecs_view_define(UpdateView) {
  ecs_access_read(SceneSoundComp);
  ecs_access_maybe_read(SndSourceComp);
  ecs_access_maybe_read(SceneTransformComp);
  ecs_access_without(SndSourceFailedComp);
}

ecs_system_define(SndSourceUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, UpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  SndMixerComp*                m            = ecs_view_write_t(globalItr, SndMixerComp);
  const SceneTimeSettingsComp* timeSettings = ecs_view_read_t(globalItr, SceneTimeSettingsComp);

  const SndListener listener = snd_listener(world);
  const f32 timeScale = timeSettings->flags & SceneTimeFlags_Paused ? 0.0f : timeSettings->scale;

  EcsView* updateView = ecs_world_view_t(world, UpdateView);
  for (EcsIterator* itr = ecs_view_itr(updateView); ecs_view_walk(itr);) {
    const SceneSoundComp*     soundComp     = ecs_view_read_t(itr, SceneSoundComp);
    const SndSourceComp*      sourceComp    = ecs_view_read_t(itr, SndSourceComp);
    const SceneTransformComp* transformComp = ecs_view_read_t(itr, SceneTransformComp);

    if (!sourceComp) {
      if (!ecs_entity_valid(soundComp->asset)) {
        log_e("SceneSoundComp is missing an asset");
        ecs_world_add_empty_t(world, ecs_view_entity(itr), SndSourceFailedComp);
        continue;
      }
      SndObjectId id;
      if (snd_object_new(m, &id) == SndResult_Success) {
        snd_object_set_asset(m, id, soundComp->asset);
        snd_object_set_user_data(m, id, (u64)ecs_view_entity(itr));
        if (soundComp->looping) {
          snd_object_set_looping(m, id);
        }
        sourceComp = ecs_world_add_t(world, ecs_view_entity(itr), SndSourceComp, .objectId = id);
      } else {
        continue; // Failed to create a sound-object; retry next tick.
      }
    }

    if (transformComp) {
      const GeoVector sourcePos = transformComp->position;
      snd_source_update_spatial(m, soundComp, sourceComp, sourcePos, &listener, timeScale);
    } else {
      snd_source_update_constant(m, soundComp, sourceComp);
    }
  }
}

ecs_view_define(CleanupGlobalView) { ecs_access_write(SndMixerComp); }

ecs_system_define(SndSourceCleanupSys) {
  EcsView*     globalView = ecs_world_view_t(world, CleanupGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  SndMixerComp* m = ecs_view_write_t(globalItr, SndMixerComp);
  for (SndObjectId obj = sentinel_u32; obj = snd_object_next(m, obj), !sentinel_check(obj);) {
    const EcsEntityId e = (EcsEntityId)snd_object_get_user_data(m, obj);
    diag_assert(ecs_entity_valid(e));

    if (!ecs_world_exists(world, e) || !ecs_world_has_t(world, e, SndSourceComp)) {
      snd_object_set_pitch(m, obj, 0);
    }
  }
}

ecs_module_init(snd_source_module) {
  ecs_register_comp(SndSourceComp);
  ecs_register_comp_empty(SndSourceFailedComp);

  ecs_register_view(ListenerView);

  ecs_register_system(
      SndSourceUpdateSys,
      ecs_view_id(ListenerView),
      ecs_register_view(UpdateGlobalView),
      ecs_register_view(UpdateView));

  ecs_register_system(SndSourceCleanupSys, ecs_register_view(CleanupGlobalView));
}
