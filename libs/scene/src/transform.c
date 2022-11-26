#include "core_float.h"
#include "ecs_world.h"
#include "scene_register.h"
#include "scene_time.h"
#include "scene_transform.h"

ecs_comp_define_public(SceneTransformComp);
ecs_comp_define_public(SceneScaleComp);
ecs_comp_define_public(SceneVelocityComp);

ecs_view_define(GlobalView) { ecs_access_read(SceneTimeComp); }

ecs_view_define(VelocityUpdateView) {
  ecs_access_read(SceneTransformComp);
  ecs_access_write(SceneVelocityComp);
}

ecs_system_define(SceneVelocityUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneTimeComp* time         = ecs_view_read_t(globalItr, SceneTimeComp);
  const f32            deltaSeconds = scene_delta_seconds(time);

  if (deltaSeconds <= f32_epsilon) {
    return;
  }

  EcsView* updateView = ecs_world_view_t(world, VelocityUpdateView);
  for (EcsIterator* itr = ecs_view_itr(updateView); ecs_view_walk(itr);) {
    SceneVelocityComp* velo = ecs_view_write_t(itr, SceneVelocityComp);

    const GeoVector pos      = ecs_view_read_t(itr, SceneTransformComp)->position;
    const GeoVector posDelta = geo_vector_sub(pos, velo->lastPosition);

    velo->velocity     = geo_vector_div(posDelta, deltaSeconds);
    velo->lastPosition = pos;
  }
}

ecs_module_init(scene_transform_module) {
  ecs_register_comp(SceneTransformComp);
  ecs_register_comp(SceneScaleComp);
  ecs_register_comp(SceneVelocityComp);

  ecs_register_view(GlobalView);
  ecs_register_view(VelocityUpdateView);

  ecs_register_system(
      SceneVelocityUpdateSys, ecs_view_id(GlobalView), ecs_view_id(VelocityUpdateView));

  ecs_order(SceneVelocityUpdateSys, SceneOrder_VelocityUpdate);
}

GeoMatrix scene_transform_matrix(const SceneTransformComp* trans) {
  const GeoMatrix pos = geo_matrix_translate(trans->position);
  const GeoMatrix rot = geo_matrix_from_quat(trans->rotation);
  return geo_matrix_mul(&pos, &rot);
}

GeoMatrix scene_transform_matrix_inv(const SceneTransformComp* trans) {
  const GeoMatrix rot = geo_matrix_from_quat(geo_quat_inverse(trans->rotation));
  const GeoMatrix pos = geo_matrix_translate(geo_vector_mul(trans->position, -1));
  return geo_matrix_mul(&rot, &pos);
}

GeoMatrix scene_matrix_world(const SceneTransformComp* trans, const SceneScaleComp* scale) {
  const GeoVector pos      = LIKELY(trans) ? trans->position : geo_vector(0);
  const GeoQuat   rot      = LIKELY(trans) ? trans->rotation : geo_quat_ident;
  const f32       scaleMag = scale ? scale->scale : 1.0f;
  return geo_matrix_trs(pos, rot, geo_vector(scaleMag, scaleMag, scaleMag));
}

GeoVector scene_position_predict(
    const SceneTransformComp* trans, const SceneVelocityComp* velo, const TimeDuration time) {
  const GeoVector delta = geo_vector_mul(velo->velocity, time / (f32)time_second);
  return geo_vector_add(trans->position, delta);
}
