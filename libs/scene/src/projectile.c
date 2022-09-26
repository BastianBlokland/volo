#include "ecs_world.h"
#include "scene_projectile.h"
#include "scene_time.h"
#include "scene_transform.h"

ecs_comp_define_public(SceneProjectileComp);

ecs_view_define(GlobalView) { ecs_access_read(SceneTimeComp); }

ecs_view_define(ProjectileView) {
  ecs_access_read(SceneProjectileComp);
  ecs_access_write(SceneTransformComp);
}

ecs_system_define(SceneProjectileSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneTimeComp* time         = ecs_view_read_t(globalItr, SceneTimeComp);
  const f32            deltaSeconds = scene_delta_seconds(time);

  EcsView* projectileView = ecs_world_view_t(world, ProjectileView);
  for (EcsIterator* itr = ecs_view_itr(projectileView); ecs_view_walk(itr);) {
    const SceneProjectileComp* projectile = ecs_view_read_t(itr, SceneProjectileComp);
    SceneTransformComp*        trans      = ecs_view_write_t(itr, SceneTransformComp);

    const GeoVector dir      = geo_quat_rotate(trans->rotation, geo_forward);
    const GeoVector deltaPos = geo_vector_mul(dir, projectile->speed * deltaSeconds);
    trans->position          = geo_vector_add(trans->position, deltaPos);
  }
}

ecs_module_init(scene_projectile_module) {
  ecs_register_comp(SceneProjectileComp);

  ecs_register_view(GlobalView);
  ecs_register_view(ProjectileView);

  ecs_register_system(SceneProjectileSys, ecs_view_id(GlobalView), ecs_view_id(ProjectileView));
}
