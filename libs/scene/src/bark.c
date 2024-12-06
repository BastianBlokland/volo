#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_float.h"
#include "core_rng.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "scene_attachment.h"
#include "scene_bark.h"
#include "scene_lifetime.h"
#include "scene_prefab.h"
#include "scene_sound.h"
#include "scene_time.h"
#include "scene_transform.h"
#include "scene_visibility.h"

static const TimeDuration g_barkEventDuration[SceneBarkType_Count] = {
    [SceneBarkType_Death]   = time_milliseconds(500),
    [SceneBarkType_Confirm] = time_milliseconds(750),
};

#define scene_bark_cooldown_min time_seconds(2)
#define scene_bark_cooldown_max time_seconds(3)
#define scene_bark_distance_max 100.0f

typedef struct {
  i32          priority;
  StringHash   prefab;
  TimeDuration expireTimestamp;
  EcsEntityId  instigator;
  GeoVector    position;
} SceneBarkEvent;

ecs_comp_define(SceneBarkRegistryComp) {
  DynArray     events; // SceneBarkEvent[]
  TimeDuration nextBarkTime;
};

ecs_comp_define_public(SceneBarkComp);

static void ecs_destruct_registry_comp(void* data) {
  SceneBarkRegistryComp* registry = data;
  dynarray_destroy(&registry->events);
}

static SceneBarkRegistryComp* registry_init(EcsWorld* world) {
  return ecs_world_add_t(
      world,
      ecs_world_global(world),
      SceneBarkRegistryComp,
      .events = dynarray_create_t(g_allocHeap, SceneBarkEvent, 64));
}

static void registry_prune(SceneBarkRegistryComp* reg, const TimeDuration timestamp) {
  for (usize i = reg->events.size; i-- != 0;) {
    const SceneBarkEvent* evt = dynarray_at_t(&reg->events, i, SceneBarkEvent);
    if (timestamp > evt->expireTimestamp) {
      dynarray_remove_unordered(&reg->events, i, 1);
    }
  }
}

static void registry_report(
    SceneBarkRegistryComp* reg,
    const EcsEntityId      instigator,
    const SceneBarkType    type,
    const SceneBarkComp*   bark,
    const SceneTimeComp*   time,
    const GeoVector        pos) {
  diag_assert(g_barkEventDuration[type]);
  if (bark->barkPrefabs[type]) {
    *dynarray_push_t(&reg->events, SceneBarkEvent) = (SceneBarkEvent){
        .priority        = bark->priority,
        .prefab          = bark->barkPrefabs[type],
        .expireTimestamp = time->time + g_barkEventDuration[type],
        .instigator      = instigator,
        .position        = pos,
    };
  }
}

static bool registry_pop(SceneBarkRegistryComp* reg, const GeoVector pos, SceneBarkEvent* out) {
  usize bestIndex = sentinel_usize;
  i32   bestPriority;
  f32   bestDistSqr;
  for (usize i = 0; i != reg->events.size; ++i) {
    const SceneBarkEvent* evt      = dynarray_at_t(&reg->events, i, SceneBarkEvent);
    const GeoVector       posDelta = geo_vector_sub(evt->position, pos);
    const f32             distSqr  = geo_vector_mag_sqr(posDelta);
    if (distSqr > (scene_bark_distance_max * scene_bark_distance_max)) {
      continue;
    }
    if (sentinel_check(bestIndex) || evt->priority > bestPriority || distSqr < bestDistSqr) {
      bestIndex    = i;
      bestPriority = evt->priority;
      bestDistSqr  = distSqr;
    }
  }
  if (!sentinel_check(bestIndex)) {
    *out = *dynarray_at_t(&reg->events, bestIndex, SceneBarkEvent);
    dynarray_remove_unordered(&reg->events, bestIndex, 1);
    return true;
  }
  return false;
}

static void bark_spawn(EcsWorld* world, SceneBarkEvent* barkEvent) {
  const EcsEntityId barkEntity = scene_prefab_spawn(
      world,
      &(ScenePrefabSpec){
          .flags    = ScenePrefabFlags_Volatile,
          .prefabId = barkEvent->prefab,
          .faction  = SceneFaction_None,
          .position = barkEvent->position,
          .rotation = geo_quat_ident});
  ecs_world_add_t(world, barkEntity, SceneLifetimeOwnerComp, .owners[0] = barkEvent->instigator);
  ecs_world_add_t(world, barkEntity, SceneVisibilityComp); // Hearing barks requires visibility.
  scene_attach_to_entity(world, barkEntity, barkEvent->instigator);
}

static TimeDuration bark_next_time(const TimeDuration timeNow) {
  TimeDuration next = timeNow;
  next += (TimeDuration)rng_sample_range(g_rng, scene_bark_cooldown_min, scene_bark_cooldown_max);
  return next;
}

ecs_view_define(UpdateGlobalView) {
  ecs_access_read(SceneTimeComp);
  ecs_access_maybe_write(SceneBarkRegistryComp);
}

ecs_view_define(UpdateView) {
  ecs_access_write(SceneBarkComp);
  ecs_access_maybe_read(SceneTransformComp);
}

ecs_view_define(ListenerView) {
  ecs_access_with(SceneSoundListenerComp);
  ecs_access_read(SceneTransformComp);
}

static GeoVector bark_listener_position(EcsWorld* world) {
  EcsView*     listenerView = ecs_world_view_t(world, ListenerView);
  EcsIterator* listenerItr  = ecs_view_first(listenerView);
  return listenerItr ? ecs_view_read_t(listenerItr, SceneTransformComp)->position : geo_vector(0);
}

ecs_system_define(SceneBarkUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, UpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneTimeComp* time = ecs_view_read_t(globalItr, SceneTimeComp);

  SceneBarkRegistryComp* reg = ecs_view_write_t(globalItr, SceneBarkRegistryComp);
  if (reg) {
    registry_prune(reg, time->time);
  } else {
    reg = registry_init(world);
  }

  // Generate bark events.
  EcsView* updateView = ecs_world_view_t(world, UpdateView);
  for (EcsIterator* itr = ecs_view_itr(updateView); ecs_view_walk(itr);) {
    SceneBarkComp* bark = ecs_view_write_t(itr, SceneBarkComp);
    if (!bark->requests) {
      continue;
    }
    const EcsEntityId         instigator = ecs_view_entity(itr);
    const SceneTransformComp* trans      = ecs_view_read_t(itr, SceneTransformComp);
    const GeoVector           pos        = trans ? trans->position : geo_vector(0);
    bitset_for(bitset_from_var(bark->requests), barkTypeIndex) {
      registry_report(reg, instigator, (SceneBarkType)barkTypeIndex, bark, time, pos);
    }
    bark->requests = 0;
  }

  // Activate bark.
  SceneBarkEvent  barkEvent;
  const GeoVector listenerPos = bark_listener_position(world);
  if (time->time >= reg->nextBarkTime && registry_pop(reg, listenerPos, &barkEvent)) {
    bark_spawn(world, &barkEvent);
    reg->nextBarkTime = bark_next_time(time->time);
  }
}

ecs_module_init(scene_bark_module) {
  ecs_register_comp(SceneBarkRegistryComp, .destructor = ecs_destruct_registry_comp);
  ecs_register_comp(SceneBarkComp);

  ecs_register_system(
      SceneBarkUpdateSys,
      ecs_register_view(UpdateGlobalView),
      ecs_register_view(UpdateView),
      ecs_register_view(ListenerView));
}

String scene_bark_name(const SceneBarkType type) {
  diag_assert(type < SceneBarkType_Count);
  static const String g_names[] = {
      string_static("Death"),
      string_static("Confirm"),
  };
  ASSERT(array_elems(g_names) == SceneBarkType_Count, "Incorrect number of names");
  return g_names[type];
}

void scene_bark_request(SceneBarkComp* bark, const SceneBarkType type) {
  bark->requests |= 1 << type;
}
