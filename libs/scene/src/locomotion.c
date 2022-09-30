#include "core_math.h"
#include "ecs_world.h"
#include "scene_locomotion.h"
#include "scene_nav.h"
#include "scene_register.h"
#include "scene_skeleton.h"
#include "scene_time.h"
#include "scene_transform.h"

#define locomotion_arrive_threshold 0.1f
#define locomotion_rotation_speed 360.0f

ecs_comp_define_public(SceneLocomotionComp);

ecs_view_define(GlobalView) {
  ecs_access_read(SceneNavEnvComp);
  ecs_access_read(SceneTimeComp);
}

ecs_view_define(MoveView) {
  ecs_access_maybe_read(SceneScaleComp);
  ecs_access_maybe_write(SceneAnimationComp);
  ecs_access_write(SceneLocomotionComp);
  ecs_access_write(SceneTransformComp);
}

static f32 scene_loco_y_angle_diff(const GeoVector fromDir, const GeoVector toDir) {
  const GeoVector tangent      = geo_vector_cross3(geo_up, fromDir);
  const f32       dotTangentTo = geo_vector_dot(tangent, toDir);
  const f32       dotFromTo    = geo_vector_dot(fromDir, toDir);
  return math_acos_f32(math_clamp_f32(dotFromTo, -1.0f, 1.0f)) * math_sign(dotTangentTo);
}

static void scene_loco_face(SceneTransformComp* trans, const GeoVector dir, const f32 delta) {
  const GeoVector forward    = geo_quat_rotate(trans->rotation, geo_forward);
  const f32       yAngleDiff = scene_loco_y_angle_diff(forward, dir);

  const f32 maxAngleDelta = locomotion_rotation_speed * math_deg_to_rad * delta;
  const f32 yAngleDelta   = math_clamp_f32(yAngleDiff, -maxAngleDelta, maxAngleDelta);

  trans->rotation = geo_quat_mul(geo_quat_angle_axis(geo_up, yAngleDelta), trans->rotation);
  trans->rotation = geo_quat_norm(trans->rotation);
}

static void scene_loco_move(
    SceneLocomotionComp* loco, SceneTransformComp* trans, const f32 scale, const f32 delta) {
  if (!(loco->flags & SceneLocomotion_Moving)) {
    return;
  }
  const GeoVector toTarget = geo_vector_sub(loco->target, trans->position);
  const f32       dist     = geo_vector_mag(toTarget);
  if (dist < (loco->radius + locomotion_arrive_threshold)) {
    loco->flags &= ~SceneLocomotion_Moving;
    return;
  }
  const GeoVector dir       = geo_vector_div(toTarget, dist);
  const f32       distDelta = math_min(dist, loco->speed * scale * delta);

  trans->position = geo_vector_add(trans->position, geo_vector_mul(dir, distDelta));
  scene_loco_face(trans, dir, delta);
}

/**
 * Separate this entity from blockers and (other) navigation agents.
 */
static void scene_loco_separate(
    const SceneNavEnvComp* navEnv,
    const EcsEntityId      entity,
    SceneLocomotionComp*   loco,
    SceneTransformComp*    trans) {
  const bool      moving = (loco->flags & SceneLocomotion_Moving) != 0;
  const GeoVector pos    = trans->position;
  loco->lastSeparation   = scene_nav_separate(navEnv, entity, pos, loco->radius, moving);
  trans->position        = geo_vector_add(trans->position, loco->lastSeparation);
}

ecs_system_define(SceneLocomotionMoveSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneNavEnvComp* navEnv       = ecs_view_read_t(globalItr, SceneNavEnvComp);
  const SceneTimeComp*   time         = ecs_view_read_t(globalItr, SceneTimeComp);
  const f32              deltaSeconds = scene_delta_seconds(time);

  const StringHash runAnimHash = string_hash_lit("run");

  EcsView* moveView = ecs_world_view_t(world, MoveView);
  for (EcsIterator* itr = ecs_view_itr(moveView); ecs_view_walk(itr);) {
    const EcsEntityId    entity = ecs_view_entity(itr);
    SceneAnimationComp*  anim   = ecs_view_write_t(itr, SceneAnimationComp);
    SceneLocomotionComp* loco   = ecs_view_write_t(itr, SceneLocomotionComp);
    SceneTransformComp*  trans  = ecs_view_write_t(itr, SceneTransformComp);

    const SceneScaleComp* scaleComp = ecs_view_read_t(itr, SceneScaleComp);
    const f32             scale     = scaleComp ? scaleComp->scale : 1.0f;

    scene_loco_move(loco, trans, scale, deltaSeconds);
    scene_loco_separate(navEnv, entity, loco, trans);

    if (anim) {
      const f32 targetRunWeight = (loco->flags & SceneLocomotion_Moving) ? 1.0f : 0.0f;
      loco->runWeight           = math_lerp(loco->runWeight, targetRunWeight, 10.0f * deltaSeconds);
      scene_animation_set_weight(anim, runAnimHash, loco->runWeight);
    }
  }
}

ecs_module_init(scene_locomotion_module) {
  ecs_register_comp(SceneLocomotionComp);

  ecs_register_view(GlobalView);
  ecs_register_view(MoveView);

  ecs_register_system(SceneLocomotionMoveSys, ecs_view_id(GlobalView), ecs_view_id(MoveView));

  ecs_order(SceneLocomotionMoveSys, SceneOrder_LocomotionUpdate);
}

void scene_locomotion_move_to(SceneLocomotionComp* comp, const GeoVector target) {
  comp->flags |= SceneLocomotion_Moving;
  comp->target = target;
}
