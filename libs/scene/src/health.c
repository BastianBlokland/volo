#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_float.h"
#include "core_math.h"
#include "core_rng.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "scene_bark.h"
#include "scene_health.h"
#include "scene_lifetime.h"
#include "scene_prefab.h"
#include "scene_renderable.h"
#include "scene_skeleton.h"
#include "scene_tag.h"
#include "scene_time.h"
#include "scene_transform.h"
#include "scene_vfx.h"

#define health_anim_min_norm_dmg 0.025f
#define health_anim_speed_min 0.8f
#define health_anim_speed_max 1.2f

static StringHash g_healthHitAnimHash, g_healthDeathAnimHash;

ecs_comp_define_public(SceneHealthComp);
ecs_comp_define_public(SceneHealthRequestComp);
ecs_comp_define_public(SceneHealthStatsComp);
ecs_comp_define_public(SceneDeadComp);

static SceneHealthMod* mod_storage_push(SceneHealthModStorage* storage) {
  if (UNLIKELY(storage->count == storage->capacity)) {
    const u32       newCapacity = storage->capacity ? bits_nextpow2(storage->capacity + 1) : 4;
    SceneHealthMod* newValues   = alloc_array_t(g_allocHeap, SceneHealthMod, newCapacity);
    if (UNLIKELY(!newValues)) {
      diag_crash_msg("damage_storage_push(): Allocation failed");
    }
    if (storage->capacity) {
      mem_cpy(
          mem_from_to(newValues, newValues + storage->count),
          mem_from_to(storage->values, storage->values + storage->count));
      alloc_free_array_t(g_allocHeap, storage->values, storage->capacity);
    }
    storage->values   = newValues;
    storage->capacity = newCapacity;
  }
  diag_assert(storage->count < storage->capacity);
  return &storage->values[storage->count++];
}

static void mod_storage_clear(SceneHealthModStorage* storage) { storage->count = 0; }

static void mod_storage_destroy(SceneHealthModStorage* storage) {
  if (storage->capacity) {
    alloc_free_array_t(g_allocHeap, storage->values, storage->capacity);
  }
}

static void ecs_combine_request(void* dataA, void* dataB) {
  SceneHealthRequestComp* dmgA = dataA;
  SceneHealthRequestComp* dmgB = dataB;

  diag_assert_msg(!dmgA->singleRequest, "Existing health-request cannot be a single-request");
  diag_assert_msg(dmgB->singleRequest, "Incoming health-request has be a single-request");

  *mod_storage_push(&dmgA->storage) = dmgB->request;
}

static void ecs_destruct_request(void* data) {
  SceneHealthRequestComp* comp = data;
  if (!comp->singleRequest) {
    mod_storage_destroy(&comp->storage);
  }
}

static void ecs_combine_stats(void* dataA, void* dataB) {
  SceneHealthStatsComp* statsA = dataA;
  SceneHealthStatsComp* statsB = dataB;

  for (SceneHealthStat stat = 0; stat != SceneHealthStat_Count; ++stat) {
    statsA->values[stat] += statsB->values[stat];
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

static void health_anim_play_hit(SceneAnimationComp* anim) {
  SceneAnimLayer* hitAnimLayer;
  if ((hitAnimLayer = scene_animation_layer_mut(anim, g_healthHitAnimHash))) {
    // Restart the animation if it has reached the end but don't rewind if its already playing.
    if (hitAnimLayer->time == hitAnimLayer->duration) {
      hitAnimLayer->flags |= SceneAnimFlags_Active;
      hitAnimLayer->time  = 0;
      hitAnimLayer->speed = rng_sample_range(g_rng, health_anim_speed_min, health_anim_speed_max);
    }
  }
}

static void health_anim_play_death(SceneAnimationComp* anim) {
  SceneAnimLayer* deathAnimLayer;
  if ((deathAnimLayer = scene_animation_layer_mut(anim, g_healthDeathAnimHash))) {
    deathAnimLayer->flags |= SceneAnimFlags_Active;
  }
}

typedef struct {
  SceneHealthComp* health;
  EcsIterator*     statsItr;
  f32              totalDamage, totalHealing; // Normalized.
} HealthModContext;

static void mod_apply_damage(HealthModContext* ctx, const SceneHealthMod* mod) {
  diag_assert(mod->amount < 0.0f);

  const f32 amountNorm = math_min(health_normalize(ctx->health, -mod->amount), ctx->health->norm);
  ctx->health->norm -= amountNorm;
  ctx->totalDamage += amountNorm;

  // Track damage stats for the instigator.
  if (amountNorm > f32_epsilon && ecs_view_maybe_jump(ctx->statsItr, mod->instigator)) {
    SceneHealthStatsComp* statsComp = ecs_view_write_t(ctx->statsItr, SceneHealthStatsComp);
    statsComp->values[SceneHealthStat_DealtDamage] += amountNorm * ctx->health->max;
    if (ctx->health->norm < f32_epsilon && (ctx->health->flags & SceneHealthFlags_Dead) == 0) {
      statsComp->values[SceneHealthStat_Kills] += 1.0f;
    }
  }

  // Check for death.
  if (ctx->health->norm < f32_epsilon) {
    ctx->health->norm = 0.0f;
    ctx->health->flags |= SceneHealthFlags_Dead;
  }
}

static void mod_apply_healing(HealthModContext* ctx, const SceneHealthMod* mod) {
  diag_assert(mod->amount > 0.0f);

  if (ctx->health->flags & SceneHealthFlags_Dead) {
    return; // No resurrecting.
  }

  const f32 maxToHealNorm = 1.0f - ctx->health->norm;
  const f32 amountNorm    = math_min(health_normalize(ctx->health, mod->amount), maxToHealNorm);
  ctx->health->norm += amountNorm;
  ctx->totalHealing += amountNorm;

  // Track healing stats for the instigator.
  if (amountNorm > f32_epsilon && ecs_view_maybe_jump(ctx->statsItr, mod->instigator)) {
    SceneHealthStatsComp* statsComp = ecs_view_write_t(ctx->statsItr, SceneHealthStatsComp);
    statsComp->values[SceneHealthStat_DealtHealing] += amountNorm * ctx->health->max;
  }

  // Check for fully restored.
  if (ctx->health->norm > 0.9999f) {
    ctx->health->norm = 1.0f;
  }
}

/**
 * Remove various components on death.
 * TODO: Find another way to handle this, health should't know about all these components.
 */
ecs_comp_extern(SceneCollisionComp);
ecs_comp_extern(SceneLocomotionComp);
ecs_comp_extern(SceneNavAgentComp);
ecs_comp_extern(SceneNavPathComp);
ecs_comp_extern(SceneTargetFinderComp);

static void health_death_disable(EcsWorld* world, const EcsEntityId entity) {
  ecs_utils_maybe_remove_t(world, entity, SceneCollisionComp);
  ecs_utils_maybe_remove_t(world, entity, SceneLocomotionComp);
  ecs_utils_maybe_remove_t(world, entity, SceneNavAgentComp);
  ecs_utils_maybe_remove_t(world, entity, SceneNavPathComp);
  ecs_utils_maybe_remove_t(world, entity, SceneTargetFinderComp);
}

ecs_view_define(GlobalView) { ecs_access_read(SceneTimeComp); }

ecs_view_define(HealthView) {
  ecs_access_maybe_read(SceneTransformComp);
  ecs_access_maybe_write(SceneAnimationComp);
  ecs_access_maybe_write(SceneBarkComp);
  ecs_access_maybe_write(SceneTagComp);
  ecs_access_write(SceneHealthComp);
  ecs_access_write(SceneHealthRequestComp);
}

ecs_view_define(HealthStatsView) { ecs_access_write(SceneHealthStatsComp); }

ecs_system_define(SceneHealthUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneTimeComp* time = ecs_view_read_t(globalItr, SceneTimeComp);

  EcsView* healthView = ecs_world_view_t(world, HealthView);
  EcsView* statsView  = ecs_world_view_t(world, HealthStatsView);

  EcsIterator* statsItr = ecs_view_itr(statsView);

  for (EcsIterator* itr = ecs_view_itr(healthView); ecs_view_walk(itr);) {
    const EcsEntityId         entity  = ecs_view_entity(itr);
    const SceneTransformComp* trans   = ecs_view_read_t(itr, SceneTransformComp);
    SceneAnimationComp*       anim    = ecs_view_write_t(itr, SceneAnimationComp);
    SceneHealthRequestComp*   request = ecs_view_write_t(itr, SceneHealthRequestComp);
    SceneHealthComp*          health  = ecs_view_write_t(itr, SceneHealthComp);
    SceneTagComp*             tag     = ecs_view_write_t(itr, SceneTagComp);
    SceneBarkComp*            bark    = ecs_view_write_t(itr, SceneBarkComp);

    const bool       wasDead = (health->flags & SceneHealthFlags_Dead) != 0;
    HealthModContext modCtx  = {.health = health, .statsItr = statsItr};

    // Process requests.
    diag_assert_msg(!request->singleRequest, "Health requests have to be combined");
    for (u32 i = 0; i != request->storage.count; ++i) {
      const SceneHealthMod* mod = &request->storage.values[i];
      if (mod->amount < 0.0f) {
        mod_apply_damage(&modCtx, mod);
      } else if (mod->amount > 0.0f) {
        mod_apply_healing(&modCtx, mod);
      }
    }
    mod_storage_clear(&request->storage);

    // Activate damage effects when we received damage.
    if (modCtx.totalDamage > 0.0f && (health->flags & SceneHealthFlags_Dead) == 0) {
      health->lastDamagedTime = time->time;
      health_set_damaged(world, entity, tag);
      if (anim && modCtx.totalDamage > health_anim_min_norm_dmg) {
        health_anim_play_hit(anim);
      }
    } else if ((time->time - health->lastDamagedTime) > time_milliseconds(100)) {
      health_clear_damaged(world, entity, tag);
    }

    // Handle entity death.
    if (!wasDead && health->norm <= f32_epsilon) {
      health->flags |= SceneHealthFlags_Dead;
      health->norm = 0.0f;

      health_death_disable(world, entity);
      if (anim) {
        health_anim_play_death(anim);
      }
      if (trans && health->deathEffectPrefab) {
        scene_prefab_spawn(
            world,
            &(ScenePrefabSpec){
                .flags    = ScenePrefabFlags_Volatile,
                .prefabId = health->deathEffectPrefab,
                .faction  = SceneFaction_None,
                .position = trans->position,
                .rotation = geo_quat_ident});
      }
      if (bark) {
        scene_bark_request(bark, SceneBarkType_Death);
      }
      ecs_world_add_empty_t(world, entity, SceneDeadComp);
      ecs_world_add_t(
          world, entity, SceneLifetimeDurationComp, .duration = health->deathDestroyDelay);
      ecs_world_add_t(
          world, entity, SceneRenderableFadeoutComp, .duration = time_milliseconds(500));
    }
  }
}

ecs_module_init(scene_health_module) {
  g_healthHitAnimHash   = string_hash_lit("hit");
  g_healthDeathAnimHash = string_hash_lit("death");

  ecs_register_comp(SceneHealthComp);
  ecs_register_comp(
      SceneHealthRequestComp,
      .combinator = ecs_combine_request,
      .destructor = ecs_destruct_request);
  ecs_register_comp(SceneHealthStatsComp, .combinator = ecs_combine_stats);
  ecs_register_comp_empty(SceneDeadComp);

  ecs_register_view(GlobalView);

  ecs_register_system(
      SceneHealthUpdateSys,
      ecs_view_id(GlobalView),
      ecs_register_view(HealthView),
      ecs_register_view(HealthStatsView));
}

String scene_health_stat_name(const SceneHealthStat stat) {
  diag_assert(stat < SceneHealthStat_Count);
  static const String g_names[] = {
      string_static("DealtDamage"),
      string_static("DealtHealing"),
      string_static("Kills"),
  };
  ASSERT(array_elems(g_names) == SceneHealthStat_Count, "Incorrect number of names");
  return g_names[stat];
}

f32 scene_health_points(const SceneHealthComp* health) { return health->max * health->norm; }

void scene_health_request_add(SceneHealthRequestComp* comp, const SceneHealthMod* mod) {
  diag_assert_msg(!comp->singleRequest, "SceneHealthRequestComp needs a storage");
  *mod_storage_push(&comp->storage) = *mod;
}

void scene_health_request(EcsWorld* world, const EcsEntityId target, const SceneHealthMod* mod) {
  ecs_world_add_t(world, target, SceneHealthRequestComp, .request = *mod, .singleRequest = true);
}
