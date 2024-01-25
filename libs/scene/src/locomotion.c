#include "core_diag.h"
#include "core_float.h"
#include "core_math.h"
#include "ecs_world.h"
#include "scene_locomotion.h"
#include "scene_nav.h"
#include "scene_register.h"
#include "scene_skeleton.h"
#include "scene_terrain.h"
#include "scene_time.h"
#include "scene_transform.h"

#define loco_arrive_threshold 0.1f
#define loco_anim_weight_ease_speed 2.5f

ecs_comp_define_public(SceneLocomotionComp);

ecs_view_define(GlobalView) {
  ecs_access_read(SceneTerrainComp);
  ecs_access_read(SceneNavEnvComp);
  ecs_access_read(SceneTimeComp);
}

ecs_view_define(MoveView) {
  ecs_access_maybe_read(SceneScaleComp);
  ecs_access_maybe_write(SceneAnimationComp);
  ecs_access_write(SceneLocomotionComp);
  ecs_access_write(SceneTransformComp);
}

static bool scene_loco_face(SceneTransformComp* trans, const GeoVector dir, const f32 delta) {
  const GeoQuat rotTarget = geo_quat_look(dir, geo_up);
  return geo_quat_towards(&trans->rotation, rotTarget, delta);
}

static GeoVector scene_loco_move(
    SceneLocomotionComp* loco, const SceneTransformComp* trans, const f32 scale, const f32 delta) {
  const GeoVector toTarget  = geo_vector_xz(geo_vector_sub(loco->targetPos, trans->position));
  const f32       dist      = geo_vector_mag(toTarget);
  const f32       distDelta = math_min(dist, loco->maxSpeed * scale * delta);
  loco->targetDir           = geo_vector_div(toTarget, dist);
  return geo_vector_mul(loco->targetDir, distDelta);
}

ecs_system_define(SceneLocomotionMoveSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneNavEnvComp*  navEnv  = ecs_view_read_t(globalItr, SceneNavEnvComp);
  const SceneTerrainComp* terrain = ecs_view_read_t(globalItr, SceneTerrainComp);
  const SceneTimeComp*    time    = ecs_view_read_t(globalItr, SceneTimeComp);
  const f32               dt      = scene_delta_seconds(time);

  EcsView* moveView = ecs_world_view_t(world, MoveView);
  for (EcsIterator* itr = ecs_view_itr_step(moveView, parCount, parIndex); ecs_view_walk(itr);) {
    const EcsEntityId     entity    = ecs_view_entity(itr);
    SceneAnimationComp*   anim      = ecs_view_write_t(itr, SceneAnimationComp);
    SceneLocomotionComp*  loco      = ecs_view_write_t(itr, SceneLocomotionComp);
    SceneTransformComp*   trans     = ecs_view_write_t(itr, SceneTransformComp);
    const SceneScaleComp* scaleComp = ecs_view_read_t(itr, SceneScaleComp);
    const f32             scale     = scaleComp ? scaleComp->scale : 1.0f;
    const GeoVector       pos       = trans->position;

    if (loco->flags & SceneLocomotion_Stop) {
      loco->targetPos = pos;
      loco->flags &= ~(SceneLocomotion_Moving | SceneLocomotion_Stop);
    }

    GeoVector posDelta = {0};
    if (loco->flags & SceneLocomotion_Moving) {
      const GeoVector toTarget = geo_vector_xz(geo_vector_sub(loco->targetPos, pos));
      const f32       distSqr  = geo_vector_mag_sqr(toTarget);
      if (distSqr <= (loco_arrive_threshold * loco_arrive_threshold)) {
        loco->flags &= ~SceneLocomotion_Moving;
      } else {
        posDelta = scene_loco_move(loco, trans, scale, dt);
      }
    }

    if (dt > 0) {
      /**
       * Push this entity away from other navigation agents and blockers.
       * NOTE: Use current position instead of the next position to avoid two units moving in the
       * same direction not pushing each other.
       */
      const bool      moving    = (loco->flags & SceneLocomotion_Moving) != 0;
      const f32       sepRadius = loco->radius * scale;
      const GeoVector force     = scene_nav_separate(navEnv, entity, pos, sepRadius, moving);
      posDelta                  = geo_vector_add(posDelta, geo_vector_mul(force, dt));
      loco->lastSeparation      = force;
    }

    const f32 posDeltaMag = geo_vector_mag(posDelta);
    if (posDeltaMag > 1e-4f || scene_terrain_updated(terrain)) {
      trans->position = geo_vector_add(trans->position, posDelta);
      scene_terrain_snap(terrain, &trans->position);
    }

    if (geo_vector_mag_sqr(loco->targetDir) > f32_epsilon) {
      if (scene_loco_face(trans, loco->targetDir, loco->rotationSpeedRad * dt)) {
        loco->targetDir = geo_vector(0);
      }
    }

    SceneAnimLayer* layerMove = anim ? scene_animation_layer_mut(anim, loco->moveAnimation) : null;
    if (layerMove) {
      if (layerMove->weight < f32_epsilon) {
        scene_animation_set_time(anim, loco->moveAnimation, 0);
      }
      const f32 maxSpeedThisTick = loco->maxSpeed * scale * dt;
      const f32 speedNorm        = math_min(posDeltaMag / maxSpeedThisTick, 1);
      math_towards_f32(&layerMove->weight, speedNorm, loco_anim_weight_ease_speed * dt);
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
