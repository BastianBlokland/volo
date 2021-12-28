#include "ecs_world.h"
#include "scene_time.h"
#include "scene_transform.h"
#include "scene_velocity.h"

ecs_comp_define_public(SceneVelocityComp);

ecs_view_define(GlobalView) { ecs_access_read(SceneTimeComp); }

ecs_view_define(VelocityApplyView) {
  ecs_access_read(SceneVelocityComp);
  ecs_access_write(SceneTransformComp);
}

ecs_system_define(SceneVelocityApplySys) {
  EcsView*     view      = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr = ecs_view_maybe_at(view, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneTimeComp* time = ecs_view_read_t(globalItr, SceneTimeComp);

  EcsView* applyView = ecs_world_view_t(world, VelocityApplyView);
  for (EcsIterator* itr = ecs_view_itr(applyView); ecs_view_walk(itr);) {
    const SceneVelocityComp* velocity = ecs_view_read_t(itr, SceneVelocityComp);
    SceneTransformComp*      trans    = ecs_view_write_t(itr, SceneTransformComp);

    const f32       deltaSeconds = time->delta / (f32)time_second;
    const GeoVector posDelta     = geo_vector_mul(velocity->velocity, deltaSeconds);
    trans->position              = geo_vector_add(trans->position, posDelta);

    const GeoVector rotDelta = geo_vector_mul(velocity->angularVelocity, deltaSeconds);
    trans->rotation = geo_quat_mul(trans->rotation, geo_quat_angle_axis(geo_forward, rotDelta.z));
    trans->rotation = geo_quat_mul(trans->rotation, geo_quat_angle_axis(geo_right, rotDelta.x));
    trans->rotation = geo_quat_mul(trans->rotation, geo_quat_angle_axis(geo_up, rotDelta.y));
    trans->rotation = geo_quat_norm(trans->rotation);
  }
}

ecs_module_init(scene_velocity_module) {
  ecs_register_comp(SceneVelocityComp);

  ecs_register_view(GlobalView);
  ecs_register_view(VelocityApplyView);

  ecs_register_system(
      SceneVelocityApplySys, ecs_view_id(GlobalView), ecs_view_id(VelocityApplyView));
}
