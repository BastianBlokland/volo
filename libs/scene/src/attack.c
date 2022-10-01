#include "core_diag.h"
#include "core_math.h"
#include "ecs_world.h"
#include "scene_attack.h"
#include "scene_collision.h"
#include "scene_lifetime.h"
#include "scene_locomotion.h"
#include "scene_projectile.h"
#include "scene_renderable.h"
#include "scene_skeleton.h"
#include "scene_time.h"
#include "scene_transform.h"

#define attack_aim_speed 3.5f

static StringHash g_attackAimAnimHash, g_attackFireAnimHash;

ecs_comp_define_public(SceneAttackComp);

ecs_view_define(GlobalView) { ecs_access_read(SceneTimeComp); }

ecs_view_define(AttackEntityView) {
  ecs_access_maybe_write(SceneLocomotionComp);
  ecs_access_read(SceneTransformComp);
  ecs_access_write(SceneAnimationComp);
  ecs_access_write(SceneAttackComp);
}

ecs_view_define(TargetEntityView) {
  ecs_access_read(SceneCollisionComp);
  ecs_access_maybe_read(SceneTransformComp);
  ecs_access_maybe_read(SceneScaleComp);
}

static GeoVector aim_source_position(EcsIterator* entityItr) {
  const SceneTransformComp* trans = ecs_view_read_t(entityItr, SceneTransformComp);
  return geo_vector_add(trans->position, geo_vector(0, 1.25f, 0));
}

static GeoVector aim_target_position(EcsIterator* targetItr) {
  const SceneCollisionComp* collision    = ecs_view_read_t(targetItr, SceneCollisionComp);
  const SceneTransformComp* trans        = ecs_view_read_t(targetItr, SceneTransformComp);
  const SceneScaleComp*     scale        = ecs_view_read_t(targetItr, SceneScaleComp);
  const GeoBox              targetBounds = scene_collision_world_bounds(collision, trans, scale);
  return geo_box_center(&targetBounds);
}

static void attack_projectile_spawn(
    EcsWorld*         world,
    const EcsEntityId instigator,
    const EcsEntityId graphic,
    const GeoVector   sourcePos,
    const GeoVector   targetPos) {
  diag_assert_msg(graphic, "Projectile graphic missing");

  const EcsEntityId e        = ecs_world_entity_create(world);
  const GeoVector   dir      = geo_vector_norm(geo_vector_sub(targetPos, sourcePos));
  const GeoQuat     rotation = geo_quat_look(dir, geo_up);

  ecs_world_add_t(world, e, SceneRenderableComp, .graphic = graphic);
  ecs_world_add_t(world, e, SceneTransformComp, .position = sourcePos, .rotation = rotation);
  ecs_world_add_t(world, e, SceneLifetimeDurationComp, .duration = time_seconds(5));
  ecs_world_add_t(
      world, e, SceneProjectileComp, .speed = 15, .damage = 10, .instigator = instigator);
}

ecs_system_define(SceneAttackSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneTimeComp* time         = ecs_view_read_t(globalItr, SceneTimeComp);
  const f32            deltaSeconds = scene_delta_seconds(time);

  EcsIterator* targetItr = ecs_view_itr(ecs_world_view_t(world, TargetEntityView));

  EcsView* attackView = ecs_world_view_t(world, AttackEntityView);
  for (EcsIterator* itr = ecs_view_itr(attackView); ecs_view_walk(itr);) {
    const EcsEntityId    entity = ecs_view_entity(itr);
    SceneAnimationComp*  anim   = ecs_view_write_t(itr, SceneAnimationComp);
    SceneAttackComp*     attack = ecs_view_write_t(itr, SceneAttackComp);
    SceneLocomotionComp* loco   = ecs_view_write_t(itr, SceneLocomotionComp);

    const bool hasTarget = ecs_view_maybe_jump(targetItr, attack->targetEntity) != null;
    const bool isAiming  = math_towards_f32(
        &attack->aimNorm, hasTarget ? 1.0f : 0.0f, attack_aim_speed * deltaSeconds);

    scene_animation_set_weight(anim, g_attackAimAnimHash, attack->aimNorm);
    if (!hasTarget) {
      attack->flags &= ~SceneAttackFlags_Firing;
      continue;
    }

    const GeoVector sourcePos = aim_source_position(itr);
    const GeoVector targetPos = aim_target_position(targetItr);

    if (loco) {
      const GeoVector faceDelta = geo_vector_xz(geo_vector_sub(targetPos, sourcePos));
      const GeoVector faceDir   = geo_vector_norm(faceDelta);
      scene_locomotion_face(loco, faceDir);
    }

    SceneAnimLayer* fireAnimLayer = scene_animation_layer(anim, g_attackFireAnimHash);
    diag_assert_msg(fireAnimLayer, "Attacking entity is missing a 'fire' animation");
    fireAnimLayer->flags &= ~SceneAnimFlags_Loop;
    fireAnimLayer->flags |= SceneAnimFlags_AutoWeightFade;

    const bool isFiring = (attack->flags & SceneAttackFlags_Firing) != 0;
    if (!isFiring && isAiming && (time->time - attack->lastFireTime) > attack->interval) {
      // Start firing he shot.
      fireAnimLayer->time = 0.0f;
      attack->flags |= SceneAttackFlags_Firing;
      attack_projectile_spawn(world, entity, attack->projectileGraphic, sourcePos, targetPos);
    }

    if (isFiring && fireAnimLayer->time == fireAnimLayer->duration) {
      // Finished firing the shot.
      attack->flags &= ~SceneAttackFlags_Firing;
      attack->lastFireTime = time->time;
    }
  }
}

ecs_module_init(scene_attack_module) {
  g_attackAimAnimHash  = string_hash_lit("aim");
  g_attackFireAnimHash = string_hash_lit("fire");

  ecs_register_comp(SceneAttackComp);

  ecs_register_view(GlobalView);
  ecs_register_view(AttackEntityView);
  ecs_register_view(TargetEntityView);

  ecs_register_system(
      SceneAttackSys,
      ecs_view_id(GlobalView),
      ecs_view_id(AttackEntityView),
      ecs_view_id(TargetEntityView));
}
