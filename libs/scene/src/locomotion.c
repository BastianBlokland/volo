#include "core_diag.h"
#include "core_math.h"
#include "ecs_world.h"
#include "scene_locomotion.h"
#include "scene_nav.h"
#include "scene_register.h"
#include "scene_skeleton.h"
#include "scene_terrain.h"
#include "scene_time.h"
#include "scene_transform.h"

#define locomotion_arrive_threshold 0.1f

ecs_comp_define_public(SceneLocomotionComp);

ecs_view_define(GlobalView) {
  ecs_access_maybe_read(SceneTerrainComp);
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

  bool clamped = false;
  if (yAngleDelta < -delta) {
    yAngleDelta = -delta;
    clamped     = true;
  } else if (yAngleDelta > delta) {
    yAngleDelta = delta;
    clamped     = true;
  }

  trans->rotation = geo_quat_mul(geo_quat_angle_axis(geo_up, yAngleDelta), trans->rotation);
  trans->rotation = geo_quat_norm(trans->rotation);
  return !clamped;
}

/**
 * NOTE: Returns true if we moved the entity, otherwise false.
 */
static bool scene_loco_move(
    SceneLocomotionComp* loco, SceneTransformComp* trans, const f32 scale, const f32 delta) {
  if (!(loco->flags & SceneLocomotion_Moving)) {
    return false;
  }
  const GeoVector toTarget = geo_vector_sub(loco->targetPos, trans->position);
  const f32       dist     = geo_vector_mag(toTarget);
  if (dist < locomotion_arrive_threshold) {
    loco->flags &= ~SceneLocomotion_Moving;
    return false;
  }
  const f32 distDelta = math_min(dist, loco->maxSpeed * loco->speedNorm * scale * delta);

  loco->targetDir = geo_vector_div(toTarget, dist);
  trans->position = geo_vector_add(trans->position, geo_vector_mul(loco->targetDir, distDelta));
  return true;
}

/**
 * Separate this entity from blockers and (other) navigation agents.
 * NOTE: Returns true if we moved the entity, otherwise false.
 */
static bool scene_loco_separate(
    const SceneNavEnvComp* navEnv,
    const EcsEntityId      entity,
    SceneLocomotionComp*   loco,
    SceneTransformComp*    trans,
    const f32              scale) {
  const bool      moving = (loco->flags & SceneLocomotion_Moving) != 0;
  const GeoVector pos    = trans->position;

  loco->lastSeparation = scene_nav_separate(navEnv, entity, pos, loco->radius * scale, moving);
  trans->position      = geo_vector_add(trans->position, loco->lastSeparation);

  return geo_vector_mag_sqr(loco->lastSeparation) > f32_epsilon;
}

ecs_system_define(SceneLocomotionMoveSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneNavEnvComp*  navEnv       = ecs_view_read_t(globalItr, SceneNavEnvComp);
  const SceneTerrainComp* terrain      = ecs_view_read_t(globalItr, SceneTerrainComp);
  const SceneTimeComp*    time         = ecs_view_read_t(globalItr, SceneTimeComp);
  const f32               deltaSeconds = scene_delta_seconds(time);

  EcsView* moveView = ecs_world_view_t(world, MoveView);
  for (EcsIterator* itr = ecs_view_itr_step(moveView, parCount, parIndex); ecs_view_walk(itr);) {
    const EcsEntityId    entity = ecs_view_entity(itr);
    SceneAnimationComp*  anim   = ecs_view_write_t(itr, SceneAnimationComp);
    SceneLocomotionComp* loco   = ecs_view_write_t(itr, SceneLocomotionComp);
    SceneTransformComp*  trans  = ecs_view_write_t(itr, SceneTransformComp);

    const SceneScaleComp* scaleComp     = ecs_view_read_t(itr, SceneScaleComp);
    const f32             scale         = scaleComp ? scaleComp->scale : 1.0f;
    bool                  positionDirty = false;

    if (loco->flags & SceneLocomotion_Stop) {
      loco->targetPos = trans->position;
      loco->flags &= ~(SceneLocomotion_Moving | SceneLocomotion_Stop);
      positionDirty = true;
    }

    positionDirty |= scene_loco_move(loco, trans, scale, deltaSeconds);

    if (geo_vector_mag_sqr(loco->targetDir) > f32_epsilon) {
      if (scene_loco_face(trans, loco->targetDir, loco->rotationSpeedRad * deltaSeconds)) {
        loco->targetDir = geo_vector(0);
      }
    }

    if (deltaSeconds > 0) {
      /**
       * Move this entity out of other navigation agents and blockers.
       * This is not an 'over time' effect as it moves far enough to fully separate, however with
       * groups of navigation agents it can take multiple frames to settle.
       *
       * TODO: This means that it ends up being quite frame-rate dependent (and doesn't respect
       * time-scale). Consider changing this to use forces and apply the separation over-time, this
       * will mean that we have to accept units temporary overlapping each other.
       */
      positionDirty |= scene_loco_separate(navEnv, entity, loco, trans, scale);
    }

    if (terrain && positionDirty) {
      trans->position.y = scene_terrain_height(terrain, trans->position);
    }

    if (anim && loco->speedNorm < f32_epsilon) {
      scene_animation_set_time(anim, loco->moveAnimation, 0);
    }

    const f32 targetSpeedNorm = (loco->flags & SceneLocomotion_Moving) ? 1.0f : 0.0f;
    math_towards_f32(&loco->speedNorm, targetSpeedNorm, loco->accelerationNorm * deltaSeconds);

    if (anim) {
      scene_animation_set_weight(anim, loco->moveAnimation, loco->speedNorm);
    }
  }
}

ecs_module_init(scene_locomotion_module) {
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
