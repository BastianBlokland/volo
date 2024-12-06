#include "asset_prefab.h"
#include "core_alloc.h"
#include "core_dynarray.h"
#include "core_float.h"
#include "core_math.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "scene_prefab.h"
#include "scene_sound.h"
#include "scene_tag.h"
#include "scene_time.h"
#include "scene_transform.h"
#include "scene_visibility.h"
#include "snd_mixer.h"
#include "snd_register.h"

ASSERT(sizeof(EcsEntityId) == sizeof(u64), "EntityId's have to be interpretable as 64bit integers");
ASSERT(SndChannel_Count == 2, "Only stereo sound is supported at the moment");

#define snd_source_attenuate_dist 125.0f
#define snd_source_cull_dist 150.0f
#define snd_source_event_max_time time_milliseconds(100)
#define snd_source_event_distance 10.0f

ASSERT((u32)snd_source_cull_dist > (u32)snd_source_attenuate_dist, "Invalid cull distance");

typedef struct {
  GeoVector position;
  GeoVector tangent;
} SndListener;

typedef struct {
  EcsEntityId  soundAsset;
  TimeDuration timestamp;
  GeoVector    position;
} SndEvent;

ecs_comp_define(SndEventMapComp) {
  u32      prefabMapVersion; // Version of the last processed prefab-map (for preloading).
  DynArray events;           // SndEvent[]
};

ecs_comp_define(SndSourceComp) { SndObjectId objectId; };

ecs_comp_define(SndSourceDiscardComp);

static void ecs_destruct_event_map_comp(void* data) {
  SndEventMapComp* map = data;
  dynarray_destroy(&map->events);
}

ecs_view_define(ListenerView) {
  ecs_access_with(SceneSoundListenerComp);
  ecs_access_read(SceneTransformComp);
}

ecs_view_define(PrefabMapView) { ecs_access_read(AssetPrefabMapComp); }

static SndEventMapComp* snd_event_map_init(EcsWorld* world) {
  return ecs_world_add_t(
      world,
      ecs_world_global(world),
      SndEventMapComp,
      .events = dynarray_create_t(g_allocHeap, SndEvent, 64));
}

static void snd_event_map_prune_older(SndEventMapComp* map, const TimeDuration timestamp) {
  const SndEvent* events = dynarray_begin_t(&map->events, SndEvent);
  usize           index  = 0;
  for (; index != map->events.size && events[index].timestamp < timestamp; ++index)
    ;
  dynarray_remove(&map->events, 0, index);
}

static bool snd_event_map_has(SndEventMapComp* map, const EcsEntityId sound, const GeoVector pos) {
  dynarray_for_t(&map->events, SndEvent, evt) {
    if (evt->soundAsset != sound) {
      continue;
    }
    const f32 distSqr = geo_vector_mag_sqr(geo_vector_sub(pos, evt->position));
    if (distSqr < (snd_source_event_distance * snd_source_event_distance)) {
      return true;
    }
  }
  return false;
}

static void snd_event_map_add(
    SndEventMapComp*   map,
    const TimeDuration timestamp,
    const EcsEntityId  sound,
    const GeoVector    pos) {
  *dynarray_push_t(&map->events, SndEvent) = (SndEvent){
      .timestamp  = timestamp,
      .soundAsset = sound,
      .position   = pos,
  };
}

static SndListener snd_listener(EcsWorld* world) {
  EcsView*     listenerView = ecs_world_view_t(world, ListenerView);
  EcsIterator* listenerItr  = ecs_view_first(listenerView);
  if (listenerItr) {
    const SceneTransformComp* trans = ecs_view_read_t(listenerItr, SceneTransformComp);
    return (SndListener){
        .position = trans->position,
        .tangent  = geo_quat_rotate(trans->rotation, geo_right),
    };
  }
  return (SndListener){.position = geo_vector(0), .tangent = geo_right};
}

static bool snd_source_cull(const SndListener* listener, const GeoVector pos) {
  const GeoVector delta = geo_vector_sub(pos, listener->position);
  return geo_vector_mag_sqr(delta) > (snd_source_cull_dist * snd_source_cull_dist);
}

static void snd_source_update_constant(
    SndMixerComp*         m,
    const SceneSoundComp* soundComp,
    const SndSourceComp*  srcComp,
    const f32             srcGain) {

  snd_object_set_pitch(m, srcComp->objectId, soundComp->pitch);
  for (SndChannel chan = 0; chan != SndChannel_Count; ++chan) {
    snd_object_set_gain(m, srcComp->objectId, chan, srcGain);
  }
}

static void snd_source_update_spatial(
    SndMixerComp*         m,
    const SceneSoundComp* sndComp,
    const SndSourceComp*  srcComp,
    const GeoVector       srcPos,
    const f32             srcGain,
    const SndListener*    listener,
    const f32             timeScale) {
  const GeoVector toSource = geo_vector_sub(srcPos, listener->position);

  const f32 dist            = geo_vector_mag(toSource);
  const f32 distAttenuation = 1.0f - math_min(1, dist / snd_source_attenuate_dist);

  const GeoVector dir = dist < f32_epsilon ? geo_forward : geo_vector_div(toSource, dist);
  const f32       pan = geo_vector_dot(dir, listener->tangent); // LR pan, -1 0 +1

  snd_object_set_pitch(m, srcComp->objectId, sndComp->pitch * timeScale);

  const f32 leftAttenuation = math_clamp_f32(distAttenuation * (-pan + 1.0f) * 0.5f, 0.0f, 1.0f);
  snd_object_set_gain(m, srcComp->objectId, SndChannel_Left, srcGain * leftAttenuation);

  const f32 rightAttenuation = math_clamp_f32(distAttenuation * (pan + 1.0f) * 0.5f, 0.0f, 1.0f);
  snd_object_set_gain(m, srcComp->objectId, SndChannel_Right, srcGain * rightAttenuation);
}

ecs_view_define(UpdateGlobalView) {
  ecs_access_maybe_read(ScenePrefabEnvComp);
  ecs_access_maybe_write(SndEventMapComp);
  ecs_access_read(SceneTimeComp);
  ecs_access_read(SceneTimeSettingsComp);
  ecs_access_read(SceneVisibilityEnvComp);
  ecs_access_write(SndMixerComp);
}

ecs_view_define(UpdateView) {
  ecs_access_maybe_read(SceneTagComp);
  ecs_access_maybe_read(SceneTransformComp);
  ecs_access_maybe_read(SceneVisibilityComp);
  ecs_access_maybe_read(SndSourceComp);
  ecs_access_read(SceneSoundComp);
  ecs_access_without(SndSourceDiscardComp);
}

/**
 * Preload the persistent sounds in the given prefab-map.
 */
static void snd_source_preload_prefabs(SndMixerComp* m, const AssetPrefabMapComp* prefabMap) {
  // Check for persistent sound assets on the traits.
  for (usize traitIndex = 0; traitIndex != prefabMap->traits.count; ++traitIndex) {
    const AssetPrefabTrait* trait = &prefabMap->traits.values[traitIndex];
    switch ((u32)trait->type) {
    case AssetPrefabTrait_Sound:
      if (trait->data_sound.persistent) {
        array_for_t(trait->data_sound.assets, EcsEntityId, asset) {
          if (ecs_entity_valid(*asset)) {
            snd_mixer_persistent_asset(m, *asset);
          }
        }
      }
      break;
    }
  }
  // Check for persistent sound assets on the values.
  for (usize valueIndex = 0; valueIndex != prefabMap->values.count; ++valueIndex) {
    const AssetPrefabValue* val = &prefabMap->values.values[valueIndex];
    switch ((u32)val->type) {
    case AssetPrefabValue_Sound:
      if (val->data_sound.persistent) {
        snd_mixer_persistent_asset(m, val->data_sound.asset);
      }
      break;
    }
  }
}

ecs_system_define(SndSourceUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, UpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  SndMixerComp*                 m            = ecs_view_write_t(globalItr, SndMixerComp);
  const SceneTimeComp*          time         = ecs_view_read_t(globalItr, SceneTimeComp);
  const SceneTimeSettingsComp*  timeSettings = ecs_view_read_t(globalItr, SceneTimeSettingsComp);
  const SceneVisibilityEnvComp* visEnv       = ecs_view_read_t(globalItr, SceneVisibilityEnvComp);

  SndEventMapComp* eventMap = ecs_view_write_t(globalItr, SndEventMapComp);
  if (eventMap) {
    const TimeSteady oldestEventToKeep = time->realTime - snd_source_event_max_time;
    snd_event_map_prune_older(eventMap, oldestEventToKeep);
  } else {
    eventMap = snd_event_map_init(world);
  }

  const ScenePrefabEnvComp* prefabEnv = ecs_view_read_t(globalItr, ScenePrefabEnvComp);
  if (prefabEnv && scene_prefab_map_version(prefabEnv) != eventMap->prefabMapVersion) {
    EcsView*     mapView = ecs_world_view_t(world, PrefabMapView);
    EcsIterator* mapItr  = ecs_view_maybe_at(mapView, scene_prefab_map(prefabEnv));
    if (mapItr) {
      snd_source_preload_prefabs(m, ecs_view_read_t(mapItr, AssetPrefabMapComp));
      eventMap->prefabMapVersion = scene_prefab_map_version(prefabEnv);

      log_d(
          "Preloading prefab-map sounds",
          log_param("version", fmt_int(eventMap->prefabMapVersion)));
    }
  }

  const SndListener listener = snd_listener(world);
  const f32 timeScale = timeSettings->flags & SceneTimeFlags_Paused ? 0.0f : timeSettings->scale;

  EcsView* updateView = ecs_world_view_t(world, UpdateView);
  for (EcsIterator* itr = ecs_view_itr(updateView); ecs_view_walk(itr);) {
    const SceneSoundComp*      soundComp     = ecs_view_read_t(itr, SceneSoundComp);
    const SndSourceComp*       srcComp       = ecs_view_read_t(itr, SndSourceComp);
    const SceneTransformComp*  transformComp = ecs_view_read_t(itr, SceneTransformComp);
    const SceneTagComp*        tagComp       = ecs_view_read_t(itr, SceneTagComp);
    const SceneVisibilityComp* visComp       = ecs_view_read_t(itr, SceneVisibilityComp);
    const bool                 spatial       = transformComp != null;
    const SceneTags            tags          = tagComp ? tagComp->tags : SceneTags_Default;
    const GeoVector            srcPos        = spatial ? transformComp->position : geo_vector(0);
    const f32                  srcGain       = tags & SceneTags_Emit ? soundComp->gain : 0.0f;
    const bool                 srcVisible = !visComp || scene_visible_for_render(visEnv, visComp);

    if (!srcComp) {
      if (!ecs_entity_valid(soundComp->asset)) {
        log_e("SceneSoundComp is missing an asset");
        ecs_world_add_empty_t(world, ecs_view_entity(itr), SndSourceDiscardComp);
        continue; // Discarded; do not retry.
      }
      // Skip duplicate (same sound in a close proximity) sounds.
      if (!soundComp->looping && spatial && snd_event_map_has(eventMap, soundComp->asset, srcPos)) {
        ecs_world_add_empty_t(world, ecs_view_entity(itr), SndSourceDiscardComp);
        continue; // Discarded; do not retry.
      }
      // Delay creating a sound object for looping sources that are too far away.
      if (soundComp->looping && spatial && snd_source_cull(&listener, srcPos)) {
        continue; // Too far away; retry next tick.
      }
      SndObjectId id;
      if (snd_object_new(m, &id) == SndResult_Success) {
        snd_object_set_asset(m, id, soundComp->asset);
        snd_object_set_user_data(m, id, (u64)ecs_view_entity(itr));
        if (soundComp->looping) {
          snd_object_set_looping(m, id);
          if (spatial) {
            snd_object_set_random_cursor(m, id);
          }
        }
        srcComp = ecs_world_add_t(world, ecs_view_entity(itr), SndSourceComp, .objectId = id);
        if (!soundComp->looping && spatial) {
          snd_event_map_add(eventMap, time->realTime, soundComp->asset, srcPos);
        }
      } else {
        continue; // Failed to create a sound-object; retry next tick.
      }
    }

    if (!snd_object_is_active(m, srcComp->objectId)) {
      continue; // Already finished playing on the mixer.
    }
    // Stop looping sounds that are too far away to hear.
    if (soundComp->looping && spatial && snd_source_cull(&listener, srcPos)) {
      snd_object_stop(m, srcComp->objectId);
      ecs_world_remove_t(world, ecs_view_entity(itr), SndSourceComp);
      continue;
    }
    // Fast-path for muted sounds.
    if (srcGain < f32_epsilon || !srcVisible) {
      for (SndChannel chan = 0; chan != SndChannel_Count; ++chan) {
        snd_object_set_gain(m, srcComp->objectId, chan, 0);
      }
      continue;
    }

    if (spatial) {
      snd_source_update_spatial(m, soundComp, srcComp, srcPos, srcGain, &listener, timeScale);
    } else {
      snd_source_update_constant(m, soundComp, srcComp, srcGain);
    }
  }
}

ecs_view_define(CleanupGlobalView) { ecs_access_write(SndMixerComp); }

ecs_view_define(CleanupView) {
  ecs_access_with(SndSourceComp);
  ecs_access_without(SceneSoundComp);
}

ecs_system_define(SndSourceCleanupSys) {
  EcsView*     globalView = ecs_world_view_t(world, CleanupGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }

  // Remove SndSourceComp's where the SceneSoundComp has been removed.
  EcsView* cleanupView = ecs_world_view_t(world, CleanupView);
  for (EcsIterator* itr = ecs_view_itr(cleanupView); ecs_view_walk(itr);) {
    ecs_world_remove_t(world, ecs_view_entity(itr), SndSourceComp);
  }

  // Stop playing any sound objects if the SndSourceComp has since been removed.
  SndMixerComp* m = ecs_view_write_t(globalItr, SndMixerComp);
  for (SndObjectId obj = sentinel_u32; obj = snd_object_next(m, obj), !sentinel_check(obj);) {
    const EcsEntityId e = (EcsEntityId)snd_object_get_user_data(m, obj);
    if (!ecs_entity_valid(e)) {
      continue; // User-data is not an entity; object was not created from this module.
    }
    if (!ecs_world_exists(world, e) || !ecs_world_has_t(world, e, SndSourceComp)) {
      snd_object_stop(m, obj);
    }
  }
}

ecs_module_init(snd_source_module) {
  ecs_register_comp(SndEventMapComp, .destructor = ecs_destruct_event_map_comp);
  ecs_register_comp(SndSourceComp);
  ecs_register_comp_empty(SndSourceDiscardComp);

  ecs_register_view(ListenerView);
  ecs_register_view(PrefabMapView);

  ecs_register_system(
      SndSourceUpdateSys,
      ecs_view_id(ListenerView),
      ecs_view_id(PrefabMapView),
      ecs_register_view(UpdateGlobalView),
      ecs_register_view(UpdateView));

  ecs_register_system(
      SndSourceCleanupSys, ecs_register_view(CleanupGlobalView), ecs_register_view(CleanupView));

  ecs_order(SndSourceCleanupSys, SndOrder_Cleanup);
}
