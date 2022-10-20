#include "core_diag.h"
#include "core_math.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "scene_health.h"
#include "scene_lifetime.h"
#include "scene_renderable.h"
#include "scene_skeleton.h"
#include "scene_tag.h"
#include "scene_time.h"

static StringHash g_healthHitAnimHash, g_healthDeathAnimHash;

ecs_comp_define_public(SceneHealthComp);
ecs_comp_define_public(SceneDamageComp);
ecs_comp_define(SceneHealthAnimComp) { SceneSkeletonMask hitAnimMask; };

static void ecs_combine_damage(void* dataA, void* dataB) {
  SceneDamageComp* dmgA = dataA;
  SceneDamageComp* dmgB = dataB;
  dmgA->amount += dmgB->amount;
}

ecs_view_define(HealthAnimInitView) {
  ecs_access_read(SceneRenderableComp);
  ecs_access_with(SceneAnimationComp);
  ecs_access_with(SceneHealthComp);
  ecs_access_without(SceneHealthAnimComp);
}

ecs_view_define(HealthGraphicView) { ecs_access_read(SceneSkeletonTemplComp); }

ecs_system_define(SceneHealthInitSys) {
  EcsIterator* graphicItr = ecs_view_itr(ecs_world_view_t(world, HealthGraphicView));

  EcsView* initView = ecs_world_view_t(world, HealthAnimInitView);
  for (EcsIterator* itr = ecs_view_itr(initView); ecs_view_walk(itr);) {
    const EcsEntityId          entity     = ecs_view_entity(itr);
    const SceneRenderableComp* renderable = ecs_view_read_t(itr, SceneRenderableComp);

    if (ecs_view_maybe_jump(graphicItr, renderable->graphic)) {
      const SceneSkeletonTemplComp* skelTempl = ecs_view_read_t(graphicItr, SceneSkeletonTemplComp);
      SceneHealthAnimComp*          animComp  = ecs_world_add_t(world, entity, SceneHealthAnimComp);

#define ENABLE_JOINT(_MASK_, _NAME_)                                                               \
  scene_skeleton_mask_set(_MASK_, scene_skeleton_joint_by_name(skelTempl, string_hash_lit(_NAME_)))

      /**
       * TODO: Define this skeleton mask in content instead of hard-coding it here.
       */
      scene_skeleton_mask_clear_all(&animComp->hitAnimMask);
      ENABLE_JOINT(&animComp->hitAnimMask, "Spine2");
      ENABLE_JOINT(&animComp->hitAnimMask, "Neck");
      ENABLE_JOINT(&animComp->hitAnimMask, "Neck1");
      ENABLE_JOINT(&animComp->hitAnimMask, "Head");

#undef ENABLE_JOINT
    }
  }
}

static f32 health_normalize(const SceneHealthComp* health, const f32 amount) {
  return LIKELY(health->max > 0.0f) ? (amount / health->max) : 1.0f;
}

static void health_set_damaged(EcsWorld* world, const EcsEntityId entity, SceneTagComp* tagComp) {
  if (tagComp) {
    tagComp->tags |= SceneTags_Damaged;
  } else {
    scene_tag_add(world, entity, SceneTags_Default | SceneTags_Damaged);
  }
}

static void health_clear_damaged(EcsWorld* world, const EcsEntityId entity, SceneTagComp* tagComp) {
  (void)world;
  (void)entity;
  if (tagComp) {
    tagComp->tags &= ~SceneTags_Damaged;
  }
}

static void health_anim_play_hit(SceneAnimationComp* anim, const SceneHealthAnimComp* healthAnim) {
  SceneAnimLayer* hitAnimLayer;
  if ((hitAnimLayer = scene_animation_layer(anim, g_healthHitAnimHash))) {

    if (hitAnimLayer->weight > 0) {
      return; // Don't restart the animation if its already playing.
    }
    hitAnimLayer->time  = 0;
    hitAnimLayer->speed = 1.5f;
    hitAnimLayer->flags &= ~SceneAnimFlags_Loop;
    hitAnimLayer->flags |= SceneAnimFlags_AutoFade;
    hitAnimLayer->mask = healthAnim->hitAnimMask;
  }
}

static void health_anim_play_death(SceneAnimationComp* anim) {
  SceneAnimLayer* deathAnimLayer;
  if ((deathAnimLayer = scene_animation_layer(anim, g_healthDeathAnimHash))) {
    deathAnimLayer->time  = 0;
    deathAnimLayer->speed = 1.5f; // TODO: Speed should be defined in content.
    deathAnimLayer->flags &= ~SceneAnimFlags_Loop;
    deathAnimLayer->flags |= SceneAnimFlags_AutoFadeIn;
  }
}

/**
 * Remove various components on death.
 * TODO: Find another way to handle this, health should't know about all these components.
 */
ecs_comp_extern(SceneAttackComp);
ecs_comp_extern(SceneBrainComp);
ecs_comp_extern(SceneCollisionComp);
ecs_comp_extern(SceneLocomotionComp);
ecs_comp_extern(SceneNavAgentComp);
ecs_comp_extern(SceneNavPathComp);
ecs_comp_extern(SceneTargetFinderComp);

static void health_death_disable(EcsWorld* world, const EcsEntityId entity) {
  ecs_utils_maybe_remove_t(world, entity, SceneAttackComp);
  ecs_utils_maybe_remove_t(world, entity, SceneBrainComp);
  ecs_utils_maybe_remove_t(world, entity, SceneCollisionComp);
  ecs_utils_maybe_remove_t(world, entity, SceneLocomotionComp);
  ecs_utils_maybe_remove_t(world, entity, SceneNavAgentComp);
  ecs_utils_maybe_remove_t(world, entity, SceneNavPathComp);
  ecs_utils_maybe_remove_t(world, entity, SceneTargetFinderComp);
}

ecs_view_define(GlobalView) { ecs_access_read(SceneTimeComp); }

ecs_view_define(HealthView) {
  ecs_access_write(SceneDamageComp);
  ecs_access_maybe_read(SceneHealthAnimComp);
  ecs_access_maybe_write(SceneAnimationComp);
  ecs_access_maybe_write(SceneTagComp);
  ecs_access_write(SceneHealthComp);
}

ecs_system_define(SceneHealthUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneTimeComp* time = ecs_view_read_t(globalItr, SceneTimeComp);

  EcsView* healthView = ecs_world_view_t(world, HealthView);
  for (EcsIterator* itr = ecs_view_itr(healthView); ecs_view_walk(itr);) {
    const EcsEntityId          entity     = ecs_view_entity(itr);
    SceneHealthComp*           health     = ecs_view_write_t(itr, SceneHealthComp);
    SceneDamageComp*           damage     = ecs_view_write_t(itr, SceneDamageComp);
    SceneTagComp*              tag        = ecs_view_write_t(itr, SceneTagComp);
    SceneAnimationComp*        anim       = ecs_view_write_t(itr, SceneAnimationComp);
    const SceneHealthAnimComp* healthAnim = ecs_view_read_t(itr, SceneHealthAnimComp);

    const f32 damageNorm = health_normalize(health, damage->amount);
    damage->amount       = 0;

    if (damageNorm > 0.0f) {
      damage->lastDamagedTime = time->time;
      health_set_damaged(world, entity, tag);
      if (anim && healthAnim) {
        health_anim_play_hit(anim, healthAnim);
      }
    } else if ((time->time - damage->lastDamagedTime) > time_milliseconds(100)) {
      health_clear_damaged(world, entity, tag);
    }

    const bool isDead = (health->flags & SceneHealthFlags_Dead) != 0;
    if (isDead) {
      continue;
    }

    health->norm -= damageNorm;
    if (health->norm <= 0.0f) {
      health->flags |= SceneHealthFlags_Dead;
      health->norm = 0.0f;

      health_death_disable(world, entity);
      if (anim && healthAnim) {
        health_anim_play_death(anim);
      }

      ecs_world_add_t(world, entity, SceneLifetimeDurationComp, .duration = time_seconds(5));
    }
  }
}

ecs_module_init(scene_health_module) {
  g_healthHitAnimHash   = string_hash_lit("hit");
  g_healthDeathAnimHash = string_hash_lit("death");

  ecs_register_comp(SceneHealthComp);
  ecs_register_comp(SceneDamageComp, .combinator = ecs_combine_damage);
  ecs_register_comp(SceneHealthAnimComp);

  ecs_register_view(GlobalView);

  ecs_register_system(
      SceneHealthInitSys,
      ecs_register_view(HealthAnimInitView),
      ecs_register_view(HealthGraphicView));

  ecs_register_system(SceneHealthUpdateSys, ecs_view_id(GlobalView), ecs_register_view(HealthView));
}

void scene_health_damage(EcsWorld* world, const EcsEntityId target, const f32 amount) {
  diag_assert(amount >= 0.0f);
  ecs_world_add_t(world, target, SceneDamageComp, .amount = amount);
}
