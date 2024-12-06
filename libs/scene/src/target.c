#include "core_alloc.h"
#include "core_array.h"
#include "core_dynarray.h"
#include "core_float.h"
#include "core_math.h"
#include "core_rng.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "scene_attack.h"
#include "scene_collision.h"
#include "scene_faction.h"
#include "scene_health.h"
#include "scene_location.h"
#include "scene_nav.h"
#include "scene_set.h"
#include "scene_target.h"
#include "scene_time.h"
#include "scene_transform.h"
#include "scene_visibility.h"

#define target_max_refresh_per_task 10
#define target_refresh_time_min time_seconds(1)
#define target_refresh_time_max time_seconds(2)
#define target_score_current_entity 0.1f
#define target_score_can_attack 0.2f
#define target_score_dist 1.0f
#define target_score_dir 0.25f
#define target_score_random 0.1f

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
  ecs_access_maybe_read(SceneLocationComp);
  ecs_access_maybe_read(SceneNavAgentComp);
  ecs_access_maybe_read(SceneScaleComp);
  ecs_access_maybe_write(SceneTargetTraceComp);
  ecs_access_read(SceneTransformComp);
  ecs_access_write(SceneTargetFinderComp);
}

ecs_view_define(TargetView) {
  ecs_access_maybe_read(SceneFactionComp);
  ecs_access_maybe_read(SceneLocationComp);
  ecs_access_maybe_read(SceneNavBlockerComp);
  ecs_access_maybe_read(SceneScaleComp);
  ecs_access_maybe_read(SceneVisibilityComp);
  ecs_access_read(SceneCollisionComp);
  ecs_access_read(SceneSetMemberComp);
  ecs_access_read(SceneTransformComp);
  ecs_access_with(SceneHealthComp);
}

static void target_trace_start(EcsWorld* world, const EcsEntityId entity) {
  ecs_world_add_t(
      world,
      entity,
      SceneTargetTraceComp,
      .scores = dynarray_create_t(g_allocHeap, SceneTargetScore, 128));
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

static GeoVector target_aim_source_pos(
    const SceneTransformComp* trans, const SceneScaleComp* scale, const SceneLocationComp* loc) {
  if (loc) {
    /**
     * TODO: At the moment we are using the center of the aim-target volume as an estimation of the
     * attack source position. This is obviously a very crude estimation, in the future we should
     * consider either sampling a joint or add a specific configurable entity location for this.
     */
    const GeoBoxRotated aimVolume = scene_location(loc, trans, scale, SceneLocationType_AimTarget);
    return geo_box_center(&aimVolume.box);
  }
  return trans->position;
}

static GeoVector target_aim_target_pos(
    const GeoVector           origin,
    const SceneTransformComp* tgtTrans,
    const SceneScaleComp*     tgtScale,
    const SceneLocationComp*  tgtLoc) {
  if (tgtLoc) {
    const GeoBoxRotated aimVolume =
        scene_location(tgtLoc, tgtTrans, tgtScale, SceneLocationType_AimTarget);
    return geo_box_rotated_closest_point(&aimVolume, origin);
  }
  return tgtTrans->position;
}

typedef struct {
  EcsEntityId finderEntity;
} TargetLineOfSightFilterCtx;

static bool target_los_filter(const void* ctx, const EcsEntityId entity, const u32 layer) {
  (void)layer;
  const TargetLineOfSightFilterCtx* losCtx = ctx;
  if (entity == losCtx->finderEntity) {
    return false; // Ignore collisions with yourself.
  }
  return true;
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

static bool target_reachable(
    const SceneNavEnvComp*   nav,
    const SceneNavAgentComp* finderAgent,
    const GeoVector          finderPos,
    EcsIterator*             targetItr) {
  if (!finderAgent) {
    return false; // Without a navigation agent we cannot reach any position.
  }
  const SceneNavLayer        layer            = finderAgent->layer;
  const GeoNavGrid*          grid             = scene_nav_grid(nav, layer);
  const GeoNavCell           finderNavCell    = geo_nav_at_position(grid, finderPos);
  const SceneNavBlockerComp* targetNavBlocker = ecs_view_read_t(targetItr, SceneNavBlockerComp);
  if (targetNavBlocker) {
    return geo_nav_blocker_reachable(grid, targetNavBlocker->ids[layer], finderNavCell);
  }
  const SceneTransformComp* targetTrans = ecs_view_read_t(targetItr, SceneTransformComp);
  return geo_nav_reachable(grid, finderNavCell, geo_nav_at_position(grid, targetTrans->position));
}

static f32 target_score(
    const EcsWorld*              world,
    const SceneCollisionEnvComp* collisionEnv,
    const SceneNavEnvComp*       navEnv,
    const SceneTargetFinderComp* finder,
    const EcsEntityId            finderEntity,
    const GeoVector              finderPosCenter,
    const GeoVector              finderAimDir,
    const SceneFaction           finderFaction,
    const SceneNavAgentComp*     finderNavAgent,
    const EcsEntityId            targetOld,
    EcsIterator*                 targetItr) {

  const SceneVisibilityComp* targetVisibility = ecs_view_read_t(targetItr, SceneVisibilityComp);
  if (targetVisibility && !scene_visible(targetVisibility, finderFaction)) {
    return 0.0f; // Target not visible.
  }

  const EcsEntityId         tgtEntity = ecs_view_entity(targetItr);
  const SceneTransformComp* tgtTrans  = ecs_view_read_t(targetItr, SceneTransformComp);
  const SceneScaleComp*     tgtScale  = ecs_view_read_t(targetItr, SceneScaleComp);
  const SceneLocationComp*  tgtLoc    = ecs_view_read_t(targetItr, SceneLocationComp);
  const GeoVector tgtPos   = target_aim_target_pos(finderPosCenter, tgtTrans, tgtScale, tgtLoc);
  const GeoVector toTarget = geo_vector_sub(tgtPos, finderPosCenter);
  const f32       distSqr  = geo_vector_mag_sqr(toTarget);
  if (distSqr < (finder->rangeMin * finder->rangeMin)) {
    return 0.0f; // Target too close.
  }
  if (distSqr > (finder->rangeMax * finder->rangeMax)) {
    return 0.0f; // Target too far away.
  }

  const bool excludeUnreachable = (finder->config & SceneTargetConfig_ExcludeUnreachable) != 0;
  if (excludeUnreachable && !target_reachable(navEnv, finderNavAgent, finderPosCenter, targetItr)) {
    return 0.0f; // Target unreachable.
  }
  const f32       dist = math_sqrt_f32(distSqr);
  const GeoVector dir  = dist > f32_epsilon ? geo_vector_div(toTarget, dist) : geo_forward;

  if (finder->config & SceneTargetConfig_ExcludeObscured) {
    const GeoRay                     ray       = {.point = finderPosCenter, .dir = dir};
    const TargetLineOfSightFilterCtx filterCtx = {.finderEntity = finderEntity};
    const SceneQueryFilter           filter    = {
                     .layerMask = SceneLayer_Environment | SceneLayer_Structure,
                     .callback  = target_los_filter,
                     .context   = &filterCtx,
    };
    SceneRayHit hit;
    if (scene_query_ray(collisionEnv, &ray, dist, &filter, &hit) && hit.entity != tgtEntity) {
      return 0.0f; // Target obscured.
    }
  }

  f32 score = 0.0f;
  if (tgtEntity == targetOld) {
    score += target_score_current_entity;
  }
  if (ecs_world_has_t(world, tgtEntity, SceneAttackComp)) {
    score += target_score_can_attack;
  }
  score += (1.0f - dist / finder->rangeMax) * target_score_dist;              // Distance score.
  score += math_max(0, geo_vector_dot(finderAimDir, dir)) * target_score_dir; // Direction score.
  score += rng_sample_f32(g_rng) * target_score_random;                       // Random score.
  return score;
}

static void target_queue_clear(SceneTargetFinderComp* finder) {
  mem_set(array_mem(finder->targetQueue), 0);
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
  const SceneCollisionEnvComp* colEnv = ecs_view_read_t(globalItr, SceneCollisionEnvComp);
  const SceneNavEnvComp*       navEnv = ecs_view_read_t(globalItr, SceneNavEnvComp);
  const SceneTimeComp*         time   = ecs_view_read_t(globalItr, SceneTimeComp);

  EcsView* finderView = ecs_world_view_t(world, TargetFinderView);
  EcsView* targetView = ecs_world_view_t(world, TargetView);

  // Only target entities in the 'unit' set.
  // TODO: Make this configurable.
  const StringHash targetSet = g_sceneSetUnit;

  // Limit the amount of refreshes per-frame, to avoid spikes when a large amount of units want to
  // refresh simultaneously.
  u32 refreshesRemaining = target_max_refresh_per_task;

  EcsIterator* targetItr = ecs_view_itr(targetView);
  for (EcsIterator* itr = ecs_view_itr_step(finderView, parCount, parIndex); ecs_view_walk(itr);) {
    const EcsEntityId         entity      = ecs_view_entity(itr);
    const SceneTransformComp* trans       = ecs_view_read_t(itr, SceneTransformComp);
    const SceneScaleComp*     scale       = ecs_view_read_t(itr, SceneScaleComp);
    const SceneLocationComp*  loc         = ecs_view_read_t(itr, SceneLocationComp);
    const SceneAttackAimComp* attackAim   = ecs_view_read_t(itr, SceneAttackAimComp);
    const SceneFactionComp*   factionComp = ecs_view_read_t(itr, SceneFactionComp);
    const SceneNavAgentComp*  navAgent    = ecs_view_read_t(itr, SceneNavAgentComp);
    SceneTargetFinderComp*    finder      = ecs_view_write_t(itr, SceneTargetFinderComp);
    SceneTargetTraceComp*     trace       = ecs_view_write_t(itr, SceneTargetTraceComp);
    const SceneFaction        faction     = factionComp ? factionComp->id : SceneFaction_None;

    if (!finder->nextRefreshTime) {
      finder->nextRefreshTime = target_next_refresh_time(time);
    }

    if ((finder->config & SceneTargetConfig_Trace) && !trace) {
      target_trace_start(world, entity);
    } else if (trace && !(finder->config & SceneTargetConfig_Trace)) {
      target_trace_stop(world, entity);
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
      const GeoVector   srcPos    = target_aim_source_pos(trans, scale, loc);
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
        const SceneSetMemberComp* setMember = ecs_view_read_t(targetItr, SceneSetMemberComp);
        if (!scene_set_member_contains(setMember, targetSet)) {
          continue; // Entities is not part of the set we target.
        }
        const f32 score = target_score(
            world,
            colEnv,
            navEnv,
            finder,
            entity,
            srcPos,
            aimDir,
            faction,
            navAgent,
            targetOld,
            targetItr);

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

    // Remove the primary target if its not longer valid.
    if (!ecs_view_contains(targetView, scene_target_primary(finder))) {
      target_queue_pop(finder);
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

  ecs_parallel(SceneTargetUpdateSys, g_jobsWorkerCount);
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
