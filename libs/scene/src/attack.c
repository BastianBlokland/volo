#include "core_diag.h"
#include "core_math.h"
#include "core_rng.h"
#include "ecs_world.h"
#include "scene_attachment.h"
#include "scene_attack.h"
#include "scene_collision.h"
#include "scene_faction.h"
#include "scene_lifetime.h"
#include "scene_locomotion.h"
#include "scene_projectile.h"
#include "scene_renderable.h"
#include "scene_skeleton.h"
#include "scene_time.h"
#include "scene_transform.h"
#include "scene_vfx.h"

#define attack_aim_speed 3.5f
#define attack_in_sight_threshold 0.95f
#define attack_in_sight_min_dist 2.0f

static StringHash g_attackAimAnimHash, g_attackFireAnimHash;

ecs_comp_define_public(SceneAttackComp);
ecs_comp_define(SceneAttackAnimComp) { u32 muzzleJoint; };

ecs_view_define(GlobalView) { ecs_access_read(SceneTimeComp); }

ecs_view_define(AttackInitView) {
  ecs_access_read(SceneRenderableComp);
  ecs_access_with(SceneAnimationComp);
  ecs_access_with(SceneAttackComp);
  ecs_access_without(SceneAttackAnimComp);
}

ecs_view_define(AttackGraphicView) { ecs_access_read(SceneSkeletonTemplComp); }

ecs_system_define(SceneAttackInitSys) {
  EcsIterator* graphicItr = ecs_view_itr(ecs_world_view_t(world, AttackGraphicView));

  EcsView* initView = ecs_world_view_t(world, AttackInitView);
  for (EcsIterator* itr = ecs_view_itr(initView); ecs_view_walk(itr);) {
    const EcsEntityId          entity     = ecs_view_entity(itr);
    const SceneRenderableComp* renderable = ecs_view_read_t(itr, SceneRenderableComp);

    if (ecs_view_maybe_jump(graphicItr, renderable->graphic)) {
      const SceneSkeletonTemplComp* skelTempl = ecs_view_read_t(graphicItr, SceneSkeletonTemplComp);

      const u32 muzzleJoint = scene_skeleton_joint_by_name(skelTempl, string_hash_lit("muzzle"));
      diag_assert_msg(!sentinel_check(muzzleJoint), "No 'muzzle' joint found ");
      ecs_world_add_t(world, entity, SceneAttackAnimComp, .muzzleJoint = muzzleJoint);
    }
  }
}

static GeoVector aim_target_position(EcsIterator* targetItr) {
  const SceneCollisionComp* collision    = ecs_view_read_t(targetItr, SceneCollisionComp);
  const SceneTransformComp* trans        = ecs_view_read_t(targetItr, SceneTransformComp);
  const SceneScaleComp*     scale        = ecs_view_read_t(targetItr, SceneScaleComp);
  const GeoBox              targetBounds = scene_collision_world_bounds(collision, trans, scale);
  // TODO: Instead of hardcoding an offset we should add a preferred height and clamp to bounds.
  return geo_vector_add(geo_box_center(&targetBounds), geo_vector(0, 0.3f, 0));
}

static void attack_muzzleflash_spawn(
    EcsWorld*              world,
    const SceneAttackComp* attack,
    const EcsEntityId      instigator,
    const u32              muzzleJoint) {
  const EcsEntityId e = ecs_world_entity_create(world);
  ecs_world_add_t(world, e, SceneTransformComp, .position = {0}, .rotation = geo_quat_ident);
  ecs_world_add_t(world, e, SceneLifetimeDurationComp, .duration = time_milliseconds(125));
  ecs_world_add_t(world, e, SceneVfxComp, .asset = attack->muzzleFlashVfx);
  ecs_world_add_t(world, e, SceneAttachmentComp, .target = instigator, .jointIndex = muzzleJoint);
}

static void attack_projectile_spawn(
    EcsWorld*              world,
    const SceneAttackComp* attack,
    const EcsEntityId      instigator,
    const SceneFaction     factionId,
    const GeoMatrix*       muzzleMatrix,
    const GeoVector        targetPos) {
  const EcsEntityId e         = ecs_world_entity_create(world);
  const GeoVector   sourcePos = geo_matrix_to_translation(muzzleMatrix);
  const GeoVector   dir       = geo_vector_norm(geo_vector_sub(targetPos, sourcePos));
  const GeoQuat     rotation  = geo_quat_look(dir, geo_up);

  if (attack->projectileVfx) {
    ecs_world_add_t(world, e, SceneVfxComp, .asset = attack->projectileVfx);
  }
  if (factionId != SceneFaction_None) {
    ecs_world_add_t(world, e, SceneFactionComp, .id = factionId);
  }
  ecs_world_add_t(world, e, SceneTransformComp, .position = sourcePos, .rotation = rotation);
  ecs_world_add_t(world, e, SceneLifetimeDurationComp, .duration = time_seconds(2));
  ecs_world_add_t(
      world,
      e,
      SceneProjectileComp,
      .delay      = time_milliseconds(25),
      .speed      = 50,
      .damage     = 10,
      .instigator = instigator,
      .impactVfx  = attack->impactVfx);
}

static bool attack_in_sight(const SceneTransformComp* trans, const GeoVector targetPos) {
  const GeoVector delta = geo_vector_xz(geo_vector_sub(targetPos, trans->position));
  if (geo_vector_mag_sqr(delta) < (attack_in_sight_min_dist * attack_in_sight_min_dist)) {
    return true; // Target is very close, consider it always in-sight.
  }
  const GeoVector forward = geo_vector_xz(geo_quat_rotate(trans->rotation, geo_forward));
  return geo_vector_dot(forward, delta) > attack_in_sight_threshold;
}

static TimeDuration attack_next_time(const SceneAttackComp* attack, const TimeDuration timeNow) {
  TimeDuration next = timeNow;
  next += (TimeDuration)rng_sample_range(g_rng, attack->minInterval, attack->maxInterval);
  return next;
}

ecs_view_define(AttackView) {
  ecs_access_maybe_read(SceneFactionComp);
  ecs_access_maybe_read(SceneScaleComp);
  ecs_access_maybe_write(SceneLocomotionComp);
  ecs_access_read(SceneAttackAnimComp);
  ecs_access_read(SceneSkeletonComp);
  ecs_access_read(SceneTransformComp);
  ecs_access_write(SceneAnimationComp);
  ecs_access_write(SceneAttackComp);
}

ecs_view_define(TargetView) {
  ecs_access_read(SceneCollisionComp);
  ecs_access_maybe_read(SceneTransformComp);
  ecs_access_maybe_read(SceneScaleComp);
}

ecs_system_define(SceneAttackSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneTimeComp* time         = ecs_view_read_t(globalItr, SceneTimeComp);
  const f32            deltaSeconds = scene_delta_seconds(time);

  EcsIterator* targetItr = ecs_view_itr(ecs_world_view_t(world, TargetView));

  EcsView* attackView = ecs_world_view_t(world, AttackView);
  for (EcsIterator* itr = ecs_view_itr(attackView); ecs_view_walk(itr);) {
    const EcsEntityId          entity     = ecs_view_entity(itr);
    const SceneAttackAnimComp* attackAnim = ecs_view_read_t(itr, SceneAttackAnimComp);
    const SceneScaleComp*      scale      = ecs_view_read_t(itr, SceneScaleComp);
    const SceneSkeletonComp*   skel       = ecs_view_read_t(itr, SceneSkeletonComp);
    const SceneTransformComp*  trans      = ecs_view_read_t(itr, SceneTransformComp);
    const SceneFactionComp*    faction    = ecs_view_read_t(itr, SceneFactionComp);
    SceneAnimationComp*        anim       = ecs_view_write_t(itr, SceneAnimationComp);
    SceneAttackComp*           attack     = ecs_view_write_t(itr, SceneAttackComp);
    SceneLocomotionComp*       loco       = ecs_view_write_t(itr, SceneLocomotionComp);

    const bool hasTarget = ecs_view_maybe_jump(targetItr, attack->targetEntity) != null;
    const bool isAiming  = math_towards_f32(
        &attack->aimNorm, hasTarget ? 1.0f : 0.0f, attack_aim_speed * deltaSeconds);

    scene_animation_set_weight(anim, g_attackAimAnimHash, attack->aimNorm);
    if (!hasTarget) {
      attack->flags &= ~SceneAttackFlags_Firing;
      continue;
    }

    const GeoVector targetPos = aim_target_position(targetItr);

    if (loco) {
      const GeoVector faceDelta = geo_vector_xz(geo_vector_sub(targetPos, trans->position));
      const f32       faceDist  = geo_vector_mag(faceDelta);
      if (faceDist > f32_epsilon) {
        const GeoVector faceDir = geo_vector_div(faceDelta, faceDist);
        scene_locomotion_face(loco, faceDir);
      }
    }

    SceneAnimLayer* fireAnimLayer = scene_animation_layer(anim, g_attackFireAnimHash);
    diag_assert_msg(fireAnimLayer, "Attacking entity is missing a 'fire' animation");
    fireAnimLayer->flags &= ~SceneAnimFlags_Loop;
    fireAnimLayer->flags |= SceneAnimFlags_AutoFade;

    const bool isFiring      = (attack->flags & SceneAttackFlags_Firing) != 0;
    const bool isCoolingDown = time->time < attack->nextFireTime;

    if (isAiming && !isFiring && !isCoolingDown && attack_in_sight(trans, targetPos)) {
      // Start firing the shot.
      fireAnimLayer->time = 0.0f;
      attack->flags |= SceneAttackFlags_Firing;

      const SceneFaction factionId = LIKELY(faction) ? faction->id : SceneFaction_None;
      const GeoMatrix muz = scene_skeleton_joint_world(trans, scale, skel, attackAnim->muzzleJoint);
      attack_projectile_spawn(world, attack, entity, factionId, &muz, targetPos);

      if (attack->muzzleFlashVfx) {
        attack_muzzleflash_spawn(world, attack, entity, attackAnim->muzzleJoint);
      }
    }

    if (isFiring && fireAnimLayer->time == fireAnimLayer->duration) {
      // Finished firing the shot.
      attack->flags &= ~SceneAttackFlags_Firing;
      attack->nextFireTime = attack_next_time(attack, time->time);
    }
  }
}

ecs_module_init(scene_attack_module) {
  g_attackAimAnimHash  = string_hash_lit("aim");
  g_attackFireAnimHash = string_hash_lit("fire");

  ecs_register_comp(SceneAttackComp);
  ecs_register_comp(SceneAttackAnimComp);

  ecs_register_view(GlobalView);

  ecs_register_system(
      SceneAttackInitSys, ecs_register_view(AttackInitView), ecs_register_view(AttackGraphicView));

  ecs_register_system(
      SceneAttackSys,
      ecs_view_id(GlobalView),
      ecs_register_view(AttackView),
      ecs_register_view(TargetView));
}
