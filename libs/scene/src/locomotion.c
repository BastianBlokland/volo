#include "core_diag.h"
#include "core_float.h"
#include "core_math.h"
#include "core_noise.h"
#include "core_rng.h"
#include "ecs_world.h"
#include "scene_locomotion.h"
#include "scene_nav.h"
#include "scene_register.h"
#include "scene_skeleton.h"
#include "scene_status.h"
#include "scene_terrain.h"
#include "scene_time.h"
#include "scene_transform.h"

#define loco_arrive_threshold 0.1f
#define loco_rot_turbulence_freq 5.0f
#define loco_anim_speed_threshold 0.2f
#define loco_anim_speed_ease 2.0f
#define loco_anim_weight_ease 3.0f
#define loco_move_weight_multiplier 4.0f
#define loco_face_threshold 0.8f
#define loco_wheeled_deceleration 15.0f

ecs_comp_define_public(SceneLocomotionComp);
ecs_comp_define_public(SceneLocomotionWheeledComp);

static void loco_validate_pos(MAYBE_UNUSED const GeoVector vec) {
  diag_assert_msg(
      geo_vector_mag_sqr(vec) <= (1e5f * 1e5f),
      "Position ({}) is out of bounds",
      geo_vector_fmt(vec));
}

static bool loco_is_facing(const SceneLocomotionComp* loco, const SceneTransformComp* trans) {
  const GeoVector curDir     = geo_quat_rotate(trans->rotation, geo_forward);
  const GeoVector curDirFlat = geo_vector_norm(geo_vector_xz(curDir));
  const f32       dirDot     = geo_vector_dot(curDirFlat, loco->targetDir);
  return dirDot >= loco_face_threshold;
}

static f32 loco_rot_speed(const SceneLocomotionComp* loco, const EcsEntityId e, const f32 time) {
  f32 speed = loco->rotationSpeedRad;
  if (loco_rot_turbulence_freq > 0.0) {
    const f32 seed       = ((f32)ecs_entity_id_index(e) * 0.1337f);
    const f32 turbulence = 1.0f + noise_perlin3(time * loco_rot_turbulence_freq, seed, 0);
    speed *= turbulence;
  }
  return speed;
}

static GeoVector loco_separate(
    const SceneNavEnvComp*   navEnv,
    const EcsEntityId        entity,
    SceneLocomotionComp*     loco,
    const SceneNavAgentComp* navAgent,
    const GeoVector          pos,
    const f32                scale) {
  static const f32 g_sepStrengthBlocker  = 25.0f;
  static const f32 g_sepStrengthOccupant = 10.0f;

  const SceneNavLayer ownLayer = navAgent ? navAgent->layer : SceneNavLayer_Normal;
  const GeoNavGrid*   ownGrid  = scene_nav_grid(navEnv, ownLayer);

  GeoVector force = {0};

  // Separate from blockers on our own layer.
  const GeoVector blockerSep = geo_nav_separate_from_blockers(ownGrid, pos);
  force = geo_vector_add(force, geo_vector_mul(blockerSep, g_sepStrengthBlocker));

  // Separate from nav occupants on our and bigger layers.
  const u64 id          = (u64)entity;
  const f32 radius      = scene_locomotion_radius(loco, scale);
  const f32 weight      = scene_locomotion_weight(loco, scale);
  GeoVector occupantSep = {0};
  for (SceneNavLayer layer = ownLayer; layer != SceneNavLayer_Count; ++layer) {
    const GeoNavGrid* grid     = scene_nav_grid(navEnv, layer);
    const GeoVector   layerSep = geo_nav_separate_from_occupants(grid, id, pos, radius, weight);
    occupantSep                = geo_vector_add(occupantSep, layerSep);
  }
  force = geo_vector_add(force, geo_vector_mul(occupantSep, g_sepStrengthOccupant));

  // For debug purposes save the last occupant separation.
  loco->lastSepMagSqr = geo_vector_mag_sqr(occupantSep);

  return force;
}

ecs_view_define(GlobalView) {
  ecs_access_read(SceneTerrainComp);
  ecs_access_read(SceneNavEnvComp);
  ecs_access_read(SceneTimeComp);
}

ecs_view_define(MoveView) {
  ecs_access_maybe_read(SceneNavAgentComp);
  ecs_access_maybe_read(SceneScaleComp);
  ecs_access_maybe_read(SceneStatusComp);
  ecs_access_maybe_write(SceneAnimationComp);
  ecs_access_maybe_write(SceneLocomotionWheeledComp);
  ecs_access_write(SceneLocomotionComp);
  ecs_access_write(SceneTransformComp);
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
  const f32               timeSec = scene_time_seconds(time);
  const f32               dt      = scene_delta_seconds(time);

  EcsView* moveView = ecs_world_view_t(world, MoveView);
  for (EcsIterator* itr = ecs_view_itr_step(moveView, parCount, parIndex); ecs_view_walk(itr);) {
    const EcsEntityId           entity    = ecs_view_entity(itr);
    SceneAnimationComp*         anim      = ecs_view_write_t(itr, SceneAnimationComp);
    SceneLocomotionComp*        loco      = ecs_view_write_t(itr, SceneLocomotionComp);
    SceneLocomotionWheeledComp* wheeled   = ecs_view_write_t(itr, SceneLocomotionWheeledComp);
    SceneTransformComp*         trans     = ecs_view_write_t(itr, SceneTransformComp);
    const SceneNavAgentComp*    navAgent  = ecs_view_read_t(itr, SceneNavAgentComp);
    const SceneStatusComp*      status    = ecs_view_read_t(itr, SceneStatusComp);
    const SceneScaleComp*       scaleComp = ecs_view_read_t(itr, SceneScaleComp);

    const f32 scale       = scaleComp ? scaleComp->scale : 1.0f;
    const f32 maxSpeedOrg = loco->maxSpeed * scale;
    const f32 maxSpeedMod = maxSpeedOrg * (status ? scene_status_move_speed(status) : 1.0f);

    if (loco->flags & SceneLocomotion_Stop) {
      loco->targetPos = trans->position;
      loco->targetDir = geo_quat_rotate(trans->rotation, geo_forward);
      loco->flags &= ~(SceneLocomotion_Moving | SceneLocomotion_Stop);
    }

    GeoVector posDelta = {0};
    if (loco->flags & SceneLocomotion_Moving) {
      const GeoVector toTarget = geo_vector_xz(geo_vector_sub(loco->targetPos, trans->position));
      const f32       distSqr  = geo_vector_mag_sqr(toTarget);
      if (distSqr <= (loco_arrive_threshold * loco_arrive_threshold)) {
        loco->flags &= ~SceneLocomotion_Moving;
      } else {
        const f32 dist  = math_sqrt_f32(distSqr);
        loco->targetDir = geo_vector_div(toTarget, dist);
        if (!wheeled) {
          posDelta = geo_vector_mul(loco->targetDir, math_min(dist, maxSpeedMod * dt));
        }
      }
    }

    if (wheeled) {
      if (loco->flags & SceneLocomotion_Moving && loco_is_facing(loco, trans)) {
        math_towards_f32(&wheeled->speed, maxSpeedMod, wheeled->acceleration * scale * dt);
      } else {
        math_towards_f32(&wheeled->speed, 0.0f, loco_wheeled_deceleration * scale * dt);
      }
      const GeoVector forwardRaw  = geo_quat_rotate(trans->rotation, geo_forward);
      const GeoVector forwardFlat = geo_vector_norm(geo_vector_xz(forwardRaw));
      posDelta                    = geo_vector_mul(forwardFlat, wheeled->speed * scale * dt);
    }

    if (dt > f32_epsilon) {
      /**
       * Push this entity away from other navigation agents and blockers.
       * NOTE: Use current position instead of the next position to avoid two units moving in the
       * same direction not pushing each other.
       */
      const GeoVector force = loco_separate(navEnv, entity, loco, navAgent, trans->position, scale);
      posDelta              = geo_vector_add(posDelta, geo_vector_mul(force, dt));
    }

    const f32 posDeltaMag = geo_vector_mag(posDelta);
    if (posDeltaMag > 1e-4f || scene_terrain_updated(terrain)) {
      trans->position = geo_vector_add(trans->position, posDelta);
      scene_terrain_snap(terrain, &trans->position);
      loco_validate_pos(trans->position);
      if (wheeled) {
        wheeled->terrainNormal = scene_terrain_normal(terrain, trans->position);
      }
    }

    if (geo_vector_mag_sqr(loco->targetDir) > f32_epsilon) {
      const GeoVector axis      = wheeled ? wheeled->terrainNormal : geo_up;
      const GeoQuat   rotTarget = geo_quat_to_twist(geo_quat_look(loco->targetDir, geo_up), axis);
      const f32       rotSpeed  = loco_rot_speed(loco, entity, timeSec);
      if (geo_quat_towards(&trans->rotation, rotTarget, rotSpeed * dt)) {
        loco->targetDir = geo_vector(0);
      }
    }

    SceneAnimLayer* layerMove = anim ? scene_animation_layer_mut(anim, loco->moveAnimation) : null;
    if (layerMove && dt > f32_epsilon) {
      if (layerMove->weight < f32_epsilon) {
        layerMove->time = 0.0f;
      }
      const f32 targetSpeed  = posDeltaMag / (maxSpeedOrg * dt);
      const f32 targetWeight = targetSpeed >= loco_anim_speed_threshold ? 1.0 : 0.0f;

      math_towards_f32(&layerMove->speed, targetSpeed, loco_anim_speed_ease * dt);
      math_towards_f32(&layerMove->weight, targetWeight, loco_anim_weight_ease * dt);
    }
  }
}

ecs_module_init(scene_locomotion_module) {
  ecs_register_comp(SceneLocomotionComp);
  ecs_register_comp(SceneLocomotionWheeledComp);

  ecs_register_view(GlobalView);
  ecs_register_view(MoveView);

  ecs_register_system(SceneLocomotionMoveSys, ecs_view_id(GlobalView), ecs_view_id(MoveView));

  ecs_order(SceneLocomotionMoveSys, SceneOrder_LocomotionUpdate);

  ecs_parallel(SceneLocomotionMoveSys, g_jobsWorkerCount);
}

f32 scene_locomotion_radius(const SceneLocomotionComp* loco, const f32 scale) {
  return loco->radius * scale;
}

f32 scene_locomotion_weight(const SceneLocomotionComp* loco, const f32 scale) {
  f32 result = loco->weight * scale;
  if (loco->flags & SceneLocomotion_Moving) {
    result *= loco_move_weight_multiplier;
  }
  return result;
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
