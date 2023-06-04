#include "core_alloc.h"
#include "core_array.h"
#include "core_dynarray.h"
#include "core_float.h"
#include "core_math.h"
#include "core_rng.h"
#include "ecs_world.h"
#include "scene_attack.h"
#include "scene_collision.h"
#include "scene_faction.h"
#include "scene_health.h"
#include "scene_location.h"
#include "scene_nav.h"
#include "scene_target.h"
#include "scene_time.h"
#include "scene_transform.h"
#include "scene_unit.h"
#include "scene_visibility.h"

#define target_max_refresh_per_task 10
#define target_refresh_time_min time_seconds(1)
#define target_refresh_time_max time_seconds(2)
#define target_score_current_entity 0.1f
#define target_score_dist 1.0f
#define target_score_dir 0.25f
#define target_score_random 0.1f
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
  ecs_access_read(SceneVisibilityEnvComp);
}

ecs_view_define(TargetFinderView) {
  ecs_access_maybe_read(SceneAttackAimComp);
  ecs_access_maybe_read(SceneFactionComp);
  ecs_access_maybe_read(SceneLocationComp);
  ecs_access_maybe_write(SceneTargetTraceComp);
  ecs_access_read(SceneTransformComp);
  ecs_access_write(SceneTargetFinderComp);
}

ecs_view_define(TargetView) {
  ecs_access_maybe_read(SceneFactionComp);
  ecs_access_maybe_read(SceneLocationComp);
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

static GeoVector target_aim_pos(const SceneTransformComp* trans, const SceneLocationComp* loc) {
  if (loc) {
    // TODO: Take scale into account.
    return geo_vector_add(trans->position, loc->offsets[SceneLocationType_AimTarget]);
  }
  return trans->position;
}

typedef struct {
  bool hasLos;
  f32  distance;
} TargetLineOfSightInfo;

static TargetLineOfSightInfo target_los_query(
    const SceneCollisionEnvComp* collisionEnv,
    const SceneTransformComp*    finderTrans,
    const SceneLocationComp*     finderLoc,
    const f32                    radius,
    EcsIterator*                 targetItr) {
  const GeoVector           sourcePos   = target_aim_pos(finderTrans, finderLoc);
  const SceneTransformComp* targetTrans = ecs_view_read_t(targetItr, SceneTransformComp);
  const SceneLocationComp*  targetLoc   = ecs_view_read_t(targetItr, SceneLocationComp);
  const GeoVector           targetPos   = target_aim_pos(targetTrans, targetLoc);
  const GeoVector           toTarget    = geo_vector_sub(targetPos, sourcePos);
  const f32                 dist        = geo_vector_mag(toTarget);
  if (dist <= target_los_dist_min || radius <= f32_epsilon) {
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
    const SceneCollisionEnvComp*  collisionEnv,
    const SceneVisibilityEnvComp* visibilityEnv,
    const SceneNavEnvComp*        nav,
    const SceneTargetFinderComp*  finder,
    const GeoVector               finderPosCenter,
    const GeoVector               finderAimDir,
    const SceneFaction            finderFaction,
    const EcsEntityId             targetOld,
    EcsIterator*                  targetItr) {

  const SceneTransformComp* targetTrans     = ecs_view_read_t(targetItr, SceneTransformComp);
  const SceneLocationComp*  targetLoc       = ecs_view_read_t(targetItr, SceneLocationComp);
  const GeoVector           targetPosCenter = target_aim_pos(targetTrans, targetLoc);
  const GeoVector           toTarget        = geo_vector_sub(targetPosCenter, finderPosCenter);
  const f32                 distSqr         = geo_vector_mag_sqr(toTarget);
  if (distSqr < (finder->distanceMin * finder->distanceMin)) {
    return 0.0f; // Target too close.
  }
  if (distSqr > (finder->distanceMax * finder->distanceMax)) {
    return 0.0f; // Target too far away.
  }

  const bool excludeUnreachable = (finder->flags & SceneTarget_ConfigExcludeUnreachable) != 0;
  if (excludeUnreachable && !target_reachable(nav, finderPosCenter, targetItr)) {
    return 0.0f; // Target unreachable.
  }
  const f32       dist = math_sqrt_f32(distSqr);
  const GeoVector dir  = dist > f32_epsilon ? geo_vector_div(toTarget, dist) : geo_forward;

  if (finder->flags & SceneTarget_ConfigExcludeObscured) {
    const SceneQueryFilter filter = {.layerMask = SceneLayer_Environment};
    const GeoRay           ray    = {.point = finderPosCenter, .dir = dir};
    SceneRayHit            hit;
    if (scene_query_ray(collisionEnv, &ray, dist, &filter, &hit)) {
      return 0.0f; // Target obscured.
    }
  }

  if (!scene_visible(visibilityEnv, finderFaction, targetPosCenter)) {
    return 0.0f; // Target not visible.
  }

  f32 score = ecs_view_entity(targetItr) == targetOld ? target_score_current_entity : 0.0f;
  score += (1.0f - dist / finder->distanceMax) * target_score_dist;           // Distance score.
  score += math_max(0, geo_vector_dot(finderAimDir, dir)) * target_score_dir; // Direction score.
  score += rng_sample_f32(g_rng) * target_score_random;                       // Random score.
  return score;
}

static void target_queue_clear(SceneTargetFinderComp* finder) {
  mem_set(array_mem(finder->targetQueue), 0);
}

static void target_queue_set(SceneTargetFinderComp* finder, const EcsEntityId target) {
  target_queue_clear(finder);
  finder->targetQueue[0] = target;
}

static bool target_queue_pop(SceneTargetFinderComp* finder) {
  array_for_t(finder->targetQueue, EcsEntityId, target) {
    if (*target) {
      *target = 0;
      return true;
    }
  }
  return false;
}

ecs_system_define(SceneTargetUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneCollisionEnvComp*  colEnv = ecs_view_read_t(globalItr, SceneCollisionEnvComp);
  const SceneNavEnvComp*        navEnv = ecs_view_read_t(globalItr, SceneNavEnvComp);
  const SceneTimeComp*          time   = ecs_view_read_t(globalItr, SceneTimeComp);
  const SceneVisibilityEnvComp* visEnv = ecs_view_read_t(globalItr, SceneVisibilityEnvComp);

  EcsView* finderView = ecs_world_view_t(world, TargetFinderView);
  EcsView* targetView = ecs_world_view_t(world, TargetView);

  // Limit the amount of refreshes per-frame, to avoid spikes when a large amount of units want to
  // refresh simultaneously.
  u32 refreshesRemaining = target_max_refresh_per_task;

  EcsIterator* targetItr = ecs_view_itr(targetView);
  for (EcsIterator* itr = ecs_view_itr_step(finderView, parCount, parIndex); ecs_view_walk(itr);) {
    const EcsEntityId         entity      = ecs_view_entity(itr);
    const SceneTransformComp* trans       = ecs_view_read_t(itr, SceneTransformComp);
    const SceneLocationComp*  loc         = ecs_view_read_t(itr, SceneLocationComp);
    const SceneAttackAimComp* attackAim   = ecs_view_read_t(itr, SceneAttackAimComp);
    const SceneFactionComp*   factionComp = ecs_view_read_t(itr, SceneFactionComp);
    SceneTargetFinderComp*    finder      = ecs_view_write_t(itr, SceneTargetFinderComp);
    SceneTargetTraceComp*     trace       = ecs_view_write_t(itr, SceneTargetTraceComp);
    const SceneFaction        faction     = factionComp ? factionComp->id : SceneFaction_None;

    if ((finder->flags & SceneTarget_ConfigTrace) && !trace) {
      target_trace_start(world, entity);
    } else if (trace && !(finder->flags & SceneTarget_ConfigTrace)) {
      target_trace_stop(world, entity);
    }

    if (finder->targetOverride) {
      target_queue_set(finder, finder->targetOverride);
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
      const GeoVector   pos       = target_aim_pos(trans, loc);
      const GeoVector   aimDir    = scene_attack_aim_dir(trans, attackAim);
      const EcsEntityId targetOld = scene_target_primary(finder);

      target_queue_clear(finder);
      f32 scores[scene_target_queue_size] = {0};
      for (ecs_view_itr_reset(targetItr); ecs_view_walk(targetItr);) {
        const EcsEntityId targetEntity = ecs_view_entity(targetItr);
        if (entity == targetEntity) {
          continue; // Do not target ourselves.
        }
        if (scene_is_friendly(factionComp, ecs_view_read_t(targetItr, SceneFactionComp))) {
          continue; // Do not target friendlies.
        }
        if (!ecs_world_has_t(world, targetEntity, SceneUnitComp)) {
          continue; // Only auto-target units.
        }
        const f32 score = target_score(
            colEnv, visEnv, navEnv, finder, pos, aimDir, faction, targetOld, targetItr);

        // Insert into the target queue.
        for (u32 i = 0; i != scene_target_queue_size; ++i) {
          if (score > scores[i]) {
            scores[i]              = score;
            finder->targetQueue[i] = targetEntity;
            break;
          }
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
    const EcsEntityId primaryTarget = scene_target_primary(finder);
    finder->flags &= ~SceneTarget_LineOfSight;
    if (ecs_view_contains(targetView, primaryTarget)) {
      ecs_view_jump(targetItr, primaryTarget);

      const f32                   losRadius = finder->lineOfSightRadius;
      const TargetLineOfSightInfo losInfo =
          target_los_query(colEnv, trans, loc, losRadius, targetItr);

      if (losInfo.hasLos) {
        finder->flags |= SceneTarget_LineOfSight;
      }

      const SceneTransformComp* targetTrans = ecs_view_read_t(targetItr, SceneTransformComp);
      finder->targetPosition                = targetTrans->position;
      finder->targetDistance                = losInfo.distance;
    } else {
      target_queue_pop(finder);
      finder->targetOverride = 0;
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

EcsEntityId scene_target_primary(const SceneTargetFinderComp* finder) {
  for (u32 i = 0; i != (scene_target_queue_size - 1); ++i) {
    if (finder->targetQueue[i]) {
      return finder->targetQueue[i];
    }
  }
  return finder->targetQueue[scene_target_queue_size - 1];
}

bool scene_target_contains(const SceneTargetFinderComp* finder, const EcsEntityId entity) {
  array_for_t(finder->targetQueue, EcsEntityId, target) {
    if (*target == entity) {
      return true;
    }
  }
  return false;
}

const SceneTargetScore* scene_target_trace_begin(const SceneTargetTraceComp* comp) {
  return dynarray_begin_t(&comp->scores, SceneTargetScore);
}

const SceneTargetScore* scene_target_trace_end(const SceneTargetTraceComp* comp) {
  return dynarray_end_t(&comp->scores, SceneTargetScore);
}
