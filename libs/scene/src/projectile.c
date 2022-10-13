#include "core_diag.h"
#include "ecs_world.h"
#include "scene_collision.h"
#include "scene_faction.h"
#include "scene_health.h"
#include "scene_lifetime.h"
#include "scene_projectile.h"
#include "scene_time.h"
#include "scene_transform.h"
#include "scene_vfx.h"

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

ecs_view_define(TargetView) { ecs_access_write(SceneHealthComp); }

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
  case SceneFaction_Count:
  case SceneFaction_None:
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

static void projectile_damage(const SceneProjectileComp* projectile, const EcsIterator* targetItr) {
  SceneHealthComp* health = ecs_view_write_t(targetItr, SceneHealthComp);
  scene_health_damage(health, projectile->damage);
}

static void projectile_impact_spawn(
    EcsWorld*                  world,
    const SceneProjectileComp* projectile,
    const GeoVector            pos,
    const GeoVector            norm) {
  const EcsEntityId e   = ecs_world_entity_create(world);
  const GeoQuat     rot = geo_quat_look(norm, geo_up);
  ecs_world_add_t(world, e, SceneTransformComp, .position = pos, .rotation = rot);
  ecs_world_add_t(world, e, SceneLifetimeDurationComp, .duration = time_milliseconds(200));
  ecs_world_add_t(world, e, SceneVfxComp, .asset = projectile->impactVfx);
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

  EcsIterator* targetItr = ecs_view_itr(ecs_world_view_t(world, TargetView));

  EcsView* projectileView = ecs_world_view_t(world, ProjectileView);
  for (EcsIterator* itr = ecs_view_itr(projectileView); ecs_view_walk(itr);) {
    const EcsEntityId       entity     = ecs_view_entity(itr);
    SceneProjectileComp*    projectile = ecs_view_write_t(itr, SceneProjectileComp);
    SceneTransformComp*     trans      = ecs_view_write_t(itr, SceneTransformComp);
    const SceneFactionComp* faction    = ecs_view_read_t(itr, SceneFactionComp);

    if (projectile->delay > 0) {
      projectile->delay -= time->delta;
      continue;
    }

    const GeoVector dir       = geo_quat_rotate(trans->rotation, geo_forward);
    const f32       deltaDist = projectile->speed * deltaSeconds;
    const GeoRay    ray       = {.point = trans->position, .dir = dir};

    const QueryFilterCtx   filterCtx = {.instigator = entity};
    const SceneQueryFilter filter    = {
        .context   = &filterCtx,
        .callback  = &projectile_query_filter,
        .layerMask = projectile_query_layer_mask(faction),
    };

    SceneRayHit hit;
    if (scene_query_ray(collisionEnv, &ray, deltaDist, &filter, &hit)) {
      ecs_world_remove_t(world, entity, SceneProjectileComp);
      ecs_world_add_t(world, entity, SceneLifetimeDurationComp, .duration = time_milliseconds(150));
      trans->position = hit.position;

      if (projectile->impactVfx) {
        projectile_impact_spawn(world, projectile, hit.position, hit.normal);
      }
      if (ecs_view_maybe_jump(targetItr, hit.entity)) {
        projectile_damage(projectile, targetItr);
      }
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
  ecs_register_view(TargetView);

  ecs_register_system(
      SceneProjectileSys,
      ecs_view_id(GlobalView),
      ecs_view_id(ProjectileView),
      ecs_view_id(TargetView));
}
