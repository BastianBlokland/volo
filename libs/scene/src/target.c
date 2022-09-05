#include "core_float.h"
#include "ecs_world.h"
#include "scene_health.h"
#include "scene_target.h"
#include "scene_transform.h"

ecs_comp_define_public(SceneTargetFinderComp);

ecs_view_define(TargetFinderView) {
  ecs_access_read(SceneTransformComp);
  ecs_access_write(SceneTargetFinderComp);
}

ecs_view_define(TargetView) {
  ecs_access_read(SceneTransformComp);
  ecs_access_with(SceneHealthComp);
}

ecs_system_define(SceneTargetUpdateSys) {
  /**
   * Find the closest target for each target-finder.
   */
  EcsView* finderView = ecs_world_view_t(world, TargetFinderView);
  EcsView* targetView = ecs_world_view_t(world, TargetView);

  EcsIterator* targetItr = ecs_view_itr(targetView);
  for (EcsIterator* finderItr = ecs_view_itr(finderView); ecs_view_walk(finderItr);) {
    const EcsEntityId         entity = ecs_view_entity(finderItr);
    const SceneTransformComp* trans  = ecs_view_read_t(finderItr, SceneTransformComp);
    SceneTargetFinderComp*    finder = ecs_view_write_t(finderItr, SceneTargetFinderComp);

    finder->targetDistSqr = f32_max;
    finder->target        = 0;
    for (ecs_view_itr_reset(targetItr); ecs_view_walk(targetItr);) {
      const EcsEntityId targetEntity = ecs_view_entity(targetItr);
      if (entity == targetEntity) {
        continue; // Do not target ourselves.
      }
      const SceneTransformComp* targetTrans = ecs_view_read_t(targetItr, SceneTransformComp);
      const GeoVector           posDelta = geo_vector_sub(targetTrans->position, trans->position);
      const f32                 distSqr  = geo_vector_mag_sqr(posDelta);
      if (distSqr < finder->targetDistSqr) {
        finder->target        = targetEntity;
        finder->targetDistSqr = distSqr;
      }
    }
  }
}

ecs_module_init(scene_target_module) {
  ecs_register_comp(SceneTargetFinderComp);

  ecs_register_view(TargetFinderView);
  ecs_register_view(TargetView);

  ecs_register_system(SceneTargetUpdateSys, ecs_view_id(TargetFinderView), ecs_view_id(TargetView));
}
