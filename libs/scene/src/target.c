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
#define target_distance_deviation 15.0f

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
  ecs_access_read(SceneTransformComp);
  ecs_access_with(SceneCollisionComp);
  ecs_access_with(SceneHealthComp);
}

static GeoVector target_aim_position(EcsIterator* entityItr) {
  const SceneTransformComp* trans = ecs_view_read_t(entityItr, SceneTransformComp);
  return geo_vector_add(trans->position, geo_vector(0, 1.25f, 0));
}

static bool target_line_of_sight_test(
    const SceneCollisionEnvComp* collisionEnv, EcsIterator* finderItr, EcsIterator* targetItr) {
  const GeoVector sourcePos = target_aim_position(finderItr);
  const GeoVector targetPos = target_aim_position(targetItr);
  const GeoVector toTarget  = geo_vector_sub(targetPos, sourcePos);
  const f32       dist      = geo_vector_mag(toTarget);
  if (UNLIKELY(dist <= f32_epsilon)) {
    return true;
  }
  const SceneQueryFilter filter = {.layerMask = SceneLayer_Environment};
  const GeoRay           ray    = {.point = sourcePos, .dir = geo_vector_div(toTarget, dist)};
  SceneRayHit            hit;
  return !scene_query_ray(collisionEnv, &ray, dist, &filter, &hit);
}

static bool
target_finder_needs_refresh(const SceneTargetFinderComp* finder, const SceneTimeComp* time) {
  return time->time >= finder->nextRefreshTime;
}

static TimeDuration target_next_refresh_time(const SceneTimeComp* time) {
  TimeDuration next = time->time;
  next += (TimeDuration)rng_sample_range(g_rng, target_refresh_time_min, target_refresh_time_max);
  return next;
}

static f32 target_score_sqr(const SceneTransformComp* transA, const SceneTransformComp* transB) {
  const GeoVector posDelta        = geo_vector_sub(transA->position, transB->position);
  const f32       distSqr         = geo_vector_mag_sqr(posDelta);
  const f32       maxDeviationSqr = target_distance_deviation * target_distance_deviation;
  return distSqr + rng_sample_f32(g_rng) * maxDeviationSqr;
}

ecs_system_define(SceneTargetUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneCollisionEnvComp* collisionEnv = ecs_view_read_t(globalItr, SceneCollisionEnvComp);
  const SceneTimeComp*         time         = ecs_view_read_t(globalItr, SceneTimeComp);

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
        const f32                 scoreSqr    = target_score_sqr(trans, targetTrans);
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
    finder->targetFlags &= ~SceneTarget_LineOfSight;
    if (ecs_view_contains(targetView, finder->target)) {
      ecs_view_jump(targetItr, finder->target);

      const SceneTransformComp* targetTrans = ecs_view_read_t(targetItr, SceneTransformComp);
      finder->targetPosition                = targetTrans->position;

      if (target_line_of_sight_test(collisionEnv, itr, targetItr)) {
        finder->targetFlags |= SceneTarget_LineOfSight;
      }
    } else {
      finder->target = 0;
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
