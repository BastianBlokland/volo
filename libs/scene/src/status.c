#include "core_array.h"
#include "core_bits.h"
#include "core_bitset.h"
#include "core_diag.h"
#include "core_float.h"
#include "ecs_world.h"
#include "scene_attachment.h"
#include "scene_health.h"
#include "scene_lifetime.h"
#include "scene_prefab.h"
#include "scene_status.h"
#include "scene_tag.h"
#include "scene_time.h"
#include "scene_visibility.h"

static const f32 g_sceneStatusDamagePerSec[SceneStatusType_Count] = {
    [SceneStatusType_Burning]  = 50,
    [SceneStatusType_Bleeding] = 5,
};
static const String g_sceneStatusEffectPrefabs[SceneStatusType_Count] = {
    [SceneStatusType_Burning]  = string_static("EffectBurning"),
    [SceneStatusType_Bleeding] = string_static("EffectBleeding"),
};
static const TimeDuration g_sceneStatusTimeout[SceneStatusType_Count] = {
    [SceneStatusType_Burning]  = time_seconds(4),
    [SceneStatusType_Bleeding] = time_seconds(6),
};

ASSERT(SceneStatusType_Count <= bytes_to_bits(sizeof(SceneStatusMask)), "Status mask too small");

#define scene_status_effect_destroy_delay time_seconds(2)

ecs_comp_define_public(SceneStatusComp);
ecs_comp_define_public(SceneStatusRequestComp);

static void ecs_combine_status_request(void* dataA, void* dataB) {
  SceneStatusRequestComp* reqA = dataA;
  SceneStatusRequestComp* reqB = dataB;

  reqA->add |= reqB->add;
  reqA->remove |= reqB->remove;
  for (SceneStatusType type = 0; type != SceneStatusType_Count; ++type) {
    if (!reqA->instigators[type]) {
      reqA->instigators[type] = reqB->instigators[type];
    }
  }
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
          .flags    = ScenePrefabFlags_Volatile,
          .prefabId = string_hash(g_sceneStatusEffectPrefabs[type]), // TODO: Cache hashed name.
          .faction  = SceneFaction_None,
          .rotation = geo_quat_ident,
      });
  ecs_world_add_t(world, result, SceneLifetimeOwnerComp, .owners[0] = owner);
  if (status->effectJoint) {
    scene_attach_to_joint_name(world, result, owner, status->effectJoint);
  } else {
    scene_attach_to_entity(world, result, owner);
  }
  ecs_world_add_t(world, result, SceneVisibilityComp); // Seeing status-effects requires visibility.
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

  EcsView*     effectInstanceView = ecs_world_view_t(world, EffectInstanceView);
  EcsIterator* effectInstanceItr  = ecs_view_itr(effectInstanceView);

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
        status->instigators[typeIndex]     = request->instigators[typeIndex];
      }
      request->add = request->remove = 0;
      mem_set(array_mem(request->instigators), 0);
      effectsDirty = true;
    }

    // Process active types.
    bitset_for(bitset_from_var(status->active), typeIndex) {
      const SceneStatusType type             = (SceneStatusType)typeIndex;
      const TimeDuration    timeSinceRefresh = time->time - status->lastRefreshTime[type];
      if (g_sceneStatusTimeout[type] && timeSinceRefresh > g_sceneStatusTimeout[type]) {
        status->active &= ~(1 << type);
        effectsDirty = true;
      }
      if (damage && g_sceneStatusDamagePerSec[type] > 0) {
        scene_health_damage_add(
            damage,
            &(SceneDamageInfo){
                .instigator = status->instigators[type],
                .amount     = g_sceneStatusDamagePerSec[type] * deltaSec,
            });
      }
    }

    // Create / destroy effects.
    const bool isDead = health && (health->flags & SceneHealthFlags_Dead) != 0;
    if (effectsDirty || isDead) {
      for (SceneStatusType type = 0; type != SceneStatusType_Count; ++type) {
        const bool needsEffect = (status->active & (1 << type)) != 0 && !isDead;
        if (needsEffect && !status->effectEntities[type]) {
          status->effectEntities[type] = status_effect_create(world, entity, status, type);
        } else if (!needsEffect && status->effectEntities[type]) {
          if (ecs_view_maybe_jump(effectInstanceItr, status->effectEntities[type])) {
            ecs_view_write_t(effectInstanceItr, SceneTagComp)->tags &= ~SceneTags_Emit;
            ecs_world_add_t(
                world,
                status->effectEntities[type],
                SceneLifetimeDurationComp,
                .duration = scene_status_effect_destroy_delay);
          }
          status->effectEntities[type] = 0;
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
      string_static("Bleeding"),
  };
  ASSERT(array_elems(g_names) == SceneStatusType_Count, "Incorrect number of names");
  return g_names[type];
}

void scene_status_add(
    EcsWorld*             world,
    const EcsEntityId     target,
    const SceneStatusType type,
    const EcsEntityId     instigator) {
  scene_status_add_many(world, target, 1 << type, instigator);
}

void scene_status_add_many(
    EcsWorld*             world,
    const EcsEntityId     target,
    const SceneStatusMask mask,
    const EcsEntityId     instigator) {
  diag_assert(mask);

  SceneStatusRequestComp* req = ecs_world_add_t(world, target, SceneStatusRequestComp, .add = mask);
  bitset_for(bitset_from_var(mask), typeIndex) { req->instigators[typeIndex] = instigator; }
}

void scene_status_remove(EcsWorld* world, const EcsEntityId target, const SceneStatusType type) {
  scene_status_remove_many(world, target, 1 << type);
}

void scene_status_remove_many(
    EcsWorld* world, const EcsEntityId target, const SceneStatusMask mask) {
  diag_assert(mask);

  ecs_world_add_t(world, target, SceneStatusRequestComp, .remove = mask);
}
