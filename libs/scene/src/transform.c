#include "core_diag.h"
#include "core_float.h"
#include "ecs_world.h"
#include "scene_register.h"
#include "scene_time.h"
#include "scene_transform.h"

#define velocity_update_max_time_step (1.0f / 10)
#define velocity_update_max_dist 5.0f

ecs_comp_define_public(SceneTransformComp);
ecs_comp_define_public(SceneScaleComp);
ecs_comp_define_public(SceneVelocityComp);

static f32 trans_time_to_seconds(const TimeDuration dur) {
  static const f64 g_toSecMul = 1.0 / (f64)time_second;
  // NOTE: Potentially can be done in 32 bit but with nano-seconds its at the edge of f32 precision.
  return (f32)((f64)dur * g_toSecMul);
}

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
    return; // Game is paused, we cannot update the velocity.
  }
  if (deltaSeconds > velocity_update_max_time_step) {
    return; // Skip very large update steps (frame spikes).
  }

  static const f32 g_avgWindow = 1.0f / 2.5f;

  EcsView* updateView = ecs_world_view_t(world, VelocityUpdateView);
  for (EcsIterator* itr = ecs_view_itr_step(updateView, parCount, parIndex); ecs_view_walk(itr);) {
    SceneVelocityComp* veloComp = ecs_view_write_t(itr, SceneVelocityComp);

    const GeoVector pos      = ecs_view_read_t(itr, SceneTransformComp)->position;
    const GeoVector posDelta = geo_vector_sub(pos, veloComp->lastPosition);

    veloComp->lastPosition = pos;

    if (geo_vector_mag_sqr(posDelta) > (velocity_update_max_dist * velocity_update_max_dist)) {
      continue; // Entity moved too far this frame (teleported?).
    }

    const GeoVector newVelo      = geo_vector_div(posDelta, deltaSeconds);
    const GeoVector oldVeloAvg   = veloComp->velocityAvg;
    const GeoVector veloAvgDelta = geo_vector_mul(geo_vector_sub(newVelo, oldVeloAvg), g_avgWindow);
    veloComp->velocityAvg        = geo_vector_add(oldVeloAvg, veloAvgDelta);
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

  ecs_parallel(SceneVelocityUpdateSys, 2);
  ecs_order(SceneVelocityUpdateSys, SceneOrder_VelocityUpdate);
}

GeoVector scene_transform_to_world(
    const SceneTransformComp* trans, const SceneScaleComp* scale, const GeoVector pos) {
  const GeoVector scaled = scale ? geo_vector_mul(pos, scale->scale) : pos;
  return geo_vector_add(trans->position, geo_quat_rotate(trans->rotation, scaled));
}

void scene_transform_rotate_around(
    SceneTransformComp* trans, const GeoVector pivot, const GeoQuat rot) {
  const GeoVector delta        = geo_vector_sub(trans->position, pivot);
  const GeoVector deltaRotated = geo_quat_rotate(rot, delta);
  trans->position              = geo_vector_add(pivot, deltaRotated);
  trans->rotation              = geo_quat_norm(geo_quat_mul(rot, trans->rotation));
}

void scene_transform_scale_around(
    SceneTransformComp* trans, SceneScaleComp* scale, const GeoVector pivot, const f32 scaleDelta) {
  diag_assert(scaleDelta > f32_epsilon);

  const GeoVector delta       = geo_vector_sub(trans->position, pivot);
  const GeoVector deltaScaled = geo_vector_mul(delta, scaleDelta);
  trans->position             = geo_vector_add(pivot, deltaScaled);
  scale->scale *= scaleDelta;
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
    const SceneTransformComp* trans,
    const SceneVelocityComp*  velo,
    const TimeDuration        timeInFuture) {
  if (!velo) {
    return trans->position;
  }
  const GeoVector delta = geo_vector_mul(velo->velocityAvg, trans_time_to_seconds(timeInFuture));
  return geo_vector_add(trans->position, delta);
}
