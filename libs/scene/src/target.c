#include "core_alloc.h"
#include "core_dynarray.h"
#include "core_float.h"
#include "core_math.h"
#include "core_rng.h"
#include "ecs_world.h"
#include "scene_attack.h"
#include "scene_collision.h"
#include "scene_faction.h"
#include "scene_health.h"
#include "scene_nav.h"
#include "scene_target.h"
#include "scene_time.h"
#include "scene_transform.h"

#define target_max_refresh_per_task 25
#define target_refresh_time_min time_seconds(1)
#define target_refresh_time_max time_seconds(2.5)
#define target_score_current_entity 0.1f
#define target_score_distance 1.0f
#define target_score_direction 0.25f
#define target_los_dist_min 1.0f
#define target_los_dist_max 50.0f

ecs_comp_define_public(SceneTargetFinderComp);

ecs_comp_define(SceneTargetTraceComp) {
  DynArray scores; // SceneTargetScore[].
};

static void ecs_destruct_target_trace(void* data) {
  SceneTargetTraceComp* comp = data;
  dynarray_destroy(&comp->scores);
}

ecs_view_define(GlobalView) {
  ecs_access_read(SceneCollisionEnvComp);
  ecs_access_read(SceneNavEnvComp);
  ecs_access_read(SceneTimeComp);
}

ecs_view_define(TargetFinderView) {
  ecs_access_maybe_read(SceneAttackAimComp);
  ecs_access_maybe_read(SceneFactionComp);
  ecs_access_maybe_write(SceneTargetTraceComp);
  ecs_access_read(SceneTransformComp);
  ecs_access_write(SceneTargetFinderComp);
}

ecs_view_define(TargetView) {
  ecs_access_maybe_read(SceneFactionComp);
  ecs_access_maybe_read(SceneNavBlockerComp);
  ecs_access_read(SceneCollisionComp);
  ecs_access_read(SceneTransformComp);
  ecs_access_with(SceneHealthComp);
}

static void target_trace_start(EcsWorld* world, const EcsEntityId entity) {
  ecs_world_add_t(
      world,
      entity,
      SceneTargetTraceComp,
      .scores = dynarray_create_t(g_alloc_heap, SceneTargetScore, 128));
}

static void target_trace_stop(EcsWorld* world, const EcsEntityId entity) {
  ecs_world_remove_t(world, entity, SceneTargetTraceComp);
}

static void target_trace_clear(SceneTargetTraceComp* trace) { dynarray_clear(&trace->scores); }

static void target_trace_add(SceneTargetTraceComp* trace, const EcsEntityId e, const f32 score) {
  *dynarray_push_t(&trace->scores, SceneTargetScore) = (SceneTargetScore){
      .entity = e,
      .value  = score,
  };
}

static GeoVector target_position_center(const SceneTransformComp* trans) {
  // TODO: Target position should either be based on target collision bounds or be configurable.
  return geo_vector_add(trans->position, geo_vector(0, 1.25f, 0));
}

typedef struct {
  bool hasLos;
  f32  distance;
} TargetLineOfSightInfo;

static TargetLineOfSightInfo target_los_query(
    const SceneCollisionEnvComp* collisionEnv,
    const SceneTransformComp*    finderTrans,
    const f32                    radius,
    EcsIterator*                 targetItr) {
  const GeoVector           sourcePos   = target_position_center(finderTrans);
  const SceneTransformComp* targetTrans = ecs_view_read_t(targetItr, SceneTransformComp);
  const GeoVector           targetPos   = target_position_center(targetTrans);
  const GeoVector           toTarget    = geo_vector_sub(targetPos, sourcePos);
  const f32                 dist        = geo_vector_mag(toTarget);
  if (dist <= target_los_dist_min) {
    return (TargetLineOfSightInfo){.hasLos = true, .distance = dist};
  }
  if (dist > target_los_dist_max) {
    return (TargetLineOfSightInfo){.hasLos = false, .distance = dist};
  }

  const SceneLayer       targetLayer = ecs_view_read_t(targetItr, SceneCollisionComp)->layer;
  const SceneQueryFilter filter      = {.layerMask = SceneLayer_Environment | targetLayer};
  const GeoRay           ray         = {.point = sourcePos, .dir = geo_vector_div(toTarget, dist)};

  SceneRayHit hit;
  if (scene_query_ray_fat(collisionEnv, &ray, radius, dist, &filter, &hit)) {
    const bool hasLos = (hit.layer & targetLayer) != 0;
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

static bool
target_reachable(const SceneNavEnvComp* nav, const GeoVector finderPos, EcsIterator* targetItr) {

  const GeoNavCell           finderNavCell    = scene_nav_at_position(nav, finderPos);
  const SceneNavBlockerComp* targetNavBlocker = ecs_view_read_t(targetItr, SceneNavBlockerComp);
  if (targetNavBlocker) {
    return scene_nav_reachable_blocker(nav, finderNavCell, targetNavBlocker);
  }
  const SceneTransformComp* targetTrans = ecs_view_read_t(targetItr, SceneTransformComp);
  return scene_nav_reachable(nav, finderNavCell, scene_nav_at_position(nav, targetTrans->position));
}

static f32 target_score(
    const SceneNavEnvComp*       nav,
    const SceneTargetFinderComp* finder,
    const GeoVector              finderPosCenter,
    const GeoVector              finderAimDir,
    const EcsEntityId            targetOld,
    EcsIterator*                 targetItr) {

  const SceneTransformComp* targetTrans     = ecs_view_read_t(targetItr, SceneTransformComp);
  const GeoVector           targetPosCenter = target_position_center(targetTrans);
  const GeoVector           toTarget        = geo_vector_sub(targetPosCenter, finderPosCenter);
  const f32                 distanceSqr     = geo_vector_mag_sqr(toTarget);
  if (distanceSqr > (finder->distanceMax * finder->distanceMax)) {
    return 0.0f; // Target too far away.
  }

  const bool excludeUnreachable = (finder->flags & SceneTarget_ConfigExcludeUnreachable) != 0;
  if (excludeUnreachable && !target_reachable(nav, finderPosCenter, targetItr)) {
    return 0.0f; // Target unreachable.
  }

  const f32 distance = math_sqrt_f32(distanceSqr);

  f32 aimDirDot;
  if (distance > f32_epsilon) {
    aimDirDot = geo_vector_dot(finderAimDir, geo_vector_div(toTarget, distance));
  } else {
    aimDirDot = 1.0f;
  }

  f32 score = ecs_view_entity(targetItr) == targetOld ? target_score_current_entity : 0.0f;
  score += (1.0f - distance / finder->distanceMax) * target_score_distance; // Distance score.
  score += math_max(0, aimDirDot) * target_score_direction;                 // Direction score.
  score += rng_sample_f32(g_rng) * finder->scoreRandom;                     // Random score.
  return score;
}

ecs_system_define(SceneTargetUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneCollisionEnvComp* colEnv = ecs_view_read_t(globalItr, SceneCollisionEnvComp);
  const SceneNavEnvComp*       navEnv = ecs_view_read_t(globalItr, SceneNavEnvComp);
  const SceneTimeComp*         time   = ecs_view_read_t(globalItr, SceneTimeComp);

  EcsView* finderView = ecs_world_view_t(world, TargetFinderView);
  EcsView* targetView = ecs_world_view_t(world, TargetView);

  // Limit the amount of refreshes per-frame, to avoid spikes when a large amount of units want to
  // refresh simultaneously.
  u32 refreshesRemaining = target_max_refresh_per_task;

  EcsIterator* targetItr = ecs_view_itr(targetView);
  for (EcsIterator* itr = ecs_view_itr_step(finderView, parCount, parIndex); ecs_view_walk(itr);) {
    const EcsEntityId         entity    = ecs_view_entity(itr);
    const SceneTransformComp* trans     = ecs_view_read_t(itr, SceneTransformComp);
    const SceneAttackAimComp* attackAim = ecs_view_read_t(itr, SceneAttackAimComp);
    const SceneFactionComp*   faction   = ecs_view_read_t(itr, SceneFactionComp);
    SceneTargetFinderComp*    finder    = ecs_view_write_t(itr, SceneTargetFinderComp);
    SceneTargetTraceComp*     trace     = ecs_view_write_t(itr, SceneTargetTraceComp);

    if ((finder->flags & SceneTarget_ConfigTrace) && !trace) {
      target_trace_start(world, entity);
    } else if (trace && !(finder->flags & SceneTarget_ConfigTrace)) {
      target_trace_stop(world, entity);
    }

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
      if (trace) {
        target_trace_clear(trace);
      }
      const GeoVector   posCenter = target_position_center(trans);
      const GeoVector   aimDir    = scene_attack_aim_dir(trans, attackAim);
      const EcsEntityId targetOld = finder->target;
      finder->targetScore         = 0.0f;
      finder->target              = 0;
      for (ecs_view_itr_reset(targetItr); ecs_view_walk(targetItr);) {
        const EcsEntityId targetEntity = ecs_view_entity(targetItr);
        if (entity == targetEntity) {
          continue; // Do not target ourselves.
        }
        if (scene_is_friendly(faction, ecs_view_read_t(targetItr, SceneFactionComp))) {
          continue; // Do not target friendlies.
        }
        const f32 score = target_score(navEnv, finder, posCenter, aimDir, targetOld, targetItr);
        if (score > finder->targetScore) {
          finder->target      = targetEntity;
          finder->targetScore = score;
        }
        if (trace) {
          target_trace_add(trace, targetEntity, score);
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
      const TargetLineOfSightInfo losInfo   = target_los_query(colEnv, trans, losRadius, targetItr);

      if (losInfo.hasLos) {
        finder->flags |= SceneTarget_LineOfSight;
      }

      const SceneTransformComp* targetTrans = ecs_view_read_t(targetItr, SceneTransformComp);
      finder->targetPosition                = targetTrans->position;
      finder->targetDistance                = losInfo.distance;
    } else {
      finder->targetOverride = 0;
      finder->target         = 0;
      if (finder->flags & SceneTarget_ConfigInstantRefreshOnIdle) {
        finder->nextRefreshTime = 0;
      }
    }
  }
}

ecs_module_init(scene_target_module) {
  ecs_register_comp(SceneTargetFinderComp);
  ecs_register_comp(SceneTargetTraceComp, .destructor = ecs_destruct_target_trace);

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

const SceneTargetScore* scene_target_trace_begin(const SceneTargetTraceComp* comp) {
  return dynarray_begin_t(&comp->scores, SceneTargetScore);
}

const SceneTargetScore* scene_target_trace_end(const SceneTargetTraceComp* comp) {
  return dynarray_end_t(&comp->scores, SceneTargetScore);
}
