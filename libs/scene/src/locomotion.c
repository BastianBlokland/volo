#include "core_diag.h"
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
#define locomotion_accelerate_time 4.0f

static StringHash g_locoRunAnimHash;

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

static bool scene_loco_face(SceneTransformComp* trans, const GeoVector dir, const f32 delta) {
  const GeoVector forward     = geo_quat_rotate(trans->rotation, geo_forward);
  f32             yAngleDelta = scene_loco_y_angle_diff(forward, dir);

  bool      clamped       = false;
  const f32 maxAngleDelta = locomotion_rotation_speed * math_deg_to_rad * delta;
  if (yAngleDelta < -maxAngleDelta) {
    yAngleDelta = -maxAngleDelta;
    clamped     = true;
  } else if (yAngleDelta > maxAngleDelta) {
    yAngleDelta = maxAngleDelta;
    clamped     = true;
  }

  trans->rotation = geo_quat_mul(geo_quat_angle_axis(geo_up, yAngleDelta), trans->rotation);
  trans->rotation = geo_quat_norm(trans->rotation);
  return !clamped;
}

static void scene_loco_move(
    SceneLocomotionComp* loco, SceneTransformComp* trans, const f32 scale, const f32 delta) {
  if (!(loco->flags & SceneLocomotion_Moving)) {
    return;
  }
  const GeoVector toTarget = geo_vector_sub(loco->targetPos, trans->position);
  const f32       dist     = geo_vector_mag(toTarget);
  if (dist < (loco->radius + locomotion_arrive_threshold)) {
    loco->flags &= ~SceneLocomotion_Moving;
    return;
  }
  const f32 distDelta = math_min(dist, loco->maxSpeed * loco->speedNorm * scale * delta);

  loco->targetDir = geo_vector_div(toTarget, dist);
  trans->position = geo_vector_add(trans->position, geo_vector_mul(loco->targetDir, distDelta));
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

  EcsView* moveView = ecs_world_view_t(world, MoveView);
  for (EcsIterator* itr = ecs_view_itr_step(moveView, parCount, parIndex); ecs_view_walk(itr);) {
    const EcsEntityId    entity = ecs_view_entity(itr);
    SceneAnimationComp*  anim   = ecs_view_write_t(itr, SceneAnimationComp);
    SceneLocomotionComp* loco   = ecs_view_write_t(itr, SceneLocomotionComp);
    SceneTransformComp*  trans  = ecs_view_write_t(itr, SceneTransformComp);

    const SceneScaleComp* scaleComp = ecs_view_read_t(itr, SceneScaleComp);
    const f32             scale     = scaleComp ? scaleComp->scale : 1.0f;

    if (loco->flags & SceneLocomotion_Stop) {
      loco->targetPos = trans->position;
      loco->flags &= ~(SceneLocomotion_Moving | SceneLocomotion_Stop);
    }

    scene_loco_move(loco, trans, scale, deltaSeconds);
    if (geo_vector_mag_sqr(loco->targetDir) > f32_epsilon) {
      if (scene_loco_face(trans, loco->targetDir, deltaSeconds)) {
        loco->targetDir = geo_vector(0);
      }
    }
    scene_loco_separate(navEnv, entity, loco, trans);

    if (anim && loco->speedNorm < f32_epsilon) {
      scene_animation_set_time(anim, g_locoRunAnimHash, 0);
    }

    const f32 targetSpeedNorm = (loco->flags & SceneLocomotion_Moving) ? 1.0f : 0.0f;
    math_towards_f32(&loco->speedNorm, targetSpeedNorm, locomotion_accelerate_time * deltaSeconds);

    if (anim) {
      scene_animation_set_weight(anim, g_locoRunAnimHash, loco->speedNorm);
    }
  }
}

ecs_module_init(scene_locomotion_module) {
  g_locoRunAnimHash = string_hash_lit("run");

  ecs_register_comp(SceneLocomotionComp);

  ecs_register_view(GlobalView);
  ecs_register_view(MoveView);

  ecs_register_system(SceneLocomotionMoveSys, ecs_view_id(GlobalView), ecs_view_id(MoveView));

  ecs_order(SceneLocomotionMoveSys, SceneOrder_LocomotionUpdate);

  ecs_parallel(SceneLocomotionMoveSys, 4);
}

void scene_locomotion_move(SceneLocomotionComp* comp, const GeoVector target) {
  comp->flags |= SceneLocomotion_Moving;
  comp->targetPos = target;
}

void scene_locomotion_face(SceneLocomotionComp* comp, const GeoVector direction) {
  diag_assert_msg(
      math_abs(geo_vector_mag_sqr(direction) - 1.0f) <= 1e-6f,
      "Direction ({}) is not normalized",
      geo_vector_fmt(direction));

  comp->targetDir = direction;
}

void scene_locomotion_stop(SceneLocomotionComp* comp) { comp->flags |= SceneLocomotion_Stop; }
