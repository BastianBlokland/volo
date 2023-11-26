#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_float.h"
#include "core_rng.h"
#include "ecs_world.h"
#include "scene_attachment.h"
#include "scene_lifetime.h"
#include "scene_prefab.h"
#include "scene_sound.h"
#include "scene_taunt.h"
#include "scene_time.h"
#include "scene_transform.h"

static const TimeDuration g_tauntEventDuration[SceneTauntType_Count] = {
    [SceneTauntType_Death]   = time_milliseconds(500),
    [SceneTauntType_Confirm] = time_milliseconds(750),
};

#define scene_taunt_cooldown_min time_seconds(2)
#define scene_taunt_cooldown_max time_seconds(3)
#define scene_taunt_distance_max 100.0f

typedef struct {
  i32          priority;
  StringHash   prefab;
  TimeDuration expireTimestamp;
  EcsEntityId  instigator;
  GeoVector    position;
} SceneTauntEvent;

ecs_comp_define(SceneTauntRegistryComp) {
  DynArray     events; // SceneTauntEvent[]
  TimeDuration nextTauntTime;
};

ecs_comp_define_public(SceneTauntComp);

static void ecs_destruct_registry_comp(void* data) {
  SceneTauntRegistryComp* registry = data;
  dynarray_destroy(&registry->events);
}

static SceneTauntRegistryComp* registry_init(EcsWorld* world) {
  return ecs_world_add_t(
      world,
      ecs_world_global(world),
      SceneTauntRegistryComp,
      .events = dynarray_create_t(g_alloc_heap, SceneTauntEvent, 64));
}

static void registry_prune(SceneTauntRegistryComp* reg, const TimeDuration timestamp) {
  for (usize i = reg->events.size; i-- != 0;) {
    const SceneTauntEvent* evt = dynarray_at_t(&reg->events, i, SceneTauntEvent);
    if (timestamp > evt->expireTimestamp) {
      dynarray_remove_unordered(&reg->events, i, 1);
    }
  }
}

static void registry_report(
    SceneTauntRegistryComp* reg,
    const EcsEntityId       instigator,
    const SceneTauntType    type,
    const SceneTauntComp*   taunt,
    const SceneTimeComp*    time,
    const GeoVector         pos) {
  diag_assert(g_tauntEventDuration[type]);
  if (taunt->tauntPrefabs[type]) {
    *dynarray_push_t(&reg->events, SceneTauntEvent) = (SceneTauntEvent){
        .priority        = taunt->priority,
        .prefab          = taunt->tauntPrefabs[type],
        .expireTimestamp = time->time + g_tauntEventDuration[type],
        .instigator      = instigator,
        .position        = pos,
    };
  }
}

static bool registry_pop(SceneTauntRegistryComp* reg, const GeoVector pos, SceneTauntEvent* out) {
  usize bestIndex = sentinel_usize;
  i32   bestPriority;
  f32   bestDistSqr;
  for (usize i = 0; i != reg->events.size; ++i) {
    const SceneTauntEvent* evt      = dynarray_at_t(&reg->events, i, SceneTauntEvent);
    const GeoVector        posDelta = geo_vector_sub(evt->position, pos);
    const f32              distSqr  = geo_vector_mag_sqr(posDelta);
    if (distSqr > (scene_taunt_distance_max * scene_taunt_distance_max)) {
      continue;
    }
    if (sentinel_check(bestIndex) || evt->priority > bestPriority || distSqr < bestDistSqr) {
      bestIndex    = i;
      bestPriority = evt->priority;
      bestDistSqr  = distSqr;
    }
  }
  if (!sentinel_check(bestIndex)) {
    *out = *dynarray_at_t(&reg->events, bestIndex, SceneTauntEvent);
    dynarray_remove_unordered(&reg->events, bestIndex, 1);
    return true;
  }
  return false;
}

static void taunt_spawn(EcsWorld* world, SceneTauntEvent* tauntEvent) {
  const EcsEntityId tauntEntity = scene_prefab_spawn(
      world,
      &(ScenePrefabSpec){
          .prefabId = tauntEvent->prefab,
          .faction  = SceneFaction_None,
          .position = tauntEvent->position,
          .rotation = geo_quat_ident});
  ecs_world_add_t(world, tauntEntity, SceneLifetimeOwnerComp, .owners[0] = tauntEvent->instigator);
  scene_attach_to_entity(world, tauntEntity, tauntEvent->instigator);
}

static TimeDuration taunt_next_time(const TimeDuration timeNow) {
  TimeDuration next = timeNow;
  next += (TimeDuration)rng_sample_range(g_rng, scene_taunt_cooldown_min, scene_taunt_cooldown_max);
  return next;
}

ecs_view_define(UpdateGlobalView) {
  ecs_access_read(SceneTimeComp);
  ecs_access_maybe_write(SceneTauntRegistryComp);
}

ecs_view_define(UpdateView) {
  ecs_access_write(SceneTauntComp);
  ecs_access_maybe_read(SceneTransformComp);
}

ecs_view_define(ListenerView) {
  ecs_access_with(SceneSoundListenerComp);
  ecs_access_read(SceneTransformComp);
}

static GeoVector taunt_listener_position(EcsWorld* world) {
  EcsView*     listenerView = ecs_world_view_t(world, ListenerView);
  EcsIterator* listenerItr  = ecs_view_first(listenerView);
  return listenerItr ? ecs_view_read_t(listenerItr, SceneTransformComp)->position : geo_vector(0);
}

ecs_system_define(SceneTauntUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, UpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneTimeComp* time = ecs_view_read_t(globalItr, SceneTimeComp);

  SceneTauntRegistryComp* reg = ecs_view_write_t(globalItr, SceneTauntRegistryComp);
  if (reg) {
    registry_prune(reg, time->time);
  } else {
    reg = registry_init(world);
  }

  // Generate taunt events.
  EcsView* updateView = ecs_world_view_t(world, UpdateView);
  for (EcsIterator* itr = ecs_view_itr(updateView); ecs_view_walk(itr);) {
    SceneTauntComp* taunt = ecs_view_write_t(itr, SceneTauntComp);
    if (!taunt->requests) {
      continue;
    }
    const EcsEntityId         instigator = ecs_view_entity(itr);
    const SceneTransformComp* trans      = ecs_view_read_t(itr, SceneTransformComp);
    const GeoVector           pos        = trans ? trans->position : geo_vector(0);
    bitset_for(bitset_from_var(taunt->requests), tauntTypeIndex) {
      registry_report(reg, instigator, (SceneTauntType)tauntTypeIndex, taunt, time, pos);
    }
    taunt->requests = 0;
  }

  // Activate taunt.
  SceneTauntEvent tauntEvent;
  const GeoVector listenerPos = taunt_listener_position(world);
  if (time->time >= reg->nextTauntTime && registry_pop(reg, listenerPos, &tauntEvent)) {
    taunt_spawn(world, &tauntEvent);
    reg->nextTauntTime = taunt_next_time(time->time);
  }
}

ecs_module_init(scene_taunt_module) {
  ecs_register_comp(SceneTauntRegistryComp, .destructor = ecs_destruct_registry_comp);
  ecs_register_comp(SceneTauntComp);

  ecs_register_system(
      SceneTauntUpdateSys,
      ecs_register_view(UpdateGlobalView),
      ecs_register_view(UpdateView),
      ecs_register_view(ListenerView));
}

void scene_taunt_request(SceneTauntComp* taunt, const SceneTauntType type) {
  taunt->requests |= 1 << type;
}
