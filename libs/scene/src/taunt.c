#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "ecs_world.h"
#include "scene_taunt.h"
#include "scene_time.h"
#include "scene_transform.h"

static const TimeDuration g_tauntEventDuration[SceneTauntType_Count] = {
    [SceneTauntType_Death] = time_seconds(1),
};

typedef struct {
  SceneTauntType type;
  StringHash     prefab;
  TimeDuration   expireTimestamp;
  EcsEntityId    instigator;
  GeoVector      position;
} SceneTauntEvent;

ecs_comp_define(SceneTauntRegistryComp) {
  DynArray events; // SceneTauntEvent[]
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

  *dynarray_push_t(&reg->events, SceneTauntEvent) = (SceneTauntEvent){
      .type            = type,
      .prefab          = taunt->tauntPrefabs[type],
      .expireTimestamp = time->time + g_tauntEventDuration[type],
      .instigator      = instigator,
      .position        = pos,
  };
}

ecs_view_define(UpdateGlobalView) {
  ecs_access_read(SceneTimeComp);
  ecs_access_maybe_write(SceneTauntRegistryComp);
}

ecs_view_define(UpdateView) {
  ecs_access_write(SceneTauntComp);
  ecs_access_maybe_read(SceneTransformComp);
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
    if (taunt->requests & SceneTauntRequests_Death) {
      registry_report(reg, instigator, SceneTauntType_Death, taunt, time, pos);
    }
    taunt->requests = 0;
  }
}

ecs_module_init(scene_taunt_module) {
  ecs_register_comp(SceneTauntRegistryComp, .destructor = ecs_destruct_registry_comp);
  ecs_register_comp(SceneTauntComp);

  ecs_register_system(
      SceneTauntUpdateSys, ecs_register_view(UpdateGlobalView), ecs_register_view(UpdateView));
}
