#include "asset_weapon.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_float.h"
#include "core_math.h"
#include "core_rng.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "geo_box_rotated.h"
#include "geo_sphere.h"
#include "log_logger.h"
#include "scene_attachment.h"
#include "scene_attack.h"
#include "scene_collision.h"
#include "scene_faction.h"
#include "scene_health.h"
#include "scene_lifetime.h"
#include "scene_location.h"
#include "scene_locomotion.h"
#include "scene_prefab.h"
#include "scene_projectile.h"
#include "scene_renderable.h"
#include "scene_skeleton.h"
#include "scene_sound.h"
#include "scene_status.h"
#include "scene_time.h"
#include "scene_transform.h"
#include "scene_vfx.h"
#include "scene_visibility.h"
#include "scene_weapon.h"

#define attack_in_sight_threshold 0.99f
#define attack_in_sight_min_dist 1.0f
#define attack_aim_reset_time time_seconds(5)

ecs_comp_define_public(SceneAttackComp);
ecs_comp_define_public(SceneAttackAimComp);

ecs_comp_define(SceneAttackTraceComp) {
  DynArray events; // SceneAttackEvent[].
};

static void ecs_destruct_attack_trace(void* data) {
  SceneAttackTraceComp* comp = data;
  dynarray_destroy(&comp->events);
}

ecs_view_define(GlobalView) {
  ecs_access_read(SceneCollisionEnvComp);
  ecs_access_read(SceneTimeComp);
  ecs_access_read(SceneWeaponResourceComp);
}

ecs_view_define(WeaponMapView) { ecs_access_read(AssetWeaponMapComp); }
ecs_view_define(GraphicView) { ecs_access_read(SceneSkeletonTemplComp); }

static void attack_trace_start(EcsWorld* world, const EcsEntityId entity) {
  ecs_world_add_t(
      world,
      entity,
      SceneAttackTraceComp,
      .events = dynarray_create_t(g_allocHeap, SceneAttackEvent, 4));
}

static void attack_trace_stop(EcsWorld* world, const EcsEntityId entity) {
  ecs_world_remove_t(world, entity, SceneAttackTraceComp);
}

static void attack_trace_prune_expired(SceneAttackTraceComp* trace, const TimeReal timestamp) {
  for (usize i = trace->events.size; i-- != 0;) {
    if (dynarray_at_t(&trace->events, i, SceneAttackEvent)->expireTimestamp < timestamp) {
      dynarray_remove_unordered(&trace->events, i, 1);
    }
  }
}

static void attack_trace_add(SceneAttackTraceComp* trace, const SceneAttackEvent* event) {
  *dynarray_push_t(&trace->events, SceneAttackEvent) = *event;
}

static const AssetWeaponMapComp* attack_weapon_map_get(EcsIterator* globalItr, EcsView* mapView) {
  const SceneWeaponResourceComp* resource = ecs_view_read_t(globalItr, SceneWeaponResourceComp);
  const EcsEntityId              mapAsset = scene_weapon_map(resource);
  EcsIterator*                   itr      = ecs_view_maybe_at(mapView, mapAsset);
  return itr ? ecs_view_read_t(itr, AssetWeaponMapComp) : null;
}

static void aim_face(
    SceneAttackAimComp*       attackAim,
    SceneLocomotionComp*      loco,
    const SceneTransformComp* trans,
    const GeoVector           targetPos) {

  const GeoVector delta = geo_vector_xz(geo_vector_sub(targetPos, trans->position));
  const f32       dist  = geo_vector_mag(delta);
  const GeoVector dir   = dist <= f32_epsilon ? geo_forward : geo_vector_div(delta, dist);

  if (attackAim) {
    scene_attack_aim(attackAim, trans, dir);
    return;
  }
  if (loco) {
    scene_locomotion_face(loco, dir);
    return;
  }
}

static GeoVector
aim_position(const GeoVector origin, EcsIterator* targetItr, const TimeDuration timeInFuture) {
  const SceneTransformComp* tgtTrans = ecs_view_read_t(targetItr, SceneTransformComp);
  const SceneScaleComp*     tgtScale = ecs_view_read_t(targetItr, SceneScaleComp);
  const SceneVelocityComp*  tgtVelo  = ecs_view_read_t(targetItr, SceneVelocityComp);
  const SceneLocationComp*  tgtLoc   = ecs_view_read_t(targetItr, SceneLocationComp);

  if (tgtLoc) {
    const GeoBoxRotated aimVolume = scene_location_predict(
        tgtLoc, tgtTrans, tgtScale, tgtVelo, SceneLocationType_AimTarget, timeInFuture);
    return geo_box_rotated_closest_point(&aimVolume, origin);
  }
  return scene_position_predict(tgtTrans, tgtVelo, timeInFuture);
}

static f32 aim_estimate_distance(const GeoVector origin, EcsIterator* targetItr) {
  const SceneTransformComp* tgtTrans = ecs_view_read_t(targetItr, SceneTransformComp);
  return geo_vector_mag(geo_vector_sub(tgtTrans->position, origin));
}

static GeoVector aim_estimate_impact_point(const GeoVector origin, EcsIterator* targetItr) {
  const SceneTransformComp* tgtTrans     = ecs_view_read_t(targetItr, SceneTransformComp);
  const SceneScaleComp*     tgtScale     = ecs_view_read_t(targetItr, SceneScaleComp);
  const SceneCollisionComp* tgtCollision = ecs_view_read_t(targetItr, SceneCollisionComp);
  const SceneLocationComp*  tgtLoc       = ecs_view_read_t(targetItr, SceneLocationComp);

  GeoVector targetPos;
  if (tgtLoc) {
    const GeoBoxRotated aimVolume =
        scene_location(tgtLoc, tgtTrans, tgtScale, SceneLocationType_AimTarget);
    targetPos = geo_box_rotated_closest_point(&aimVolume, origin);
  } else {
    targetPos = tgtTrans->position;
  }
  const GeoVector toTarget     = geo_vector_sub(targetPos, origin);
  const f32       toTargetDist = geo_vector_mag(toTarget);
  if (toTargetDist <= f32_epsilon) {
    return origin;
  }
  const GeoRay ray  = {.point = origin, .dir = geo_vector_div(toTarget, toTargetDist)};
  const f32    tgtT = scene_collision_intersect_ray(tgtCollision, tgtTrans, tgtScale, &ray);
  return tgtT > 0 ? geo_ray_position(&ray, tgtT) : origin;
}

static SceneLayer damage_query_layer_mask(const SceneFaction factionId) {
  SceneLayer mask = SceneLayer_Unit | SceneLayer_Destructible;
  if (factionId != SceneFaction_None) {
    mask &= ~scene_faction_layers(factionId); // Ignore units in from the same faction.
  }
  return mask;
}

static GeoQuat proj_random_dev(const f32 spreadAngle) {
  const f32 minAngle = -spreadAngle * 0.5f * math_deg_to_rad;
  const f32 maxAngle = spreadAngle * 0.5f * math_deg_to_rad;
  const f32 angle    = rng_sample_range(g_rng, minAngle, maxAngle);
  return geo_quat_angle_axis(angle, geo_up);
}

static bool attack_in_sight(const GeoVector pos, const GeoQuat aimRot, const GeoVector targetPos) {
  const GeoVector delta   = geo_vector_xz(geo_vector_sub(targetPos, pos));
  const f32       sqrDist = geo_vector_mag_sqr(delta);
  if (sqrDist < (attack_in_sight_min_dist * attack_in_sight_min_dist)) {
    return true; // Target is very close, consider it always in-sight.
  }
  const GeoVector forward = geo_vector_norm(geo_vector_xz(geo_quat_rotate(aimRot, geo_forward)));
  const GeoVector dirToTarget = geo_vector_div(delta, math_sqrt_f32(sqrDist));
  return geo_vector_dot(forward, dirToTarget) > attack_in_sight_threshold;
}

static TimeDuration attack_next_fire_time(const AssetWeapon* weapon, const TimeDuration timeNow) {
  TimeDuration next = timeNow;
  next += (TimeDuration)rng_sample_range(g_rng, weapon->intervalMin, weapon->intervalMax);
  return next;
}

static TimeDuration weapon_estimate_impact_time(
    const AssetWeaponMapComp* weaponMap, const AssetWeapon* weapon, const f32 estimatedDistance) {
  TimeDuration result = 0;
  for (u16 i = 0; i != weapon->effectCount; ++i) {
    const AssetWeaponEffect* effect = &weaponMap->effects.values[weapon->effectIndex + i];
    switch (effect->type) {
    case AssetWeaponEffect_Projectile: {
      const f32          flightTimeSeconds = estimatedDistance / effect->data_proj.speed;
      const TimeDuration flightTime        = (TimeDuration)time_seconds(flightTimeSeconds);
      result                               = math_max(result, effect->data_proj.delay + flightTime);
    } break;
    case AssetWeaponEffect_Damage:
      result = math_max(result, effect->data_dmg.delay);
      break;
    default:
      break;
    }
  }
  return result;
}

static void weapon_damage_frustum(
    const GeoVector pos,
    const GeoVector direction,
    const f32       length,
    const f32       radiusBegin,
    const f32       radiusEnd,
    GeoVector       outPoints[PARAM_ARRAY_SIZE(8)]) {
  const GeoVector right  = geo_vector_norm(geo_vector_cross3(direction, geo_up));
  const GeoVector up     = geo_vector_cross3(direction, right);
  const GeoVector endPos = geo_vector_add(pos, geo_vector_mul(direction, length));

  static const f32 g_pointsLocal[][2] = {
      {-1.0f, -1.0f},
      {1.0f, -1.0f},
      {1.0f, 1.0f},
      {-1.0f, 1.0f},
  };

  for (u32 i = 0; i != array_elems(g_pointsLocal); ++i) {
    outPoints[i] = geo_vector_add(
        geo_vector_add(pos, geo_vector_mul(right, g_pointsLocal[i][0] * radiusBegin)),
        geo_vector_mul(up, g_pointsLocal[i][1] * radiusBegin));
  }
  for (u32 i = 0; i != array_elems(g_pointsLocal); ++i) {
    outPoints[4 + i] = geo_vector_add(
        geo_vector_add(endPos, geo_vector_mul(right, g_pointsLocal[i][0] * radiusEnd)),
        geo_vector_mul(up, g_pointsLocal[i][1] * radiusEnd));
  }
}

typedef struct {
  EcsWorld*                     world;
  EcsView*                      targetView;
  EcsEntityId                   instigator;
  const SceneCollisionEnvComp*  collisionEnv;
  const AssetWeaponMapComp*     weaponMap;
  const AssetWeapon*            weapon;
  const SceneTransformComp*     trans;
  const SceneScaleComp*         scale;
  const SceneSkeletonComp*      skel;
  const SceneSkeletonTemplComp* skelTempl;
  const SceneStatusComp*        status;
  SceneAttackComp*              attack;
  SceneAttackTraceComp*         trace;
  SceneAnimationComp*           anim;
  SceneFaction                  factionId;
  TimeDuration                  time;
  f32                           deltaSeconds;
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
    log_e("Weapon joint not found", log_param("entity", ecs_entity_fmt(ctx->instigator)));
    return EffectResult_Done;
  }
  const GeoMatrix orgMat = scene_skeleton_joint_world(ctx->trans, ctx->scale, ctx->skel, orgIdx);
  const GeoVector orgPos = geo_matrix_to_translation(&orgMat);

  GeoVector dir;
  if (def->launchTowardsTarget) {
    dir = geo_vector_norm(geo_vector_sub(ctx->attack->targetPos, orgPos));
  } else {
    // HACK: Using up instead of forward because the joints created by blender use that orientation.
    dir = geo_matrix_transform3(&orgMat, geo_up);
  }
  const GeoQuat rot = geo_quat_mul(geo_quat_look(dir, geo_up), proj_random_dev(def->spreadAngle));

  if (ctx->trace) {
    const SceneAttackEvent evt = {
        .type            = SceneAttackEventType_Proj,
        .expireTimestamp = ctx->time + time_milliseconds(250),
        .data_proj       = {.pos = orgPos, .target = ctx->attack->targetPos},
    };
    attack_trace_add(ctx->trace, &evt);
  }

  const EcsEntityId projectileEntity = scene_prefab_spawn(
      ctx->world,
      &(ScenePrefabSpec){
          .flags    = ScenePrefabFlags_Volatile,
          .prefabId = def->projectilePrefab,
          .faction  = ctx->factionId,
          .position = orgPos,
          .rotation = rot,
          .scale    = 1.0f,
      });

  const f32 damageMult = ctx->status ? scene_status_damage(ctx->status) : 1.0f;

  SceneProjectileFlags projectileFlags = 0;
  projectileFlags |= def->seekTowardsTarget ? SceneProjectile_Seek : 0;

  ecs_world_add_t(
      ctx->world,
      projectileEntity,
      SceneProjectileComp,
      .flags        = projectileFlags,
      .applyStatus  = def->applyStatusMask,
      .speed        = def->speed,
      .damage       = def->damage * damageMult,
      .damageRadius = def->damageRadius,
      .destroyDelay = def->destroyDelay,
      .instigator   = ctx->instigator,
      .impactPrefab = def->impactPrefab,
      .seekEntity   = ctx->attack->targetCurrent,
      .seekPos      = ctx->attack->targetPos);

  // Seeing attacks requires visibility.
  ecs_world_add_t(ctx->world, projectileEntity, SceneVisibilityComp);

  return EffectResult_Done;
}

static EffectResult effect_update_dmg(
    const AttackCtx*            ctx,
    const TimeDuration          effectTime,
    const u32                   effectIndex,
    const bool                  interrupt,
    const AssetWeaponEffectDmg* def) {

  if (effectTime < def->delay) {
    return interrupt ? EffectResult_Done : EffectResult_Running; // Waiting to execute.
  }
  const bool firstExecution = effect_execute_once(ctx, effectIndex);
  if (!def->continuous && !firstExecution) {
    return EffectResult_Done; // Already executed.
  }

  const u32 orgIdx = scene_skeleton_joint_by_name(ctx->skelTempl, def->originJoint);
  if (sentinel_check(orgIdx)) {
    log_e("Weapon joint not found", log_param("entity", ecs_entity_fmt(ctx->instigator)));
    return EffectResult_Done;
  }
  const GeoMatrix orgMat = scene_skeleton_joint_world(ctx->trans, ctx->scale, ctx->skel, orgIdx);
  const GeoVector orgPos = geo_matrix_to_translation(&orgMat);
  EcsEntityId     hits[scene_query_max_hits];
  u32             hitCount;

  const SceneQueryFilter filter = {
      .layerMask = damage_query_layer_mask(ctx->factionId),
  };

  if (def->length > f32_epsilon) {
    f32 effectiveLength = def->length;
    if (def->lengthGrowTime) {
      effectiveLength *= math_min(1.0f, (f32)(effectTime - def->delay) / (f32)def->lengthGrowTime);
    }

    // HACK: Using up instead of forward because the joints created by blender use that orientation.
    const GeoVector dir = geo_vector_norm(geo_matrix_transform3(&orgMat, geo_up));
    GeoVector       frustum[8];
    weapon_damage_frustum(orgPos, dir, effectiveLength, def->radius, def->radiusEnd, frustum);
    hitCount = scene_query_frustum_all(ctx->collisionEnv, frustum, &filter, hits);

    if (ctx->trace) {
      const SceneAttackEvent evt = {
          .type            = SceneAttackEventType_DmgFrustum,
          .expireTimestamp = def->continuous ? 0 : ctx->time + time_milliseconds(250),
      };
      mem_cpy(array_mem(evt.data_dmgFrustum.corners), array_mem(frustum));
      attack_trace_add(ctx->trace, &evt);
    }
  } else {
    const GeoSphere orgSphere = {
        .point  = orgPos,
        .radius = def->radius * (ctx->scale ? ctx->scale->scale : 1.0f),
    };
    hitCount = scene_query_sphere_all(ctx->collisionEnv, &orgSphere, &filter, hits);

    if (ctx->trace) {
      const SceneAttackEvent evt = {
          .type            = SceneAttackEventType_DmgSphere,
          .expireTimestamp = def->continuous ? 0 : ctx->time + time_milliseconds(250),
          .data_dmgSphere  = {.pos = orgSphere.point, .radius = orgSphere.radius},
      };
      attack_trace_add(ctx->trace, &evt);
    }
  }

  const f32 damageMult = ctx->status ? scene_status_damage(ctx->status) : 1.0f;
  const f32 damage     = def->damage * damageMult;

  EcsIterator* hitItr = ecs_view_itr(ctx->targetView);
  for (u32 i = 0; i != hitCount; ++i) {
    if (hits[i] == ctx->instigator) {
      continue; // Ignore ourselves.
    }
    if (!ecs_view_maybe_jump(hitItr, hits[i])) {
      continue; // Hit entity is no longer alive or is missing components.
    }

    // Apply damage.
    if (damage > f32_epsilon) {
      const f32 damageThisTick = def->continuous ? (damage * ctx->deltaSeconds) : damage;
      scene_health_request(
          ctx->world,
          hits[i],
          &(SceneHealthMod){
              .instigator = ctx->instigator,
              .amount     = -damageThisTick /* negate to deal damage */,
          });
    }

    // Apply status.
    if (def->applyStatusMask && ecs_world_has_t(ctx->world, hits[i], SceneStatusComp)) {
      scene_status_add_many(ctx->world, hits[i], def->applyStatusMask, ctx->instigator);
    }

    // Spawn impact.
    if (firstExecution && def->impactPrefab) {
      const GeoVector impactPoint = aim_estimate_impact_point(orgPos, hitItr);
      scene_prefab_spawn(
          ctx->world,
          &(ScenePrefabSpec){
              .flags    = ScenePrefabFlags_Volatile,
              .prefabId = def->impactPrefab,
              .faction  = SceneFaction_None,
              .position = geo_vector_lerp(impactPoint, orgPos, 0.5f),
              .rotation = geo_quat_ident,
          });
    }
  }
  if (!def->continuous || interrupt) {
    return EffectResult_Done;
  }
  return EffectResult_Running;
}

static EffectResult effect_update_anim(
    const AttackCtx*             ctx,
    const TimeDuration           effectTime,
    const u32                    effectIndex,
    const bool                   interrupt,
    const AssetWeaponEffectAnim* def) {

  if (effectTime < def->delay) {
    return interrupt ? EffectResult_Done : EffectResult_Running; // Waiting to execute.
  }

  SceneAnimLayer* animLayer = scene_animation_layer_mut(ctx->anim, def->layer);
  if (UNLIKELY(!animLayer)) {
    log_e("Weapon animation not found", log_param("entity", ecs_entity_fmt(ctx->instigator)));
    return EffectResult_Done;
  }

  if (effect_execute_once(ctx, effectIndex)) {
    if (def->continuous) {
      animLayer->flags |= SceneAnimFlags_Loop; // Loop animation.
    } else {
      animLayer->flags &= ~SceneAnimFlags_Loop; // Don't loop animation.
    }
    animLayer->flags |= SceneAnimFlags_Active;
    animLayer->time  = 0.0f; // Restart the animation.
    animLayer->speed = def->speed;
    return EffectResult_Running;
  }

  // NOTE: Make sure the animation is always active while running, important for hot-loading.
  animLayer->flags |= SceneAnimFlags_Active;

  if (interrupt) {
    animLayer->flags &= ~SceneAnimFlags_Loop; // Disable animation looping.
    if (def->allowEarlyInterrupt) {
      return EffectResult_Done;
    }
  } else if (def->continuous) {
    return EffectResult_Running;
  }
  const bool isAtEnd = animLayer->time >= animLayer->duration;
  return isAtEnd ? EffectResult_Done : EffectResult_Running;
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
    if (def->waitUntilFinished && (effectTime - def->delay) < def->duration) {
      return EffectResult_Running;
    }
    return EffectResult_Done;
  }

  const EcsEntityId inst           = ctx->instigator;
  const u32         jointOriginIdx = scene_skeleton_joint_by_name(ctx->skelTempl, def->originJoint);
  if (UNLIKELY(sentinel_check(jointOriginIdx))) {
    log_e("Weapon joint not found", log_param("entity", ecs_entity_fmt(inst)));
    return EffectResult_Done;
  }

  const EcsEntityId e = ecs_world_entity_create(ctx->world);
  ecs_world_add_empty_t(ctx->world, e, SceneLevelInstanceComp);
  ecs_world_add_t(ctx->world, e, SceneTransformComp, .position = {0}, .rotation = geo_quat_ident);
  if (math_abs(def->scale - 1.0f) > 1e-3f) {
    ecs_world_add_t(ctx->world, e, SceneScaleComp, .scale = def->scale);
  }
  ecs_world_add_t(ctx->world, e, SceneLifetimeDurationComp, .duration = def->duration);
  ecs_world_add_t(ctx->world, e, SceneVisibilityComp); // Seeing attacks requires visibility.
  ecs_world_add_t(
      ctx->world, e, SceneVfxSystemComp, .asset = def->asset, .alpha = 1.0f, .emitMultiplier = 1.0);
  scene_attach_to_joint(ctx->world, e, inst, jointOriginIdx);

  return EffectResult_Done;
}

static EffectResult effect_update_sound(
    const AttackCtx*              ctx,
    const TimeDuration            effectTime,
    const u32                     effectIndex,
    const AssetWeaponEffectSound* def) {

  if (effectTime < def->delay) {
    return EffectResult_Running; // Waiting to execute.
  }
  if (!effect_execute_once(ctx, effectIndex)) {
    return EffectResult_Done;
  }

  const EcsEntityId inst     = ctx->instigator;
  const u32         jointIdx = scene_skeleton_joint_by_name(ctx->skelTempl, def->originJoint);
  if (UNLIKELY(sentinel_check(jointIdx))) {
    log_e("Weapon joint not found", log_param("entity", ecs_entity_fmt(inst)));
    return EffectResult_Done;
  }
  const GeoMatrix mat   = scene_skeleton_joint_world(ctx->trans, ctx->scale, ctx->skel, jointIdx);
  const GeoVector pos   = geo_matrix_to_translation(&mat);
  const f32       gain  = rng_sample_range(g_rng, def->gainMin, def->gainMax);
  const f32       pitch = rng_sample_range(g_rng, def->pitchMin, def->pitchMax);

  const EcsEntityId e = ecs_world_entity_create(ctx->world);
  ecs_world_add_empty_t(ctx->world, e, SceneLevelInstanceComp);
  ecs_world_add_t(ctx->world, e, SceneTransformComp, .position = pos, .rotation = geo_quat_ident);
  ecs_world_add_t(ctx->world, e, SceneLifetimeDurationComp, .duration = def->duration);
  ecs_world_add_t(ctx->world, e, SceneSoundComp, .asset = def->asset, .gain = gain, .pitch = pitch);
  ecs_world_add_t(ctx->world, e, SceneVisibilityComp); // Hearing attacks requires visibility.

  return EffectResult_Done;
}

static EffectResult
effect_update(const AttackCtx* ctx, const TimeDuration effectTime, const bool interrupt) {
  diag_assert(ctx->weapon->effectCount <= sizeof(ctx->attack->executedEffects) * 8);

  EffectResult result = EffectResult_Done;
  for (u16 i = 0; i != ctx->weapon->effectCount; ++i) {
    const AssetWeaponEffect* effect = &ctx->weaponMap->effects.values[ctx->weapon->effectIndex + i];
    switch (effect->type) {
    case AssetWeaponEffect_Projectile:
      result |= effect_update_proj(ctx, effectTime, i, &effect->data_proj);
      break;
    case AssetWeaponEffect_Damage:
      result |= effect_update_dmg(ctx, effectTime, i, interrupt, &effect->data_dmg);
      break;
    case AssetWeaponEffect_Animation:
      result |= effect_update_anim(ctx, effectTime, i, interrupt, &effect->data_anim);
      break;
    case AssetWeaponEffect_Vfx:
      result |= effect_update_vfx(ctx, effectTime, i, &effect->data_vfx);
      break;
    case AssetWeaponEffect_Sound:
      result |= effect_update_sound(ctx, effectTime, i, &effect->data_sound);
      break;
    }
  }
  return result;
}

ecs_view_define(AttackView) {
  ecs_access_maybe_read(SceneFactionComp);
  ecs_access_maybe_read(SceneScaleComp);
  ecs_access_maybe_read(SceneStatusComp);
  ecs_access_maybe_write(SceneAttackAimComp);
  ecs_access_maybe_write(SceneAttackTraceComp);
  ecs_access_maybe_write(SceneLocomotionComp);
  ecs_access_read(SceneRenderableComp);
  ecs_access_read(SceneTransformComp);
  ecs_access_without(SceneDeadComp);
  ecs_access_write(SceneAnimationComp);
  ecs_access_write(SceneAttackComp);
  ecs_access_write(SceneSkeletonComp);
}

ecs_view_define(TargetView) {
  ecs_access_maybe_read(SceneLocationComp);
  ecs_access_maybe_read(SceneScaleComp);
  ecs_access_maybe_read(SceneVelocityComp);
  ecs_access_read(SceneCollisionComp);
  ecs_access_read(SceneTransformComp);
  ecs_access_with(SceneHealthComp);
  ecs_access_without(SceneDeadComp);
}

ecs_system_define(SceneAttackSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneCollisionEnvComp* collisionEnv = ecs_view_read_t(globalItr, SceneCollisionEnvComp);
  const SceneTimeComp*         time         = ecs_view_read_t(globalItr, SceneTimeComp);
  const f32                    deltaSec     = scene_delta_seconds(time);

  EcsView*                  weaponMapView = ecs_world_view_t(world, WeaponMapView);
  const AssetWeaponMapComp* weaponMap     = attack_weapon_map_get(globalItr, weaponMapView);
  if (!weaponMap) {
    return; // Weapon-map not loaded yet.
  }

  EcsView*     targetView = ecs_world_view_t(world, TargetView);
  EcsIterator* targetItr  = ecs_view_itr(targetView);
  EcsIterator* graphicItr = ecs_view_itr(ecs_world_view_t(world, GraphicView));

  EcsView* attackView = ecs_world_view_t(world, AttackView);
  for (EcsIterator* itr = ecs_view_itr_step(attackView, parCount, parIndex); ecs_view_walk(itr);) {
    const EcsEntityId          entity     = ecs_view_entity(itr);
    const SceneFactionComp*    faction    = ecs_view_read_t(itr, SceneFactionComp);
    const SceneRenderableComp* renderable = ecs_view_read_t(itr, SceneRenderableComp);
    const SceneScaleComp*      scale      = ecs_view_read_t(itr, SceneScaleComp);
    const SceneTransformComp*  trans      = ecs_view_read_t(itr, SceneTransformComp);
    const SceneStatusComp*     status     = ecs_view_read_t(itr, SceneStatusComp);
    SceneAnimationComp*        anim       = ecs_view_write_t(itr, SceneAnimationComp);
    SceneAttackAimComp*        attackAim  = ecs_view_write_t(itr, SceneAttackAimComp);
    SceneAttackComp*           attack     = ecs_view_write_t(itr, SceneAttackComp);
    SceneAttackTraceComp*      trace      = ecs_view_write_t(itr, SceneAttackTraceComp);
    SceneLocomotionComp*       loco       = ecs_view_write_t(itr, SceneLocomotionComp);
    SceneSkeletonComp*         skel       = ecs_view_write_t(itr, SceneSkeletonComp);

    if ((attack->flags & SceneAttackFlags_Trace) && !trace) {
      attack_trace_start(world, entity);
    } else if (trace && !(attack->flags & SceneAttackFlags_Trace)) {
      attack_trace_stop(world, entity);
    }
    if (trace) {
      attack_trace_prune_expired(trace, time->time);
    }

    if (!ecs_view_maybe_jump(graphicItr, renderable->graphic)) {
      continue; // Graphic is missing required components.
    }
    const SceneSkeletonTemplComp* skelTempl = ecs_view_read_t(graphicItr, SceneSkeletonTemplComp);

    if (UNLIKELY(!attack->weaponName)) {
      continue; // Entity has no weapon equipped.
    }
    const AssetWeapon* weapon = asset_weapon_get(weaponMap, attack->weaponName);
    if (UNLIKELY(!weapon)) {
      log_e("Weapon not found", log_param("entity", ecs_entity_fmt(entity)));
      continue;
    }

    const TimeDuration timeSinceHadTarget = time->time - attack->lastHasTargetTime;
    const bool         isMoving           = loco && (loco->flags & SceneLocomotion_Moving) != 0;
    const bool         allowReady         = weapon->readyWhileMoving || !isMoving;

    bool weaponReady = false;
    attack->flags &= ~SceneAttackFlags_Readying;
    if (allowReady && (attack->targetCurrent || timeSinceHadTarget < weapon->readyMinTime)) {
      if (!(weaponReady = math_towards_f32(&attack->readyNorm, 1, weapon->readySpeed * deltaSec))) {
        attack->flags |= SceneAttackFlags_Readying;
      }
    } else {
      if (!math_towards_f32(&attack->readyNorm, 0, weapon->readySpeed * deltaSec)) {
        attack->flags |= SceneAttackFlags_Readying;
      }
    }

    if (attackAim && isMoving && timeSinceHadTarget > attack_aim_reset_time) {
      scene_attack_aim_reset(attackAim);
    }

    if (weapon->readyAnim) {
      scene_animation_set_weight(anim, weapon->readyAnim, attack->readyNorm);
    }

    // Change target if currently not attacking.
    bool interruptFiring = false;
    if (attack->flags & SceneAttackFlags_Firing) {
      interruptFiring = attack->targetCurrent != attack->targetDesired;
    } else {
      attack->targetCurrent = attack->targetDesired;
    }

    // Aim at target and potentially start a new attack.
    if (ecs_view_maybe_jump(targetItr, attack->targetCurrent)) {
      attack->lastHasTargetTime = time->time;

      const f32    distEst       = aim_estimate_distance(trans->position, targetItr);
      TimeDuration impactTimeEst = 0;
      if (weapon->flags & AssetWeapon_PredictiveAim) {
        impactTimeEst = weapon_estimate_impact_time(weaponMap, weapon, distEst);
      }
      const GeoVector targetPos = aim_position(trans->position, targetItr, impactTimeEst);
      aim_face(attackAim, loco, trans, targetPos);

      const bool      isCoolingDown = time->time < attack->nextFireTime;
      const GeoVector pos           = trans->position;
      const GeoQuat   aimRot        = scene_attack_aim_rot(trans, attackAim);
      const bool canFire = weaponReady && !isCoolingDown && attack_in_sight(pos, aimRot, targetPos);

      if (!(attack->flags & SceneAttackFlags_Firing) && canFire) {
        // Start the attack.
        attack->lastFireTime = time->time;
        attack->flags |= SceneAttackFlags_Firing;
        attack->executedEffects = 0;
        attack->targetPos       = targetPos;
      } else {
        interruptFiring = !canFire;
      }
    } else {
      interruptFiring = true;
      if (attack->targetDesired == attack->targetCurrent) {
        attack->targetDesired = 0;
      }
      attack->targetCurrent = 0;
    }

    // Update the current attack.
    if (attack->flags & SceneAttackFlags_Firing) {
      const AttackCtx ctx = {
          .world        = world,
          .targetView   = targetView,
          .instigator   = entity,
          .collisionEnv = collisionEnv,
          .weaponMap    = weaponMap,
          .weapon       = weapon,
          .trans        = trans,
          .scale        = scale,
          .skel         = skel,
          .skelTempl    = skelTempl,
          .status       = status,
          .attack       = attack,
          .trace        = trace,
          .anim         = anim,
          .factionId    = LIKELY(faction) ? faction->id : SceneFaction_None,
          .time         = time->time,
          .deltaSeconds = deltaSec,
      };
      const TimeDuration effectTime = time->time - attack->lastFireTime;
      if (effect_update(&ctx, effectTime, interruptFiring) == EffectResult_Done) {
        // Finish the attack.
        attack->flags &= ~SceneAttackFlags_Firing;
        attack->nextFireTime = attack_next_fire_time(weapon, time->time);
      }
    }
  }
}

ecs_view_define(AimUpdateView) {
  ecs_access_read(SceneRenderableComp);
  ecs_access_write(SceneSkeletonComp);
  ecs_access_write(SceneAttackAimComp);
}

ecs_system_define(SceneAttackAimSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const f32 dt = scene_delta_seconds(ecs_view_read_t(globalItr, SceneTimeComp));

  EcsIterator* graphicItr = ecs_view_itr(ecs_world_view_t(world, GraphicView));

  EcsView* updateView = ecs_world_view_t(world, AimUpdateView);
  for (EcsIterator* itr = ecs_view_itr(updateView); ecs_view_walk(itr);) {
    const SceneRenderableComp* renderable = ecs_view_read_t(itr, SceneRenderableComp);
    SceneAttackAimComp*        attackAim  = ecs_view_write_t(itr, SceneAttackAimComp);
    SceneSkeletonComp*         skel       = ecs_view_write_t(itr, SceneSkeletonComp);

    if (!ecs_view_maybe_jump(graphicItr, renderable->graphic)) {
      continue; // Graphic is missing required components.
    }
    const SceneSkeletonTemplComp* skelTempl = ecs_view_read_t(graphicItr, SceneSkeletonTemplComp);

    attackAim->isAiming = !geo_quat_towards(
        &attackAim->aimLocalActual, attackAim->aimLocalTarget, attackAim->aimSpeedRad * dt);

    const u32 aimJointIdx = scene_skeleton_joint_by_name(skelTempl, attackAim->aimJoint);
    if (!sentinel_check(aimJointIdx)) {
      const GeoMatrix postTransform = geo_matrix_from_quat(attackAim->aimLocalActual);
      scene_skeleton_post_transform(skel, aimJointIdx, &postTransform);
    }
  }
}

ecs_module_init(scene_attack_module) {
  ecs_register_comp(SceneAttackComp);
  ecs_register_comp(SceneAttackAimComp);
  ecs_register_comp(SceneAttackTraceComp, .destructor = ecs_destruct_attack_trace);

  ecs_register_view(GlobalView);
  ecs_register_view(WeaponMapView);
  ecs_register_view(GraphicView);
  ecs_register_view(AttackView);
  ecs_register_view(TargetView);
  ecs_register_view(AimUpdateView);

  ecs_register_system(
      SceneAttackSys,
      ecs_view_id(GlobalView),
      ecs_view_id(WeaponMapView),
      ecs_view_id(GraphicView),
      ecs_view_id(AttackView),
      ecs_view_id(TargetView));
  ecs_parallel(SceneAttackSys, g_jobsWorkerCount);

  ecs_register_system(
      SceneAttackAimSys,
      ecs_view_id(GlobalView),
      ecs_view_id(AimUpdateView),
      ecs_view_id(GraphicView));
}

GeoQuat scene_attack_aim_rot(const SceneTransformComp* trans, const SceneAttackAimComp* aimComp) {
  if (aimComp) {
    return geo_quat_mul(trans->rotation, aimComp->aimLocalActual);
  }
  return trans->rotation;
}

GeoVector scene_attack_aim_dir(const SceneTransformComp* trans, const SceneAttackAimComp* aimComp) {
  const GeoQuat aimRot = scene_attack_aim_rot(trans, aimComp);
  return geo_quat_rotate(aimRot, geo_forward);
}

void scene_attack_aim(
    SceneAttackAimComp* attackAim, const SceneTransformComp* trans, const GeoVector dir) {
  diag_assert_msg(
      math_abs(geo_vector_mag_sqr(dir) - 1.0f) <= 1e-6f,
      "Direction ({}) is not normalized",
      geo_vector_fmt(dir));

  const GeoQuat aimWorld            = geo_quat_look(dir, geo_up);
  const GeoQuat aimLocal            = geo_quat_from_to(trans->rotation, aimWorld);
  const GeoQuat aimLocalConstrained = geo_quat_to_twist(aimLocal, geo_up);
  attackAim->aimLocalTarget         = aimLocalConstrained;
}

void scene_attack_aim_reset(SceneAttackAimComp* attackAim) {
  attackAim->aimLocalTarget = geo_quat_ident;
}

const SceneAttackEvent* scene_attack_trace_begin(const SceneAttackTraceComp* trace) {
  return dynarray_begin_t(&trace->events, SceneAttackEvent);
}

const SceneAttackEvent* scene_attack_trace_end(const SceneAttackTraceComp* trace) {
  return dynarray_end_t(&trace->events, SceneAttackEvent);
}
