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

#define proj_seek_angle_max (210.0f * math_deg_to_rad)
#define proj_seek_buildup_time time_milliseconds(750)

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

ecs_view_define(SeekTargetView) {
  ecs_access_read(SceneTransformComp);
  ecs_access_with(SceneCollisionComp);
}

static GeoVector seek_position(EcsIterator* entityItr) {
  const SceneTransformComp* trans = ecs_view_read_t(entityItr, SceneTransformComp);

  // TODO: Target offset should either be based on target collision bounds or be configurable.
  const GeoVector targetAimOffset = geo_vector(0, 1.0f, 0);
  return geo_vector_add(trans->position, targetAimOffset);
}

static void seek_towards_position(
    const SceneProjectileComp* proj, SceneTransformComp* projTrans, const f32 deltaSec) {
  const GeoVector seekDelta = geo_vector_sub(proj->seekPos, projTrans->position);
  const f32       seekDist  = geo_vector_mag(seekDelta);
  if (seekDist <= f32_epsilon) {
    return;
  }
  const GeoVector seekDir      = geo_vector_div(seekDelta, seekDist);
  const GeoQuat   seekRot      = geo_quat_look(seekDir, geo_up);
  const f32       seekStrength = math_min(1.0f, proj->age / (f32)proj_seek_buildup_time);
  geo_quat_towards(&projTrans->rotation, seekRot, seekStrength * proj_seek_angle_max * deltaSec);
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
    EcsWorld*                    world,
    const SceneCollisionEnvComp* colEnv,
    const SceneQueryFilter*      targetFilter,
    const EcsEntityId            projEntity,
    const SceneProjectileComp*   proj,
    const GeoVector              hitPos,
    const GeoVector              hitNormal,
    const EcsEntityId            hitEntity) {

  ecs_world_remove_t(world, projEntity, SceneProjectileComp);
  ecs_world_add_t(world, projEntity, SceneLifetimeDurationComp, .duration = proj->destroyDelay);

  if (proj->impactVfx) {
    projectile_impact_spawn(world, proj, hitPos, hitNormal);
  }

  EcsEntityId hits[scene_query_max_hits + 1];
  u32         hitCount = 0;

  // Find all targets in the damage radius.
  if (proj->damageRadius > f32_epsilon) {
    const GeoSphere damageSphere = {
        .point  = hitPos,
        .radius = proj->damageRadius,
    };
    hitCount = scene_query_sphere_all(colEnv, &damageSphere, targetFilter, hits);
  }

  // Add the entity we hit directly.
  if (hitEntity) {
    hits[hitCount++] = hitEntity;
  }

  // Damage all the found entities.
  for (u32 i = 0; i != hitCount; ++i) {
    const bool exists = ecs_world_exists(world, hits[i]);
    if (exists && ecs_world_has_t(world, hits[i], SceneHealthComp)) {
      scene_health_damage(world, hits[i], proj->damage);
    }
  }
}

ecs_system_define(SceneProjectileSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneCollisionEnvComp* colEnv   = ecs_view_read_t(globalItr, SceneCollisionEnvComp);
  const SceneTimeComp*         time     = ecs_view_read_t(globalItr, SceneTimeComp);
  const f32                    deltaSec = scene_delta_seconds(time);

  EcsIterator* seekTargetItr = ecs_view_itr(ecs_world_view_t(world, SeekTargetView));

  EcsView* projView = ecs_world_view_t(world, ProjectileView);
  for (EcsIterator* itr = ecs_view_itr_step(projView, parCount, parIndex); ecs_view_walk(itr);) {
    const EcsEntityId       entity  = ecs_view_entity(itr);
    SceneProjectileComp*    proj    = ecs_view_write_t(itr, SceneProjectileComp);
    SceneTransformComp*     trans   = ecs_view_write_t(itr, SceneTransformComp);
    const SceneFactionComp* faction = ecs_view_read_t(itr, SceneFactionComp);

    // Update age.
    proj->age += time->delta;

    // Optionally seek towards target.
    if (proj->flags & SceneProjectile_Seek) {
      if (ecs_view_maybe_jump(seekTargetItr, proj->seekEntity)) {
        proj->seekPos = seek_position(seekTargetItr);
      } else {
        /**
         * Seek target is missing, adjust the seek position downwards to guide the projectile
         * towards the ground.
         */
        proj->seekPos = geo_vector_sub(proj->seekPos, geo_vector(0, deltaSec, 0));
      }
      seek_towards_position(proj, trans, deltaSec);
    }

    const GeoVector        dir       = geo_quat_rotate(trans->rotation, geo_forward);
    const f32              deltaDist = proj->speed * deltaSec;
    const GeoRay           ray       = {.point = trans->position, .dir = dir};
    const QueryFilterCtx   filterCtx = {.instigator = entity};
    const SceneQueryFilter filter    = {
        .context   = &filterCtx,
        .callback  = &projectile_query_filter,
        .layerMask = projectile_query_layer_mask(faction),
    };

    // Test collisions with other entities.
    SceneRayHit hit;
    if (scene_query_ray(colEnv, &ray, deltaDist, &filter, &hit)) {
      trans->position = hit.position;
      projectile_hit(world, colEnv, &filter, entity, proj, hit.position, hit.normal, hit.entity);
      continue;
    }

    // Test collision with the ground.
    const GeoPlane groundPlane = {.normal = geo_up};
    const f32      groundHitT  = geo_plane_intersect_ray(&groundPlane, &ray);
    if (groundHitT >= 0 && groundHitT <= deltaDist) {
      const EcsEntityId hitEntity = 0;
      const GeoVector   hitPos    = geo_ray_position(&ray, groundHitT);
      trans->position             = hitPos;
      projectile_hit(world, colEnv, &filter, entity, proj, hitPos, groundPlane.normal, hitEntity);
      continue;
    }

    // Update position.
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
