#include "core_diag.h"
#include "core_math.h"
#include "ecs_world.h"
#include "scene_collision.h"
#include "scene_faction.h"
#include "scene_health.h"
#include "scene_lifetime.h"
#include "scene_projectile.h"
#include "scene_time.h"
#include "scene_transform.h"
#include "scene_vfx.h"

#define projectile_seek_angle_max (55.0f * math_deg_to_rad)

ecs_comp_define_public(SceneProjectileComp);

ecs_view_define(GlobalView) {
  ecs_access_read(SceneCollisionEnvComp);
  ecs_access_read(SceneTimeComp);
}

ecs_view_define(ProjectileView) {
  ecs_access_maybe_read(SceneFactionComp);
  ecs_access_write(SceneProjectileComp);
  ecs_access_write(SceneTransformComp);
}

ecs_view_define(SeekTargetView) { ecs_access_read(SceneTransformComp); }

static GeoVector seek_target_position(EcsIterator* entityItr) {
  const SceneTransformComp* trans = ecs_view_read_t(entityItr, SceneTransformComp);

  // TODO: Target offset should either be based on target collision bounds or be configurable.
  const GeoVector targetAimOffset = geo_vector(0, 1.0f, 0);
  return geo_vector_add(trans->position, targetAimOffset);
}

static void seek_apply(SceneTransformComp* trans, EcsIterator* seekTargetItr, const f32 deltaSec) {
  const GeoVector seekPos   = seek_target_position(seekTargetItr);
  const GeoVector seekDelta = geo_vector_sub(seekPos, trans->position);
  const f32       seekDist  = geo_vector_mag(seekDelta);
  if (seekDist <= f32_epsilon) {
    return;
  }
  const GeoVector seekDir = geo_vector_div(seekDelta, seekDist);
  const GeoQuat   seekRot = geo_quat_look(seekDir, geo_up);
  geo_quat_towards(&trans->rotation, seekRot, projectile_seek_angle_max * deltaSec);
}

typedef struct {
  EcsEntityId instigator;
} QueryFilterCtx;

static bool projectile_query_filter(const void* context, const EcsEntityId entity) {
  const QueryFilterCtx* ctx = context;
  if (entity == ctx->instigator) {
    return false;
  }
  return true;
}

static SceneLayer projectile_faction_ignore_layer(const SceneFaction faction) {
  switch (faction) {
  case SceneFaction_A:
    return SceneLayer_UnitFactionA;
  case SceneFaction_B:
    return SceneLayer_UnitFactionB;
  case SceneFaction_C:
    return SceneLayer_UnitFactionC;
  case SceneFaction_D:
    return SceneLayer_UnitFactionD;
  case SceneFaction_None:
    return SceneLayer_None;
  case SceneFaction_Count:
    break;
  }
  diag_crash_msg("Unsupported faction");
}

static SceneLayer projectile_query_layer_mask(const SceneFactionComp* faction) {
  SceneLayer layer = SceneLayer_Environment | SceneLayer_Unit;
  if (faction) {
    layer &= ~projectile_faction_ignore_layer(faction->id);
  }
  return layer;
}

static void projectile_impact_spawn(
    EcsWorld*                  world,
    const SceneProjectileComp* projectile,
    const GeoVector            pos,
    const GeoVector            norm) {
  const EcsEntityId e   = ecs_world_entity_create(world);
  const GeoQuat     rot = geo_quat_look(norm, geo_up);
  ecs_world_add_t(world, e, SceneTransformComp, .position = pos, .rotation = rot);
  ecs_world_add_t(world, e, SceneLifetimeDurationComp, .duration = projectile->impactLifetime);
  ecs_world_add_t(world, e, SceneVfxComp, .asset = projectile->impactVfx);
}

static void projectile_hit(
    EcsWorld*                  world,
    const EcsEntityId          projEntity,
    const SceneProjectileComp* proj,
    SceneTransformComp*        projTrans,
    const GeoVector            hitPos,
    const GeoVector            hitNormal,
    const EcsEntityId          hitEntity) {
  ecs_world_remove_t(world, projEntity, SceneProjectileComp);
  ecs_world_add_t(world, projEntity, SceneLifetimeDurationComp, .duration = proj->destroyDelay);
  projTrans->position = hitPos;

  if (proj->impactVfx) {
    projectile_impact_spawn(world, proj, hitPos, hitNormal);
  }
  const bool hitEntityExists = hitEntity && ecs_world_exists(world, hitEntity);
  if (hitEntityExists && ecs_world_has_t(world, hitEntity, SceneHealthComp)) {
    scene_health_damage(world, hitEntity, proj->damage);
  }
}

ecs_system_define(SceneProjectileSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneCollisionEnvComp* collisionEnv = ecs_view_read_t(globalItr, SceneCollisionEnvComp);
  const SceneTimeComp*         time         = ecs_view_read_t(globalItr, SceneTimeComp);
  const f32                    deltaSeconds = scene_delta_seconds(time);

  EcsIterator* seekTargetItr = ecs_view_itr(ecs_world_view_t(world, SeekTargetView));

  EcsView* projView = ecs_world_view_t(world, ProjectileView);
  for (EcsIterator* itr = ecs_view_itr_step(projView, parCount, parIndex); ecs_view_walk(itr);) {
    const EcsEntityId       entity  = ecs_view_entity(itr);
    SceneProjectileComp*    proj    = ecs_view_write_t(itr, SceneProjectileComp);
    SceneTransformComp*     trans   = ecs_view_write_t(itr, SceneTransformComp);
    const SceneFactionComp* faction = ecs_view_read_t(itr, SceneFactionComp);

    if (ecs_view_maybe_jump(seekTargetItr, proj->seekTarget)) {
      seek_apply(trans, seekTargetItr, deltaSeconds);
    }

    const GeoVector dir       = geo_quat_rotate(trans->rotation, geo_forward);
    const f32       deltaDist = proj->speed * deltaSeconds;
    const GeoRay    ray       = {.point = trans->position, .dir = dir};

    const QueryFilterCtx   filterCtx = {.instigator = entity};
    const SceneQueryFilter filter    = {
           .context   = &filterCtx,
           .callback  = &projectile_query_filter,
           .layerMask = projectile_query_layer_mask(faction),
    };

    SceneRayHit hit;
    if (scene_query_ray(collisionEnv, &ray, deltaDist, &filter, &hit)) {
      projectile_hit(world, entity, proj, trans, hit.position, hit.normal, hit.entity);
      continue;
    }

    const GeoPlane groundPlane = {.normal = geo_up};
    const f32      groundHitT  = geo_plane_intersect_ray(&groundPlane, &ray);
    if (groundHitT >= 0 && groundHitT <= deltaDist) {
      const EcsEntityId hitEntity = 0;
      const GeoVector   hitPos    = geo_ray_position(&ray, groundHitT);
      projectile_hit(world, entity, proj, trans, hitPos, groundPlane.normal, hitEntity);
      continue;
    }

    const GeoVector deltaPos = geo_vector_mul(dir, deltaDist);
    trans->position          = geo_vector_add(trans->position, deltaPos);
  }
}

ecs_module_init(scene_projectile_module) {
  ecs_register_comp(SceneProjectileComp);

  ecs_register_view(GlobalView);
  ecs_register_view(ProjectileView);
  ecs_register_view(SeekTargetView);

  ecs_register_system(
      SceneProjectileSys,
      ecs_view_id(GlobalView),
      ecs_view_id(ProjectileView),
      ecs_view_id(SeekTargetView));

  ecs_parallel(SceneProjectileSys, 4);
}
