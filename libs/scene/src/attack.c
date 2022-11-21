#include "asset_weapon.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_rng.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "scene_attachment.h"
#include "scene_attack.h"
#include "scene_collision.h"
#include "scene_faction.h"
#include "scene_health.h"
#include "scene_lifetime.h"
#include "scene_locomotion.h"
#include "scene_projectile.h"
#include "scene_renderable.h"
#include "scene_skeleton.h"
#include "scene_time.h"
#include "scene_transform.h"
#include "scene_vfx.h"
#include "scene_weapon.h"

#define attack_in_sight_threshold 0.9995f
#define attack_in_sight_min_dist 2.0f

ecs_comp_define_public(SceneAttackComp);

ecs_view_define(GlobalView) {
  ecs_access_read(SceneCollisionEnvComp);
  ecs_access_read(SceneTimeComp);
  ecs_access_read(SceneWeaponResourceComp);
}

ecs_view_define(WeaponMapView) { ecs_access_read(AssetWeaponMapComp); }

ecs_view_define(GraphicView) { ecs_access_read(SceneSkeletonTemplComp); }

static const AssetWeaponMapComp* attack_weapon_map_get(EcsIterator* globalItr, EcsView* mapView) {
  const SceneWeaponResourceComp* resource = ecs_view_read_t(globalItr, SceneWeaponResourceComp);
  const EcsEntityId              mapAsset = scene_weapon_map(resource);
  EcsIterator*                   itr      = ecs_view_maybe_at(mapView, mapAsset);
  return itr ? ecs_view_read_t(itr, AssetWeaponMapComp) : null;
}

static bool aim_update(
    SceneAttackComp*   attack,
    const AssetWeapon* weapon,
    const f32          deltaSeconds,
    const bool         wantAim) {

  return math_towards_f32(&attack->aimNorm, wantAim ? 1.0f : 0.0f, weapon->aimSpeed * deltaSeconds);
}

static GeoVector aim_target_position(EcsIterator* targetItr) {
  const SceneCollisionComp* collision    = ecs_view_read_t(targetItr, SceneCollisionComp);
  const SceneTransformComp* trans        = ecs_view_read_t(targetItr, SceneTransformComp);
  const SceneScaleComp*     scale        = ecs_view_read_t(targetItr, SceneScaleComp);
  const GeoBox              targetBounds = scene_collision_world_bounds(collision, trans, scale);
  // TODO: Instead of hardcoding an offset we should add a preferred height and clamp to bounds.
  return geo_vector_add(geo_box_center(&targetBounds), geo_vector(0, 0.3f, 0));
}

static SceneLayer damage_ignore_layer(const SceneFaction factionId) {
  switch (factionId) {
  case SceneFaction_A:
    return SceneLayer_UnitFactionA;
  case SceneFaction_B:
    return SceneLayer_UnitFactionB;
  case SceneFaction_C:
    return SceneLayer_UnitFactionC;
  case SceneFaction_D:
    return SceneLayer_UnitFactionD;
  case SceneFaction_None:
    return SceneLayer_None;
  case SceneFaction_Count:
    break;
  }
  diag_crash_msg("Unsupported faction");
}

static SceneLayer damage_query_layer_mask(const SceneFaction factionId) {
  SceneLayer layer = SceneLayer_Unit;
  if (factionId) {
    layer &= ~damage_ignore_layer(factionId);
  }
  return layer;
}

static GeoQuat proj_random_dev(const f32 spreadAngle) {
  const f32 minAngle = -spreadAngle * 0.5f * math_deg_to_rad;
  const f32 maxAngle = spreadAngle * 0.5f * math_deg_to_rad;
  const f32 angle    = rng_sample_range(g_rng, minAngle, maxAngle);
  return geo_quat_angle_axis(geo_up, angle);
}

static bool attack_in_sight(const SceneTransformComp* trans, const GeoVector targetPos) {
  const GeoVector delta   = geo_vector_xz(geo_vector_sub(targetPos, trans->position));
  const f32       sqrDist = geo_vector_mag_sqr(delta);
  if (sqrDist < (attack_in_sight_min_dist * attack_in_sight_min_dist)) {
    return true; // Target is very close, consider it always in-sight.
  }
  const GeoVector forward     = geo_vector_xz(geo_quat_rotate(trans->rotation, geo_forward));
  const GeoVector dirToTarget = geo_vector_div(delta, math_sqrt_f32(sqrDist));
  return geo_vector_dot(forward, dirToTarget) > attack_in_sight_threshold;
}

static TimeDuration attack_next_fire_time(const AssetWeapon* weapon, const TimeDuration timeNow) {
  TimeDuration next = timeNow;
  next += (TimeDuration)rng_sample_range(g_rng, weapon->intervalMin, weapon->intervalMax);
  return next;
}

typedef struct {
  EcsWorld*                     world;
  EcsEntityId                   instigator;
  const SceneCollisionEnvComp*  collisionEnv;
  const AssetWeaponMapComp*     weaponMap;
  const AssetWeapon*            weapon;
  const SceneTransformComp*     trans;
  const SceneScaleComp*         scale;
  const SceneSkeletonComp*      skel;
  const SceneSkeletonTemplComp* skelTempl;
  SceneAttackComp*              attack;
  SceneAnimationComp*           anim;
  SceneFaction                  factionId;
} AttackCtx;

static bool effect_execute_once(const AttackCtx* ctx, const u32 effectIndex) {
  if (ctx->attack->executedEffects & (1 << effectIndex)) {
    return false; // Already executed.
  }
  ctx->attack->executedEffects |= 1 << effectIndex;
  return true;
}

typedef enum {
  EffectResult_Done,
  EffectResult_Running,
} EffectResult;

static EffectResult effect_update_proj(
    const AttackCtx*             ctx,
    const TimeDuration           effectTime,
    const u32                    effectIndex,
    const AssetWeaponEffectProj* def) {

  if (effectTime < def->delay) {
    return EffectResult_Running; // Waiting to execute.
  }
  if (!effect_execute_once(ctx, effectIndex)) {
    return EffectResult_Done; // Already executed.
  }

  const u32 orgIdx = scene_skeleton_joint_by_name(ctx->skelTempl, def->originJoint);
  if (sentinel_check(orgIdx)) {
    log_e("Weapon joint not found", log_param("entity", fmt_int(ctx->instigator, .base = 16)));
    return EffectResult_Done;
  }
  const GeoMatrix orgMat = scene_skeleton_joint_world(ctx->trans, ctx->scale, ctx->skel, orgIdx);
  const GeoVector orgPos = geo_matrix_to_translation(&orgMat);
  const GeoVector dir    = geo_vector_norm(geo_vector_sub(ctx->attack->targetPos, orgPos));
  const GeoQuat   rot = geo_quat_mul(geo_quat_look(dir, geo_up), proj_random_dev(def->spreadAngle));

  const EcsEntityId e = ecs_world_entity_create(ctx->world);
  if (def->vfxProjectile) {
    ecs_world_add_t(ctx->world, e, SceneVfxComp, .asset = def->vfxProjectile);
  }
  if (ctx->factionId != SceneFaction_None) {
    ecs_world_add_t(ctx->world, e, SceneFactionComp, .id = ctx->factionId);
  }
  ecs_world_add_t(ctx->world, e, SceneTransformComp, .position = orgPos, .rotation = rot);
  ecs_world_add_t(ctx->world, e, SceneLifetimeDurationComp, .duration = def->lifetime);
  ecs_world_add_t(
      ctx->world,
      e,
      SceneProjectileComp,
      .speed          = def->speed,
      .damage         = def->damage,
      .impactLifetime = def->impactLifetime,
      .instigator     = ctx->instigator,
      .impactVfx      = def->vfxImpact);

  return EffectResult_Done;
}

static EffectResult effect_update_dmg(
    const AttackCtx*            ctx,
    const TimeDuration          effectTime,
    const u32                   effectIndex,
    const AssetWeaponEffectDmg* def) {

  if (effectTime < def->delay) {
    return EffectResult_Running; // Waiting to execute.
  }
  if (!effect_execute_once(ctx, effectIndex)) {
    return EffectResult_Done; // Already executed.
  }

  const u32 orgIdx = scene_skeleton_joint_by_name(ctx->skelTempl, def->originJoint);
  if (sentinel_check(orgIdx)) {
    log_e("Weapon joint not found", log_param("entity", fmt_int(ctx->instigator, .base = 16)));
    return EffectResult_Done;
  }
  const GeoMatrix orgMat    = scene_skeleton_joint_world(ctx->trans, ctx->scale, ctx->skel, orgIdx);
  const GeoSphere orgSphere = {
      .point  = geo_matrix_to_translation(&orgMat),
      .radius = def->radius * (ctx->scale ? ctx->scale->scale : 1.0f),
  };
  const SceneQueryFilter filter = {
      .layerMask = damage_query_layer_mask(ctx->factionId),
  };
  EcsEntityId hits[scene_query_max_hits];
  const u32   hitCount = scene_query_sphere_all(ctx->collisionEnv, &orgSphere, &filter, hits);

  for (u32 i = 0; i != hitCount; ++i) {
    if (hits[i] == ctx->instigator) {
      continue; // Ignore ourselves.
    }
    const bool hitEntityExists = ecs_world_exists(ctx->world, hits[i]);
    if (hitEntityExists && ecs_world_has_t(ctx->world, hits[i], SceneHealthComp)) {
      scene_health_damage(ctx->world, hits[i], def->damage);
    }
  }

  return EffectResult_Done;
}

static EffectResult effect_update_anim(
    const AttackCtx*             ctx,
    const TimeDuration           effectTime,
    const u32                    effectIndex,
    const AssetWeaponEffectAnim* def) {

  if (effectTime < def->delay) {
    return EffectResult_Running; // Waiting to execute.
  }

  SceneAnimLayer* animLayer = scene_animation_layer(ctx->anim, def->layer);
  if (UNLIKELY(!animLayer)) {
    log_e("Weapon animation not found", log_param("entity", fmt_int(ctx->instigator, .base = 16)));
    return EffectResult_Done;
  }

  if (effect_execute_once(ctx, effectIndex)) {
    animLayer->flags &= ~SceneAnimFlags_Loop;    // Don't loop animation.
    animLayer->flags |= SceneAnimFlags_AutoFade; // Automatically blend-in and out.
    animLayer->time  = 0.0f;                     // Restart the animation.
    animLayer->speed = def->speed;
    return EffectResult_Running;
  }

  // Keep running until the animation reaches the end.
  return animLayer->time == animLayer->duration ? EffectResult_Done : EffectResult_Running;
}

static EffectResult effect_update_vfx(
    const AttackCtx*            ctx,
    const TimeDuration          effectTime,
    const u32                   effectIndex,
    const AssetWeaponEffectVfx* def) {

  if (effectTime < def->delay) {
    return EffectResult_Running; // Waiting to execute.
  }
  if (!effect_execute_once(ctx, effectIndex)) {
    return (effectTime - def->delay) < def->duration ? EffectResult_Running : EffectResult_Done;
  }

  const EcsEntityId inst           = ctx->instigator;
  const u32         jointOriginIdx = scene_skeleton_joint_by_name(ctx->skelTempl, def->originJoint);
  if (UNLIKELY(sentinel_check(jointOriginIdx))) {
    log_e("Weapon joint not found", log_param("entity", fmt_int(inst, .base = 16)));
    return EffectResult_Done;
  }

  const EcsEntityId e = ecs_world_entity_create(ctx->world);
  ecs_world_add_t(ctx->world, e, SceneTransformComp, .position = {0}, .rotation = geo_quat_ident);
  ecs_world_add_t(ctx->world, e, SceneLifetimeDurationComp, .duration = def->duration);
  ecs_world_add_t(ctx->world, e, SceneVfxComp, .asset = def->asset);
  ecs_world_add_t(ctx->world, e, SceneAttachmentComp, .target = inst, .jointIndex = jointOriginIdx);

  return EffectResult_Done;
}

static EffectResult effect_update(const AttackCtx* ctx, const TimeDuration effectTime) {
  diag_assert(ctx->weapon->effectCount <= sizeof(ctx->attack->executedEffects) * 8);

  EffectResult result = EffectResult_Done;
  for (u16 i = 0; i != ctx->weapon->effectCount; ++i) {
    const AssetWeaponEffect* effect = &ctx->weaponMap->effects[ctx->weapon->effectIndex + i];
    switch (effect->type) {
    case AssetWeaponEffect_Projectile:
      result |= effect_update_proj(ctx, effectTime, i, &effect->data_proj);
      break;
    case AssetWeaponEffect_Damage:
      result |= effect_update_dmg(ctx, effectTime, i, &effect->data_dmg);
      break;
    case AssetWeaponEffect_Animation:
      result |= effect_update_anim(ctx, effectTime, i, &effect->data_anim);
      break;
    case AssetWeaponEffect_Vfx:
      result |= effect_update_vfx(ctx, effectTime, i, &effect->data_vfx);
    }
  }
  return result;
}

ecs_view_define(AttackView) {
  ecs_access_maybe_read(SceneFactionComp);
  ecs_access_maybe_read(SceneScaleComp);
  ecs_access_maybe_write(SceneLocomotionComp);
  ecs_access_read(SceneRenderableComp);
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
  const SceneCollisionEnvComp* collisionEnv = ecs_view_read_t(globalItr, SceneCollisionEnvComp);
  const SceneTimeComp*         time         = ecs_view_read_t(globalItr, SceneTimeComp);
  const f32                    deltaSeconds = scene_delta_seconds(time);

  EcsView*                  weaponMapView = ecs_world_view_t(world, WeaponMapView);
  const AssetWeaponMapComp* weaponMap     = attack_weapon_map_get(globalItr, weaponMapView);
  if (!weaponMap) {
    return; // Weapon-map not loaded yet.
  }

  EcsIterator* targetItr  = ecs_view_itr(ecs_world_view_t(world, TargetView));
  EcsIterator* graphicItr = ecs_view_itr(ecs_world_view_t(world, GraphicView));

  EcsView* attackView = ecs_world_view_t(world, AttackView);
  for (EcsIterator* itr = ecs_view_itr_step(attackView, parCount, parIndex); ecs_view_walk(itr);) {
    const EcsEntityId          entity     = ecs_view_entity(itr);
    const SceneFactionComp*    faction    = ecs_view_read_t(itr, SceneFactionComp);
    const SceneRenderableComp* renderable = ecs_view_read_t(itr, SceneRenderableComp);
    const SceneScaleComp*      scale      = ecs_view_read_t(itr, SceneScaleComp);
    const SceneSkeletonComp*   skel       = ecs_view_read_t(itr, SceneSkeletonComp);
    const SceneTransformComp*  trans      = ecs_view_read_t(itr, SceneTransformComp);
    SceneAnimationComp*        anim       = ecs_view_write_t(itr, SceneAnimationComp);
    SceneAttackComp*           attack     = ecs_view_write_t(itr, SceneAttackComp);
    SceneLocomotionComp*       loco       = ecs_view_write_t(itr, SceneLocomotionComp);

    if (!ecs_view_maybe_jump(graphicItr, renderable->graphic)) {
      continue; // Graphic is missing required components.
    }
    const SceneSkeletonTemplComp* skelTempl = ecs_view_read_t(graphicItr, SceneSkeletonTemplComp);

    if (UNLIKELY(!attack->weaponName)) {
      continue; // Entity has no weapon equipped.
    }
    const AssetWeapon* weapon = asset_weapon_get(weaponMap, attack->weaponName);
    if (UNLIKELY(!weapon)) {
      log_e("Weapon not found", log_param("entity", fmt_int(entity, .base = 16)));
      continue;
    }

    const TimeDuration timeSinceLastFire = time->time - attack->lastFireTime;
    const bool         hasTarget = ecs_view_maybe_jump(targetItr, attack->targetEntity) != null;
    const bool         isMoving  = loco && (loco->flags & SceneLocomotion_Moving) != 0;
    const bool shouldAim = !isMoving && (hasTarget || timeSinceLastFire < weapon->aimMinTime);

    const bool isAiming = aim_update(attack, weapon, deltaSeconds, shouldAim);
    if (weapon->aimAnim) {
      scene_animation_set_weight(anim, weapon->aimAnim, attack->aimNorm);
    }

    // Potentially start a new attack.
    if (hasTarget) {
      const GeoVector targetPos = aim_target_position(targetItr);
      if (loco) {
        const GeoVector faceDelta = geo_vector_xz(geo_vector_sub(targetPos, trans->position));
        const f32       faceDist  = geo_vector_mag(faceDelta);
        if (faceDist > f32_epsilon) {
          const GeoVector faceDir = geo_vector_div(faceDelta, faceDist);
          scene_locomotion_face(loco, faceDir);
        }
      }

      const bool isFiring      = (attack->flags & SceneAttackFlags_Firing) != 0;
      const bool isCoolingDown = time->time < attack->nextFireTime;

      if (isAiming && !isFiring && !isCoolingDown && attack_in_sight(trans, targetPos)) {
        // Start the attack..
        attack->lastFireTime = time->time;
        attack->flags |= SceneAttackFlags_Firing;
        attack->executedEffects = 0;
        attack->targetPos       = targetPos;
      }
    }

    // Update the current attack.
    if (attack->flags & SceneAttackFlags_Firing) {
      const AttackCtx ctx = {
          .world        = world,
          .instigator   = entity,
          .collisionEnv = collisionEnv,
          .weaponMap    = weaponMap,
          .weapon       = weapon,
          .trans        = trans,
          .scale        = scale,
          .skel         = skel,
          .skelTempl    = skelTempl,
          .attack       = attack,
          .anim         = anim,
          .factionId    = LIKELY(faction) ? faction->id : SceneFaction_None,
      };
      const TimeDuration effectTime = time->time - attack->lastFireTime;
      if (effect_update(&ctx, effectTime) == EffectResult_Done) {
        // Finish the attack.
        attack->flags &= ~SceneAttackFlags_Firing;
        attack->nextFireTime = attack_next_fire_time(weapon, time->time);
      }
    }
  }
}

ecs_module_init(scene_attack_module) {
  ecs_register_comp(SceneAttackComp);

  ecs_register_view(GlobalView);
  ecs_register_view(WeaponMapView);
  ecs_register_view(GraphicView);

  ecs_register_system(
      SceneAttackSys,
      ecs_view_id(GlobalView),
      ecs_view_id(WeaponMapView),
      ecs_view_id(GraphicView),
      ecs_register_view(AttackView),
      ecs_register_view(TargetView));

  ecs_parallel(SceneAttackSys, 4);
}
