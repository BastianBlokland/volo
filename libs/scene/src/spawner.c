#include "core_math.h"
#include "core_rng.h"
#include "ecs_world.h"
#include "scene_faction.h"
#include "scene_prefab.h"
#include "scene_spawner.h"
#include "scene_time.h"
#include "scene_transform.h"

ecs_comp_define_public(SceneSpawnerComp);
ecs_comp_define(SceneSpawnerInstanceComp) { EcsEntityId spawner; };

static GeoVector spawn_random_point_in_circle(const GeoVector center, const f32 radius) {
  const f32 r     = radius * math_sqrt_f32(rng_sample_f32(g_rng));
  const f32 theta = rng_sample_f32(g_rng) * 2 * math_pi_f32;
  return (GeoVector){
      .x = center.x + r * math_cos_f32(theta),
      .y = center.y,
      .z = center.z + r * math_sin_f32(theta),
  };
}

static void spawner_spawn(
    EcsWorld*               world,
    const SceneSpawnerComp* spawner,
    const EcsEntityId       spawnerEntity,
    const GeoVector         spawnerPos,
    const GeoQuat           spawnerRot,
    const SceneFaction      faction,
    const u32               spawnCount) {

  ScenePrefabSpec spec = {
      .prefabId = spawner->prefabId,
      .faction  = faction,
      .rotation = spawnerRot,
      .flags    = ScenePrefabFlags_SnapToTerrain,
  };
  for (u32 i = 0; i != spawnCount; ++i) {
    spec.position       = spawn_random_point_in_circle(spawnerPos, spawner->radius);
    const EcsEntityId e = scene_prefab_spawn(world, &spec);
    ecs_world_add_t(world, e, SceneSpawnerInstanceComp, .spawner = spawnerEntity);
  }
}

static u32 spawner_instance_count(EcsView* instanceView, const EcsEntityId spawnerEntity) {
  u32 res = 0;
  for (EcsIterator* itr = ecs_view_itr(instanceView); ecs_view_walk(itr);) {
    if (ecs_view_read_t(itr, SceneSpawnerInstanceComp)->spawner == spawnerEntity) {
      ++res;
    }
  }
  return res;
}

static TimeDuration spawner_next_time(const SceneSpawnerComp* spawner, const TimeDuration timeNow) {
  TimeDuration next = timeNow;
  next += (TimeDuration)rng_sample_range(g_rng, spawner->intervalMin, spawner->intervalMax);
  return next;
}

ecs_view_define(GlobalView) { ecs_access_read(SceneTimeComp); }

ecs_view_define(SpawnerUpdateView) {
  ecs_access_maybe_read(SceneFactionComp);
  ecs_access_maybe_read(SceneTransformComp);
  ecs_access_write(SceneSpawnerComp);
}

ecs_view_define(SpawnerInstanceView) { ecs_access_read(SceneSpawnerInstanceComp); }

ecs_system_define(SceneSpawnerUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneTimeComp* time = ecs_view_read_t(globalItr, SceneTimeComp);

  EcsView* updateView   = ecs_world_view_t(world, SpawnerUpdateView);
  EcsView* instanceView = ecs_world_view_t(world, SpawnerInstanceView);

  for (EcsIterator* itr = ecs_view_itr(updateView); ecs_view_walk(itr);) {
    const EcsEntityId         entity      = ecs_view_entity(itr);
    SceneSpawnerComp*         spawnerComp = ecs_view_write_t(itr, SceneSpawnerComp);
    const SceneTransformComp* transComp   = ecs_view_read_t(itr, SceneTransformComp);
    const SceneFactionComp*   factionComp = ecs_view_read_t(itr, SceneFactionComp);

    const bool hasInterval = spawnerComp->intervalMax > 0;
    if (!spawnerComp->nextTime && hasInterval) {
      spawnerComp->nextTime = spawner_next_time(spawnerComp, time->time);
    }
    if (time->time >= spawnerComp->nextTime) {
      const GeoVector    spawnerPos = LIKELY(transComp) ? transComp->position : geo_vector(0);
      const GeoQuat      spawnerRot = LIKELY(transComp) ? transComp->rotation : geo_quat_ident;
      const SceneFaction faction    = LIKELY(factionComp) ? factionComp->id : SceneFaction_None;

      const u32 instancesMax     = spawnerComp->maxInstances ? spawnerComp->maxInstances : u32_max;
      const u32 instancesCurrent = spawner_instance_count(instanceView, entity);

      if (instancesCurrent < instancesMax) {
        const u32 instancesRemaining = instancesMax - instancesCurrent;
        const u32 amountToSpawn      = math_min(instancesRemaining, spawnerComp->count);

        spawner_spawn(world, spawnerComp, entity, spawnerPos, spawnerRot, faction, amountToSpawn);
      }

      if (hasInterval) {
        spawnerComp->nextTime = spawner_next_time(spawnerComp, time->time);
      } else {
        spawnerComp->nextTime = time_days(99999);
      }
    }
  }
}

ecs_module_init(scene_spawner_module) {
  ecs_register_comp(SceneSpawnerComp);
  ecs_register_comp(SceneSpawnerInstanceComp);

  ecs_register_view(GlobalView);
  ecs_register_view(SpawnerUpdateView);
  ecs_register_view(SpawnerInstanceView);

  ecs_register_system(
      SceneSpawnerUpdateSys,
      ecs_view_id(GlobalView),
      ecs_view_id(SpawnerUpdateView),
      ecs_view_id(SpawnerInstanceView));
}
