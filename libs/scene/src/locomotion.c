#include "core_math.h"
#include "ecs_world.h"
#include "scene_locomotion.h"
#include "scene_time.h"
#include "scene_transform.h"

#define locomotion_arrive_threshold 1e-2f

ecs_comp_define_public(SceneLocomotionComp);

ecs_view_define(GlobalView) { ecs_access_read(SceneTimeComp); }

ecs_view_define(MoveView) {
  ecs_access_read(SceneLocomotionComp);
  ecs_access_write(SceneTransformComp);
}

static void scene_locomotion_move(
    const SceneLocomotionComp* loco, SceneTransformComp* trans, const f32 deltaSeconds) {
  const GeoVector toTarget = geo_vector_sub(loco->target, trans->position);
  const f32       dist     = geo_vector_mag(toTarget);
  if (dist < locomotion_arrive_threshold) {
    return;
  }
  const GeoVector dir   = geo_vector_div(toTarget, dist);
  const f32       delta = math_min(dist, loco->speed * deltaSeconds);

  trans->position = geo_vector_add(trans->position, geo_vector_mul(dir, delta));
}

ecs_system_define(SceneLocomotionMoveSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneTimeComp* time         = ecs_view_read_t(globalItr, SceneTimeComp);
  const f32            deltaSeconds = scene_delta_seconds(time);

  EcsView* moveView = ecs_world_view_t(world, MoveView);
  for (EcsIterator* itr = ecs_view_itr(moveView); ecs_view_walk(itr);) {
    const SceneLocomotionComp* loco  = ecs_view_read_t(itr, SceneLocomotionComp);
    SceneTransformComp*        trans = ecs_view_write_t(itr, SceneTransformComp);

    scene_locomotion_move(loco, trans, deltaSeconds);
  }
}

ecs_module_init(scene_locomotion_module) {
  ecs_register_comp(SceneLocomotionComp);

  ecs_register_view(GlobalView);
  ecs_register_view(MoveView);

  ecs_register_system(SceneLocomotionMoveSys, ecs_view_id(GlobalView), ecs_view_id(MoveView));
}
