#include "core_array.h"
#include "core_bits.h"
#include "core_bitset.h"
#include "ecs_world.h"
#include "scene_attachment.h"
#include "scene_health.h"
#include "scene_lifetime.h"
#include "scene_prefab.h"
#include "scene_status.h"
#include "scene_tag.h"
#include "scene_time.h"

static const f32 g_sceneStatusDamagePerSec[SceneStatusType_Count] = {
    [SceneStatusType_Burning] = 10,
};
static const String g_sceneStatusEffectPrefabs[SceneStatusType_Count] = {
    [SceneStatusType_Burning] = string_static("EffectBurning"),
};
static const TimeDuration g_sceneStatusTimeout[SceneStatusType_Count] = {
    [SceneStatusType_Burning] = time_seconds(5),
};

ASSERT(SceneStatusType_Count <= bytes_to_bits(sizeof(SceneStatusMask)), "Status mask too small");

ecs_comp_define_public(SceneStatusComp);
ecs_comp_define_public(SceneStatusRequestComp);

static void ecs_combine_status_request(void* dataA, void* dataB) {
  SceneStatusRequestComp* reqA = dataA;
  SceneStatusRequestComp* reqB = dataB;
  reqA->add |= reqB->add;
  reqA->remove |= reqB->remove;
}

static EcsEntityId status_effect_create(
    EcsWorld*              world,
    const EcsEntityId      owner,
    const SceneStatusComp* status,
    const SceneStatusType  type) {
  if (string_is_empty(g_sceneStatusEffectPrefabs[type])) {
    return 0;
  }
  const EcsEntityId result = scene_prefab_spawn(
      world,
      &(ScenePrefabSpec){
          .prefabId = string_hash(g_sceneStatusEffectPrefabs[type]), // TODO: Cache hashed name.
          .faction  = SceneFaction_None,
          .rotation = geo_quat_ident,
      });
  ecs_world_add_t(world, result, SceneLifetimeOwnerComp, .owners[0] = owner);
  ecs_world_add_t(
      world,
      result,
      SceneAttachmentComp,
      .target     = owner,
      .jointIndex = sentinel_u32,
      .jointName  = status->effectJoint);
  return result;
}

ecs_view_define(GlobalView) { ecs_access_read(SceneTimeComp); }

ecs_view_define(StatusView) {
  ecs_access_maybe_read(SceneHealthComp);
  ecs_access_maybe_write(SceneDamageComp);
  ecs_access_write(SceneStatusComp);
  ecs_access_write(SceneStatusRequestComp);
}

ecs_view_define(EffectInstanceView) { ecs_access_write(SceneTagComp); }

ecs_system_define(SceneStatusUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneTimeComp* time     = ecs_view_read_t(globalItr, SceneTimeComp);
  const f32            deltaSec = scene_delta_seconds(time);

  EcsView*     instanceView = ecs_world_view_t(world, EffectInstanceView);
  EcsIterator* instanceItr  = ecs_view_itr(instanceView);

  EcsView* statusView = ecs_world_view_t(world, StatusView);
  for (EcsIterator* itr = ecs_view_itr(statusView); ecs_view_walk(itr);) {
    const EcsEntityId       entity  = ecs_view_entity(itr);
    SceneStatusRequestComp* request = ecs_view_write_t(itr, SceneStatusRequestComp);
    SceneStatusComp*        status  = ecs_view_write_t(itr, SceneStatusComp);
    const SceneHealthComp*  health  = ecs_view_read_t(itr, SceneHealthComp);
    SceneDamageComp*        damage  = ecs_view_write_t(itr, SceneDamageComp);

    // Apply the requests.
    bool effectsDirty = false;
    if (request->add || request->remove) {
      status->active |= (request->add & status->supported);
      status->active &= ~request->remove;
      bitset_for(bitset_from_var(request->add), typeIndex) {
        status->lastRefreshTime[typeIndex] = time->time;
      }
      request->add = request->remove = 0;
      effectsDirty                   = true;
    }

    // Process active types.
    bitset_for(bitset_from_var(status->active), typeIndex) {
      const SceneStatusType type             = (SceneStatusType)typeIndex;
      const TimeDuration    timeSinceRefresh = time->time - status->lastRefreshTime[type];
      if (g_sceneStatusTimeout[type] && timeSinceRefresh > g_sceneStatusTimeout[type]) {
        status->active &= ~(1 << type);
        effectsDirty = true;
      }
      if (damage) {
        damage->amount += g_sceneStatusDamagePerSec[type] * deltaSec;
      }
      if (!status->effectEntities[type]) {
        status->effectEntities[type] = status_effect_create(world, entity, status, type);
      }
    }

    // Enable / disable effects.
    const bool isDead = health && (health->flags & SceneHealthFlags_Dead) != 0;
    if (effectsDirty || isDead) {
      for (SceneStatusType type = 0; type != SceneStatusType_Count; ++type) {
        if (ecs_view_maybe_jump(instanceItr, status->effectEntities[type])) {
          SceneTagComp* tagComp = ecs_view_write_t(instanceItr, SceneTagComp);
          if ((status->active & (1 << type)) && !isDead) {
            tagComp->tags |= SceneTags_Emit;
          } else {
            tagComp->tags &= ~SceneTags_Emit;
          }
        }
      }
    }
  }
}

ecs_module_init(scene_status_module) {
  ecs_register_comp(SceneStatusComp);
  ecs_register_comp(SceneStatusRequestComp, .combinator = ecs_combine_status_request);

  ecs_register_view(GlobalView);
  ecs_register_view(StatusView);
  ecs_register_view(EffectInstanceView);

  ecs_register_system(
      SceneStatusUpdateSys,
      ecs_view_id(GlobalView),
      ecs_view_id(StatusView),
      ecs_view_id(EffectInstanceView));
}

bool scene_status_active(const SceneStatusComp* status, const SceneStatusType type) {
  return (status->active & (1 << type)) != 0;
}

String scene_status_name(const SceneStatusType type) {
  static const String g_names[] = {
      string_static("Burning"),
  };
  ASSERT(array_elems(g_names) == SceneStatusType_Count, "Incorrect number of names");
  return g_names[type];
}

void scene_status_add(EcsWorld* world, const EcsEntityId target, const SceneStatusType type) {
  ecs_world_add_t(world, target, SceneStatusRequestComp, .add = 1 << type);
}

void scene_status_remove(EcsWorld* world, const EcsEntityId target, const SceneStatusType type) {
  ecs_world_add_t(world, target, SceneStatusRequestComp, .remove = 1 << type);
}
