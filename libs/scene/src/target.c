#include "core_float.h"
#include "core_rng.h"
#include "ecs_world.h"
#include "scene_collision.h"
#include "scene_faction.h"
#include "scene_health.h"
#include "scene_target.h"
#include "scene_time.h"
#include "scene_transform.h"

#define target_max_refresh_per_task 25
#define target_refresh_time_min time_seconds(1)
#define target_refresh_time_max time_seconds(2.5)
#define target_los_dist_min 1.0f
#define target_los_dist_max 75.0f

ecs_comp_define_public(SceneTargetFinderComp);

ecs_view_define(GlobalView) {
  ecs_access_read(SceneCollisionEnvComp);
  ecs_access_read(SceneTimeComp);
}

ecs_view_define(TargetFinderView) {
  ecs_access_maybe_read(SceneFactionComp);
  ecs_access_read(SceneTransformComp);
  ecs_access_write(SceneTargetFinderComp);
}

ecs_view_define(TargetView) {
  ecs_access_maybe_read(SceneFactionComp);
  ecs_access_read(SceneCollisionComp);
  ecs_access_read(SceneTransformComp);
  ecs_access_with(SceneHealthComp);
}

static GeoVector target_position_center(EcsIterator* entityItr) {
  const SceneTransformComp* trans = ecs_view_read_t(entityItr, SceneTransformComp);
  // TODO: Target position should either be based on target collision bounds or be configurable.
  return geo_vector_add(trans->position, geo_vector(0, 1.25f, 0));
}

typedef struct {
  bool hasLos;
  f32  distance;
} TargetLineOfSightInfo;

static TargetLineOfSightInfo target_los_query(
    const SceneCollisionEnvComp* collisionEnv,
    const f32                    radius,
    EcsIterator*                 finderItr,
    EcsIterator*                 targetItr) {
  const GeoVector sourcePos = target_position_center(finderItr);
  const GeoVector targetPos = target_position_center(targetItr);
  const GeoVector toTarget  = geo_vector_sub(targetPos, sourcePos);
  const f32       dist      = geo_vector_mag(toTarget);
  if (dist <= target_los_dist_min) {
    return (TargetLineOfSightInfo){.hasLos = true, .distance = dist};
  }
  if (dist > target_los_dist_max) {
    return (TargetLineOfSightInfo){.hasLos = false, .distance = dist};
  }

  const EcsEntityId      targetEntity = ecs_view_entity(targetItr);
  const SceneLayer       targetLayer  = ecs_view_read_t(targetItr, SceneCollisionComp)->layer;
  const SceneQueryFilter filter       = {.layerMask = SceneLayer_Environment | targetLayer};
  const GeoRay           ray          = {.point = sourcePos, .dir = geo_vector_div(toTarget, dist)};

  SceneRayHit hit;
  if (scene_query_ray_fat(collisionEnv, &ray, radius, dist, &filter, &hit)) {
    const bool hasLos = hit.entity == targetEntity;
    return (TargetLineOfSightInfo){
        .hasLos   = hasLos,
        .distance = hasLos ? hit.time : dist,
    };
  }

  // Target not found in the collision query, can happen if its collider hasn't been registered yet.
  return (TargetLineOfSightInfo){.hasLos = false, .distance = dist};
}

static bool
target_finder_needs_refresh(const SceneTargetFinderComp* finder, const SceneTimeComp* time) {
  if (finder->targetOverride) {
    return false;
  }
  return time->time >= finder->nextRefreshTime;
}

static TimeDuration target_next_refresh_time(const SceneTimeComp* time) {
  TimeDuration next = time->time;
  next += (TimeDuration)rng_sample_range(g_rng, target_refresh_time_min, target_refresh_time_max);
  return next;
}

static f32 target_score_sqr(
    const SceneTargetFinderComp* finder,
    const SceneTransformComp*    transA,
    const SceneTransformComp*    transB) {
  const GeoVector posDelta        = geo_vector_sub(transA->position, transB->position);
  const f32       distSqr         = geo_vector_mag_sqr(posDelta);
  const f32       maxDeviationSqr = finder->scoreRandomness * finder->scoreRandomness;
  return distSqr + rng_sample_f32(g_rng) * maxDeviationSqr;
}

ecs_system_define(SceneTargetUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneCollisionEnvComp* colEnv = ecs_view_read_t(globalItr, SceneCollisionEnvComp);
  const SceneTimeComp*         time   = ecs_view_read_t(globalItr, SceneTimeComp);

  EcsView* finderView = ecs_world_view_t(world, TargetFinderView);
  EcsView* targetView = ecs_world_view_t(world, TargetView);

  // Limit the amount of refreshes per-frame, to avoid spikes when a large amount of units want to
  // refresh simultaneously.
  u32 refreshesRemaining = target_max_refresh_per_task;

  EcsIterator* targetItr = ecs_view_itr(targetView);
  for (EcsIterator* itr = ecs_view_itr_step(finderView, parCount, parIndex); ecs_view_walk(itr);) {
    const EcsEntityId         entity  = ecs_view_entity(itr);
    const SceneTransformComp* trans   = ecs_view_read_t(itr, SceneTransformComp);
    const SceneFactionComp*   faction = ecs_view_read_t(itr, SceneFactionComp);
    SceneTargetFinderComp*    finder  = ecs_view_write_t(itr, SceneTargetFinderComp);

    if (finder->targetOverride) {
      finder->target = finder->targetOverride;
      finder->flags |= SceneTarget_Overriden;
    } else {
      finder->flags &= ~SceneTarget_Overriden;
    }

    /**
     * Refresh our target.
     * NOTE: Involves an expensive walk of all potential targets. In the future we should consider
     * using an acceleration structure, for example the collision environment.
     */
    if (refreshesRemaining && target_finder_needs_refresh(finder, time)) {
      finder->targetScoreSqr = f32_max;
      finder->target         = 0;
      for (ecs_view_itr_reset(targetItr); ecs_view_walk(targetItr);) {
        const EcsEntityId targetEntity = ecs_view_entity(targetItr);
        if (entity == targetEntity) {
          continue; // Do not target ourselves.
        }
        const SceneFactionComp* targetFaction = ecs_view_read_t(targetItr, SceneFactionComp);
        if (scene_is_friendly(faction, targetFaction)) {
          continue; // Do not target friendlies.
        }
        const SceneTransformComp* targetTrans = ecs_view_read_t(targetItr, SceneTransformComp);
        const f32                 scoreSqr    = target_score_sqr(finder, trans, targetTrans);
        if (scoreSqr < finder->targetScoreSqr) {
          finder->target         = targetEntity;
          finder->targetScoreSqr = scoreSqr;
        }
      }
      finder->nextRefreshTime = target_next_refresh_time(time);
      --refreshesRemaining;
    }

    /**
     * Update information about our target.
     */
    finder->flags &= ~SceneTarget_LineOfSight;
    if (ecs_view_contains(targetView, finder->target)) {
      ecs_view_jump(targetItr, finder->target);

      const f32                   losRadius = finder->lineOfSightRadius;
      const TargetLineOfSightInfo losInfo   = target_los_query(colEnv, losRadius, itr, targetItr);

      if (losInfo.hasLos) {
        finder->flags |= SceneTarget_LineOfSight;
      }

      const SceneTransformComp* targetTrans = ecs_view_read_t(targetItr, SceneTransformComp);
      finder->targetPosition                = targetTrans->position;
      finder->targetDistance                = losInfo.distance;
    } else {
      finder->targetOverride = 0;
      finder->target         = 0;
      if (finder->flags & SceneTarget_InstantRefreshOnIdle) {
        finder->nextRefreshTime = 0;
      }
    }
  }
}

ecs_module_init(scene_target_module) {
  ecs_register_comp(SceneTargetFinderComp);

  ecs_register_view(GlobalView);
  ecs_register_view(TargetFinderView);
  ecs_register_view(TargetView);

  ecs_register_system(
      SceneTargetUpdateSys,
      ecs_view_id(GlobalView),
      ecs_view_id(TargetFinderView),
      ecs_view_id(TargetView));

  ecs_parallel(SceneTargetUpdateSys, 4);
}
