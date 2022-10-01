#include "core_diag.h"
#include "core_math.h"
#include "ecs_world.h"
#include "scene_health.h"
#include "scene_renderable.h"
#include "scene_skeleton.h"
#include "scene_tag.h"
#include "scene_time.h"

static StringHash g_healthHitAnimHash;

ecs_comp_define_public(SceneHealthComp);
ecs_comp_define(SceneHealthAnimComp) { SceneSkeletonMask hitAnimMask; };

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
      ENABLE_JOINT(&animComp->hitAnimMask, "Spine");
      ENABLE_JOINT(&animComp->hitAnimMask, "Spine1");
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
    hitAnimLayer->time = 0;
    hitAnimLayer->flags &= ~SceneAnimFlags_Loop;
    hitAnimLayer->flags |= SceneAnimFlags_AutoFade;
    hitAnimLayer->mask = healthAnim->hitAnimMask;
  }
}

ecs_view_define(GlobalView) { ecs_access_read(SceneTimeComp); }

ecs_view_define(HealthView) {
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
    SceneTagComp*              tag        = ecs_view_write_t(itr, SceneTagComp);
    SceneAnimationComp*        anim       = ecs_view_write_t(itr, SceneAnimationComp);
    const SceneHealthAnimComp* healthAnim = ecs_view_read_t(itr, SceneHealthAnimComp);

    const f32 damageNorm = health_normalize(health, health->damage);
    health->damage       = 0;

    if (damageNorm > 0.0f) {
      health->lastDamagedTime = time->time;
      health_set_damaged(world, entity, tag);
      if (anim && healthAnim) {
        health_anim_play_hit(anim, healthAnim);
      }
    } else if ((time->time - health->lastDamagedTime) > time_milliseconds(100)) {
      health_clear_damaged(world, entity, tag);
    }

    health->norm -= damageNorm;
    if (health->norm <= 0.0f) {
      health->norm = 0.0f;
      ecs_world_entity_destroy(world, ecs_view_entity(itr));
    }
  }
}

ecs_module_init(scene_health_module) {
  g_healthHitAnimHash = string_hash_lit("hit");

  ecs_register_comp(SceneHealthComp);
  ecs_register_comp(SceneHealthAnimComp);

  ecs_register_view(GlobalView);

  ecs_register_system(
      SceneHealthInitSys,
      ecs_register_view(HealthAnimInitView),
      ecs_register_view(HealthGraphicView));

  ecs_register_system(SceneHealthUpdateSys, ecs_view_id(GlobalView), ecs_register_view(HealthView));
}

void scene_health_damage(SceneHealthComp* health, const f32 amount) {
  diag_assert(amount >= 0.0f);
  health->damage += amount;
}
