#include "core_math.h"
#include "ecs_world.h"
#include "scene_locomotion.h"
#include "scene_skeleton.h"
#include "scene_time.h"
#include "scene_transform.h"

#define locomotion_arrive_threshold 1e-2f
#define locomotion_rotation_speed 360.0f

ecs_comp_define_public(SceneLocomotionComp);

ecs_view_define(GlobalView) { ecs_access_read(SceneTimeComp); }

ecs_view_define(MoveView) {
  ecs_access_maybe_write(SceneAnimationComp);
  ecs_access_write(SceneLocomotionComp);
  ecs_access_write(SceneTransformComp);
}

static f32 scene_locomotion_y_angle_diff(const GeoVector fromDir, const GeoVector toDir) {
  const GeoVector tangent      = geo_vector_cross3(geo_up, fromDir);
  const f32       dotTangentTo = geo_vector_dot(tangent, toDir);
  const f32       dotFromTo    = geo_vector_dot(fromDir, toDir);
  return math_acos_f32(math_clamp_f32(dotFromTo, -1.0f, 1.0f)) * math_sign(dotTangentTo);
}

static void
scene_locomotion_face(SceneTransformComp* trans, const GeoVector dir, const f32 deltaSeconds) {
  const GeoVector forward    = geo_quat_rotate(trans->rotation, geo_forward);
  const f32       yAngleDiff = scene_locomotion_y_angle_diff(forward, dir);

  const f32 maxAngleDelta = locomotion_rotation_speed * math_deg_to_rad * deltaSeconds;
  const f32 yAngleDelta   = math_clamp_f32(yAngleDiff, -maxAngleDelta, maxAngleDelta);

  trans->rotation = geo_quat_mul(geo_quat_angle_axis(geo_up, yAngleDelta), trans->rotation);
  trans->rotation = geo_quat_norm(trans->rotation);
}

static bool scene_locomotion_move(
    const SceneLocomotionComp* loco, SceneTransformComp* trans, const f32 deltaSeconds) {
  const GeoVector toTarget = geo_vector_sub(loco->target, trans->position);
  const f32       dist     = geo_vector_mag(toTarget);
  if (dist < locomotion_arrive_threshold) {
    return true;
  }
  const GeoVector dir   = geo_vector_div(toTarget, dist);
  const f32       delta = math_min(dist, loco->speed * deltaSeconds);

  trans->position = geo_vector_add(trans->position, geo_vector_mul(dir, delta));
  scene_locomotion_face(trans, dir, deltaSeconds);
  return false;
}

ecs_system_define(SceneLocomotionMoveSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneTimeComp* time         = ecs_view_read_t(globalItr, SceneTimeComp);
  const f32            deltaSeconds = scene_delta_seconds(time);

  const StringHash walkAnimHash = string_hash_lit("walking");

  EcsView* moveView = ecs_world_view_t(world, MoveView);
  for (EcsIterator* itr = ecs_view_itr(moveView); ecs_view_walk(itr);) {
    SceneAnimationComp*  anim  = ecs_view_write_t(itr, SceneAnimationComp);
    SceneLocomotionComp* loco  = ecs_view_write_t(itr, SceneLocomotionComp);
    SceneTransformComp*  trans = ecs_view_write_t(itr, SceneTransformComp);

    const bool arrived = scene_locomotion_move(loco, trans, deltaSeconds);
    if (anim) {
      const f32 targetWalkWeight = arrived ? 0.0f : 1.0f;
      loco->walkWeight = math_lerp(loco->walkWeight, targetWalkWeight, 10.0f * deltaSeconds);
      scene_animation_set_weight(anim, walkAnimHash, loco->walkWeight);
    }
  }
}

ecs_module_init(scene_locomotion_module) {
  ecs_register_comp(SceneLocomotionComp);

  ecs_register_view(GlobalView);
  ecs_register_view(MoveView);

  ecs_register_system(SceneLocomotionMoveSys, ecs_view_id(GlobalView), ecs_view_id(MoveView));
}
