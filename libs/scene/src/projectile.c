#include "ecs_world.h"
#include "scene_collision.h"
#include "scene_health.h"
#include "scene_projectile.h"
#include "scene_time.h"
#include "scene_transform.h"

ecs_comp_define_public(SceneProjectileComp);

ecs_view_define(GlobalView) {
  ecs_access_read(SceneCollisionEnvComp);
  ecs_access_read(SceneTimeComp);
}

ecs_view_define(ProjectileView) {
  ecs_access_read(SceneProjectileComp);
  ecs_access_write(SceneTransformComp);
}

ecs_view_define(TargetView) { ecs_access_write(SceneHealthComp); }

static bool projectile_query_filter(const void* context, const EcsEntityId entity) {
  const SceneProjectileComp* projectile = context;
  return entity != projectile->instigator;
}

static void projectile_damage(const SceneProjectileComp* projectile, const EcsIterator* targetItr) {
  SceneHealthComp* health = ecs_view_write_t(targetItr, SceneHealthComp);
  scene_health_damage(health, projectile->damage);
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
    const SceneProjectileComp* projectile = ecs_view_read_t(itr, SceneProjectileComp);
    SceneTransformComp*        trans      = ecs_view_write_t(itr, SceneTransformComp);

    const GeoVector dir       = geo_quat_rotate(trans->rotation, geo_forward);
    const f32       deltaDist = projectile->speed * deltaSeconds;
    const GeoRay    ray       = {.point = trans->position, .dir = dir};

    const SceneQueryFilter filter = {
        .context  = projectile,
        .callback = projectile_query_filter,
    };

    SceneRayHit hit;
    if (scene_query_ray(collisionEnv, &ray, &filter, &hit) && hit.time <= deltaDist) {
      ecs_world_entity_destroy(world, ecs_view_entity(itr));

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
