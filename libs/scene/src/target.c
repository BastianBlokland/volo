#include "core_float.h"
#include "ecs_world.h"
#include "scene_collision.h"
#include "scene_faction.h"
#include "scene_health.h"
#include "scene_target.h"
#include "scene_transform.h"

ecs_comp_define_public(SceneTargetFinderComp);

ecs_view_define(GlobalView) { ecs_access_read(SceneCollisionEnvComp); }

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

static GeoVector aim_position(EcsIterator* entityItr) {
  const SceneTransformComp* trans = ecs_view_read_t(entityItr, SceneTransformComp);
  return geo_vector_add(trans->position, geo_vector(0, 1.25f, 0));
}

static bool line_of_sight_filter(const void* context, const EcsEntityId entity) {
  const EcsEntityId* entityPtr = context;
  return entity != *entityPtr;
}

static bool line_of_sight_test(
    const SceneCollisionEnvComp* collisionEnv, EcsIterator* finderItr, EcsIterator* targetItr) {
  const EcsEntityId finderEntity = ecs_view_entity(finderItr);
  const EcsEntityId targetEntity = ecs_view_entity(targetItr);
  const GeoVector   sourcePos    = aim_position(finderItr);
  const GeoVector   targetPos    = aim_position(targetItr);
  const GeoVector   toTarget     = geo_vector_sub(targetPos, sourcePos);
  const f32         dist         = geo_vector_mag(toTarget);
  if (UNLIKELY(dist <= f32_epsilon)) {
    return true;
  }
  const SceneQueryFilter filter = {
      .context   = &finderEntity,
      .callback  = &line_of_sight_filter,
      .layerMask = SceneLayer_All,
  };
  const GeoRay ray = {.point = sourcePos, .dir = geo_vector_div(toTarget, dist)};
  SceneRayHit  hit;
  if (scene_query_ray(collisionEnv, &ray, &filter, &hit)) {
    return hit.time >= dist || hit.entity == targetEntity;
  }
  return false;
}

ecs_system_define(SceneTargetUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneCollisionEnvComp* collisionEnv = ecs_view_read_t(globalItr, SceneCollisionEnvComp);

  EcsView* finderView = ecs_world_view_t(world, TargetFinderView);
  EcsView* targetView = ecs_world_view_t(world, TargetView);

  EcsIterator* targetItr = ecs_view_itr(targetView);
  for (EcsIterator* finderItr = ecs_view_itr(finderView); ecs_view_walk(finderItr);) {
    const EcsEntityId         entity  = ecs_view_entity(finderItr);
    const SceneTransformComp* trans   = ecs_view_read_t(finderItr, SceneTransformComp);
    const SceneFactionComp*   faction = ecs_view_read_t(finderItr, SceneFactionComp);
    SceneTargetFinderComp*    finder  = ecs_view_write_t(finderItr, SceneTargetFinderComp);

    /**
     * Find the closest target.
     */
    finder->targetDistSqr = f32_max;
    finder->target        = 0;
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
      const GeoVector           posDelta = geo_vector_sub(targetTrans->position, trans->position);
      const f32                 distSqr  = geo_vector_mag_sqr(posDelta);
      if (distSqr < finder->targetDistSqr) {
        finder->target         = targetEntity;
        finder->targetPosition = targetTrans->position;
        finder->targetDistSqr  = distSqr;
      }
    }

    /**
     * Test Line-Of-Sight to the closest target.
     */
    finder->targetFlags &= ~SceneTarget_LineOfSight;
    if (finder->target) {
      ecs_view_jump(targetItr, finder->target);
      if (line_of_sight_test(collisionEnv, finderItr, targetItr)) {
        finder->targetFlags |= SceneTarget_LineOfSight;
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
}
