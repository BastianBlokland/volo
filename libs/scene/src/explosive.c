#include "ecs_utils.h"
#include "ecs_world.h"
#include "scene_collision.h"
#include "scene_explosive.h"
#include "scene_health.h"
#include "scene_time.h"
#include "scene_transform.h"

ecs_comp_define_public(SceneExplosiveComp);

ecs_view_define(GlobalView) {
  ecs_access_read(SceneCollisionEnvComp);
  ecs_access_read(SceneTimeComp);
}

ecs_view_define(ExplosiveView) {
  ecs_access_read(SceneTransformComp);
  ecs_access_write(SceneExplosiveComp);
}

static void scene_explode(
    EcsWorld*                    world,
    const SceneCollisionEnvComp* colEnv,
    const SceneExplosiveComp*    explosive,
    const GeoVector              position) {

  const SceneQueryFilter filter = {
      .layerMask = SceneLayer_Unit | SceneLayer_Destructible,
  };

  // Find all targets in the damage radius.
  const GeoSphere damageSphere = {
      .point  = position,
      .radius = explosive->radius,
  };
  EcsEntityId hits[scene_query_max_hits];
  const u32   hitCount = scene_query_sphere_all(colEnv, &damageSphere, &filter, hits);

  // Damage all the found entities.
  for (u32 i = 0; i != hitCount; ++i) {
    if (ecs_world_exists(world, hits[i]) && ecs_world_has_t(world, hits[i], SceneHealthComp)) {
      scene_health_damage(world, hits[i], explosive->damage);
    }
  }
}

ecs_system_define(SceneExplosiveSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneCollisionEnvComp* colEnv = ecs_view_read_t(globalItr, SceneCollisionEnvComp);
  const SceneTimeComp*         time   = ecs_view_read_t(globalItr, SceneTimeComp);

  EcsView* explosiveView = ecs_world_view_t(world, ExplosiveView);
  for (EcsIterator* itr = ecs_view_itr(explosiveView); ecs_view_walk(itr);) {
    SceneExplosiveComp*       explosive = ecs_view_write_t(itr, SceneExplosiveComp);
    const SceneTransformComp* trans     = ecs_view_read_t(itr, SceneTransformComp);

    if (explosive->delay >= 0 && (explosive->delay -= time->delta) < 0) {
      scene_explode(world, colEnv, explosive, trans->position);
    }
  }
}

ecs_module_init(scene_explosive_module) {
  ecs_register_comp(SceneExplosiveComp);

  ecs_register_view(GlobalView);
  ecs_register_view(ExplosiveView);

  ecs_register_system(SceneExplosiveSys, ecs_view_id(GlobalView), ecs_view_id(ExplosiveView));
}
