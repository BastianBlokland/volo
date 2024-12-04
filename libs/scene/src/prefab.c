#include "asset_manager.h"
#include "asset_prefab.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "core_float.h"
#include "core_rng.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "scene_attachment.h"
#include "scene_attack.h"
#include "scene_bark.h"
#include "scene_collision.h"
#include "scene_debug.h"
#include "scene_footstep.h"
#include "scene_health.h"
#include "scene_knowledge.h"
#include "scene_level.h"
#include "scene_lifetime.h"
#include "scene_light.h"
#include "scene_location.h"
#include "scene_locomotion.h"
#include "scene_name.h"
#include "scene_nav.h"
#include "scene_prefab.h"
#include "scene_product.h"
#include "scene_renderable.h"
#include "scene_script.h"
#include "scene_set.h"
#include "scene_sound.h"
#include "scene_status.h"
#include "scene_tag.h"
#include "scene_target.h"
#include "scene_terrain.h"
#include "scene_transform.h"
#include "scene_vfx.h"
#include "scene_visibility.h"

typedef enum {
  PrefabResource_MapAcquired  = 1 << 0,
  PrefabResource_MapUnloading = 1 << 1,
} PrefabResourceFlags;

/**
 * Two kinds of different spawn requests are supported:
 * - 'scene_prefab_spawn()': Does not require a global write but will be processed next frame.
 * - 'scene_prefab_spawn_onto()': Requires a global write but can be processed this frame.
 */

typedef struct {
  ScenePrefabSpec spec;
  EcsEntityId     entity;
} ScenePrefabRequest;

ecs_comp_define(ScenePrefabEnvComp) {
  PrefabResourceFlags flags;
  String              mapId;
  EcsEntityId         mapEntity;
  u32                 mapVersion;
  DynArray            requests; // ScenePrefabRequest[]
};

ecs_comp_define(ScenePrefabRequestComp) { ScenePrefabSpec spec; };
ecs_comp_define_public(ScenePrefabInstanceComp);

static void ecs_destruct_prefab_env(void* data) {
  ScenePrefabEnvComp* comp = data;
  string_free(g_allocHeap, comp->mapId);
  dynarray_destroy(&comp->requests);
}

ecs_view_define(GlobalResourceUpdateView) {
  ecs_access_write(ScenePrefabEnvComp);
  ecs_access_write(AssetManagerComp);
}

ecs_view_define(GlobalSpawnView) {
  ecs_access_read(SceneTerrainComp);
  ecs_access_write(SceneNavEnvComp);
  ecs_access_write(ScenePrefabEnvComp);
}

ecs_view_define(InstanceLayerUpdateView) {
  ecs_access_read(SceneFactionComp);
  ecs_access_read(ScenePrefabInstanceComp);
  ecs_access_write(SceneCollisionComp);
}

ecs_view_define(PrefabMapAssetView) { ecs_access_read(AssetPrefabMapComp); }
ecs_view_define(PrefabSpawnView) { ecs_access_read(ScenePrefabRequestComp); }

static void prefab_validate_pos(MAYBE_UNUSED const GeoVector vec) {
  diag_assert_msg(
      geo_vector_mag_sqr(vec) <= (1e5f * 1e5f),
      "Position ({}) is out of bounds",
      geo_vector_fmt(vec));
}

static SceneLayer prefab_instance_layer(const SceneFaction faction, const AssetPrefabFlags flags) {
  if (flags & AssetPrefabFlags_Infantry) {
    return SceneLayer_Infantry & scene_faction_layers(faction);
  }
  if (flags & AssetPrefabFlags_Vehicle) {
    return SceneLayer_Vehicle & scene_faction_layers(faction);
  }
  if (flags & AssetPrefabFlags_Structure) {
    return SceneLayer_Structure & scene_faction_layers(faction);
  }
  if (flags & AssetPrefabFlags_Destructible) {
    return SceneLayer_Destructible;
  }
  return SceneLayer_Environment;
}

ecs_system_define(ScenePrefabResourceInitSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalResourceUpdateView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  AssetManagerComp*   assets = ecs_view_write_t(globalItr, AssetManagerComp);
  ScenePrefabEnvComp* env    = ecs_view_write_t(globalItr, ScenePrefabEnvComp);

  if (!env->mapEntity) {
    env->mapEntity = asset_lookup(world, assets, env->mapId);
  }

  if (!(env->flags & (PrefabResource_MapAcquired | PrefabResource_MapUnloading))) {
    asset_acquire(world, env->mapEntity);
    env->flags |= PrefabResource_MapAcquired;
    ++env->mapVersion;

    log_i(
        "Acquiring prefab-map",
        log_param("id", fmt_text(env->mapId)),
        log_param("version", fmt_int(env->mapVersion)));
  }
}

ecs_system_define(ScenePrefabResourceUnloadChangedSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalResourceUpdateView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  ScenePrefabEnvComp* env = ecs_view_write_t(globalItr, ScenePrefabEnvComp);
  if (!env->mapEntity) {
    return;
  }

  const bool isLoaded   = ecs_world_has_t(world, env->mapEntity, AssetLoadedComp);
  const bool isFailed   = ecs_world_has_t(world, env->mapEntity, AssetFailedComp);
  const bool hasChanged = ecs_world_has_t(world, env->mapEntity, AssetChangedComp);

  if (env->flags & PrefabResource_MapAcquired && (isLoaded || isFailed) && hasChanged) {
    log_i(
        "Unloading prefab-map",
        log_param("id", fmt_text(env->mapId)),
        log_param("reason", fmt_text_lit("Asset changed")));

    asset_release(world, env->mapEntity);
    env->flags &= ~PrefabResource_MapAcquired;
    env->flags |= PrefabResource_MapUnloading;
  }
  if (env->flags & PrefabResource_MapUnloading && !isLoaded) {
    env->flags &= ~PrefabResource_MapUnloading;
  }
}

ecs_system_define(ScenePrefabInstanceLayerUpdateSys) {
  /**
   * Update the collision layer for prefab instances to support changing factions at runtime.
   */
  EcsView* instanceView = ecs_world_view_t(world, InstanceLayerUpdateView);
  for (EcsIterator* itr = ecs_view_itr(instanceView); ecs_view_walk(itr);) {
    const ScenePrefabInstanceComp* instanceComp  = ecs_view_read_t(itr, ScenePrefabInstanceComp);
    const SceneFactionComp*        factionComp   = ecs_view_read_t(itr, SceneFactionComp);
    SceneCollisionComp*            collisionComp = ecs_view_write_t(itr, SceneCollisionComp);

    collisionComp->layer = prefab_instance_layer(factionComp->id, instanceComp->assetFlags);
  }
}

static void setup_name(EcsWorld* w, EcsEntityId e, const AssetPrefabTraitName* t) {
  ecs_world_add_t(w, e, SceneNameComp, .name = t->name);
}

static void setup_set_member(EcsWorld* w, EcsEntityId e, const AssetPrefabTraitSetMember* t) {
  scene_set_member_create(w, e, t->sets, array_elems(t->sets));
}

static void setup_renderable(EcsWorld* w, EcsEntityId e, const AssetPrefabTraitRenderable* t) {
  ecs_world_add_t(w, e, SceneRenderableComp, .graphic = t->graphic, .color = geo_color_white);
}

static void setup_vfx_system(EcsWorld* w, EcsEntityId e, const AssetPrefabTraitVfx* t) {
  ecs_world_add_t(
      w, e, SceneVfxSystemComp, .asset = t->asset, .alpha = 1.0f, .emitMultiplier = 1.0f);
}

static void setup_vfx_decal(EcsWorld* w, EcsEntityId e, const AssetPrefabTraitDecal* t) {
  ecs_world_add_t(w, e, SceneVfxDecalComp, .asset = t->asset, .alpha = 1.0f);
}

static void setup_sound(EcsWorld* w, EcsEntityId e, const AssetPrefabTraitSound* t) {
  u32 assetCount = 0;
  array_for_t(t->assets, EcsEntityId, asset) {
    if (ecs_entity_valid(*asset)) {
      ++assetCount;
    }
  }
  if (LIKELY(assetCount)) {
    ecs_world_add_t(
        w,
        e,
        SceneSoundComp,
        .asset   = t->assets[(u32)(assetCount * rng_sample_f32(g_rng))],
        .gain    = rng_sample_range(g_rng, t->gainMin, t->gainMax),
        .pitch   = rng_sample_range(g_rng, t->pitchMin, t->pitchMax),
        .looping = t->looping);
  }
}

static void setup_light_point(EcsWorld* w, EcsEntityId e, const AssetPrefabTraitLightPoint* t) {
  ecs_world_add_t(w, e, SceneLightPointComp, .radiance = t->radiance, .radius = t->radius);
}

static void setup_light_dir(EcsWorld* w, EcsEntityId e, const AssetPrefabTraitLightDir* t) {
  ecs_world_add_t(
      w,
      e,
      SceneLightDirComp,
      .radiance = t->radiance,
      .shadows  = t->shadows,
      .coverage = t->coverage);
}

static void setup_light_ambient(EcsWorld* w, EcsEntityId e, const AssetPrefabTraitLightAmbient* t) {
  ecs_world_add_t(w, e, SceneLightAmbientComp, .intensity = t->intensity);
}

static void setup_lifetime(EcsWorld* w, EcsEntityId e, const AssetPrefabTraitLifetime* t) {
  ecs_world_add_t(w, e, SceneLifetimeDurationComp, .duration = t->duration);
}

static void setup_movement(
    EcsWorld* w, SceneNavEnvComp* navEnv, const EcsEntityId e, const AssetPrefabTraitMovement* t) {
  ecs_world_add_t(
      w,
      e,
      SceneLocomotionComp,
      .maxSpeed         = t->speed,
      .rotationSpeedRad = t->rotationSpeedRad,
      .radius           = t->radius,
      .weight           = t->weight,
      .moveAnimation    = t->moveAnimation);

  if (t->wheeled) {
    ecs_world_add_t(
        w,
        e,
        SceneLocomotionWheeledComp,
        .acceleration  = t->wheeledAcceleration,
        .terrainNormal = geo_up);
  }

  diag_assert(t->navLayer < SceneNavLayer_Count);
  scene_nav_add_agent(w, navEnv, e, (SceneNavLayer)t->navLayer);
}

static void setup_footstep(EcsWorld* w, const EcsEntityId e, const AssetPrefabTraitFootstep* t) {
  ecs_world_add_t(
      w,
      e,
      SceneFootstepComp,
      .jointNames[0]  = t->jointA,
      .jointNames[1]  = t->jointB,
      .decalAssets[0] = t->decalAssetA,
      .decalAssets[1] = t->decalAssetB);
}

static void setup_health(EcsWorld* w, const EcsEntityId e, const AssetPrefabTraitHealth* t) {
  ecs_world_add_t(
      w,
      e,
      SceneHealthComp,
      .norm              = 1.0f,
      .max               = t->amount,
      .deathDestroyDelay = t->deathDestroyDelay,
      .deathEffectPrefab = t->deathEffectPrefab);

  ecs_world_add_t(w, e, SceneHealthRequestComp);
}

static void setup_attack(EcsWorld* w, const EcsEntityId e, const AssetPrefabTraitAttack* t) {
  ecs_world_add_t(
      w,
      e,
      SceneAttackComp,
      .weaponName        = t->weapon,
      .lastHasTargetTime = -time_hour,
      .lastFireTime      = -time_hour);
  if (t->aimJoint) {
    ecs_world_add_t(
        w,
        e,
        SceneAttackAimComp,
        .aimJoint       = t->aimJoint,
        .aimSpeedRad    = t->aimSpeedRad,
        .aimLocalActual = geo_quat_ident,
        .aimLocalTarget = geo_quat_ident);
  }
  SceneTargetConfig config = 0;
  if (t->targetExcludeUnreachable) {
    config |= SceneTargetConfig_ExcludeUnreachable;
  }
  if (t->targetExcludeObscured) {
    config |= SceneTargetConfig_ExcludeObscured;
  }
  ecs_world_add_t(
      w,
      e,
      SceneTargetFinderComp,
      .config   = config,
      .rangeMin = t->targetRangeMin,
      .rangeMax = t->targetRangeMax);
}

static void setup_collision(
    EcsWorld*                        w,
    const EcsEntityId                e,
    const ScenePrefabSpec*           s,
    const AssetPrefab*               p,
    const AssetPrefabTraitCollision* t) {
  if (t->navBlocker) {
    scene_nav_add_blocker(w, e, SceneNavBlockerMask_All);
  }
  const SceneLayer layer = prefab_instance_layer(s->faction, p->flags);
  switch (t->shape.type) {
  case AssetPrefabShape_Sphere: {
    const SceneCollisionSphere sphere = {
        .offset = t->shape.data_sphere.offset,
        .radius = t->shape.data_sphere.radius,
    };
    scene_collision_add_sphere(w, e, sphere, layer);
  } break;
  case AssetPrefabShape_Capsule: {
    const SceneCollisionCapsule capsule = {
        .offset = t->shape.data_capsule.offset,
        .dir    = SceneCollision_Up, // TODO: Make this configurable.
        .radius = t->shape.data_capsule.radius,
        .height = t->shape.data_capsule.height,
    };
    scene_collision_add_capsule(w, e, capsule, layer);
  } break;
  case AssetPrefabShape_Box: {
    const SceneCollisionBox box = {
        .min = t->shape.data_box.min,
        .max = t->shape.data_box.max,
    };
    scene_collision_add_box(w, e, box, layer);
  } break;
  }
}

static void setup_script(
    EcsWorld*                     w,
    const EcsEntityId             e,
    const AssetPrefabMapComp*     m,
    const AssetPrefabTraitScript* t) {

  scene_script_add(w, e, t->scriptAssets, t->scriptAssetCount);

  SceneKnowledgeComp* knowledge = scene_knowledge_add(w, e);
  for (u16 i = 0; i != t->knowledgeCount; ++i) {
    const AssetPrefabValue* val = &m->values.values[t->knowledgeIndex + i];
    switch (val->type) {
    case AssetPrefabValue_Number:
      scene_knowledge_store(knowledge, val->name, script_num(val->data_number));
      break;
    case AssetPrefabValue_Bool:
      scene_knowledge_store(knowledge, val->name, script_bool(val->data_bool));
      break;
    case AssetPrefabValue_Vector3:
      scene_knowledge_store(knowledge, val->name, script_vec3(val->data_vector3));
      break;
    case AssetPrefabValue_Color:
      scene_knowledge_store(knowledge, val->name, script_color(val->data_color));
      break;
    case AssetPrefabValue_String:
      scene_knowledge_store(knowledge, val->name, script_str(val->data_string));
      break;
    case AssetPrefabValue_Asset:
      scene_knowledge_store(knowledge, val->name, script_entity(val->data_asset));
      break;
    case AssetPrefabValue_Sound:
      scene_knowledge_store(knowledge, val->name, script_entity(val->data_sound.asset));
      break;
    }
  }
}

static void setup_bark(EcsWorld* w, const EcsEntityId e, const AssetPrefabTraitBark* t) {
  ecs_world_add_t(
      w,
      e,
      SceneBarkComp,
      .priority                           = t->priority,
      .barkPrefabs[SceneBarkType_Death]   = t->barkDeathPrefab,
      .barkPrefabs[SceneBarkType_Confirm] = t->barkConfirmPrefab);
}

static void setup_location(EcsWorld* w, const EcsEntityId e, const AssetPrefabTraitLocation* t) {
  static const struct {
    SceneLocationType type;
    u32               traitOffset;
  } g_mappings[] = {
      {SceneLocationType_AimTarget, offsetof(AssetPrefabTraitLocation, aimTarget)},
  };

  SceneLocationComp* loc = ecs_world_add_t(w, e, SceneLocationComp);
  for (u32 i = 0; i != array_elems(g_mappings); ++i) {
    const AssetPrefabShapeBox* box       = bits_ptr_offset(t, g_mappings[i].traitOffset);
    loc->volumes[g_mappings[i].type].min = box->min;
    loc->volumes[g_mappings[i].type].max = box->max;
  }
}

static void setup_status(EcsWorld* w, const EcsEntityId e, const AssetPrefabTraitStatus* t) {
  ecs_world_add_t(
      w, e, SceneStatusComp, .supported = t->supportedStatusMask, .effectJoint = t->effectJoint);
  ecs_world_add_t(w, e, SceneStatusRequestComp);
}

static void setup_vision(EcsWorld* w, const EcsEntityId e, const AssetPrefabTraitVision* t) {
  SceneVisionFlags flags = 0;
  flags |= t->showInHud ? SceneVisionFlags_ShowInHud : 0;

  ecs_world_add_t(w, e, SceneVisionComp, .flags = flags, .radius = t->radius);
}

static void setup_attachment(
    EcsWorld*                         w,
    const EcsEntityId                 e,
    const ScenePrefabSpec*            s,
    const AssetPrefabTraitAttachment* t) {

  const EcsEntityId attachEntity = scene_prefab_spawn(
      w,
      &(ScenePrefabSpec){
          .flags    = ScenePrefabFlags_Volatile,
          .prefabId = t->attachmentPrefab,
          .faction  = s->faction,
          .position = s->position,
          .rotation = s->rotation,
          .scale    = t->attachmentScale});

  ecs_world_add_t(w, attachEntity, SceneLifetimeOwnerComp, .owners[0] = e);
  ecs_world_add_t(
      w,
      attachEntity,
      SceneAttachmentComp,
      .target     = e,
      .jointIndex = sentinel_u32,
      .jointName  = t->joint,
      .offset     = t->offset);
}

static void
setup_production(EcsWorld* w, const EcsEntityId e, const AssetPrefabTraitProduction* t) {
  ecs_world_add_t(
      w,
      e,
      SceneProductionComp,
      .productSetId    = t->productSetId,
      .flags           = SceneProductFlags_RallyLocalSpace,
      .rallySoundAsset = t->rallySoundAsset,
      .rallySoundGain  = t->rallySoundGain,
      .placementRadius = t->placementRadius,
      .spawnPos        = t->spawnPos,
      .rallyPos        = t->rallyPos);
}

static void setup_scale(EcsWorld* w, const EcsEntityId e, const f32 scale) {
  ecs_world_add_t(w, e, SceneScaleComp, .scale = scale < f32_epsilon ? 1.0 : scale);
}

static void setup_trait(
    EcsWorld*                 w,
    SceneNavEnvComp*          navEnv,
    const EcsEntityId         e,
    const ScenePrefabSpec*    s,
    const AssetPrefabMapComp* m,
    const AssetPrefab*        p,
    const AssetPrefabTrait*   t) {
  switch (t->type) {
  case AssetPrefabTrait_Name:
    setup_name(w, e, &t->data_name);
    return;
  case AssetPrefabTrait_SetMember:
    setup_set_member(w, e, &t->data_setMember);
    return;
  case AssetPrefabTrait_Renderable:
    setup_renderable(w, e, &t->data_renderable);
    return;
  case AssetPrefabTrait_Vfx:
    setup_vfx_system(w, e, &t->data_vfx);
    return;
  case AssetPrefabTrait_Decal:
    setup_vfx_decal(w, e, &t->data_decal);
    return;
  case AssetPrefabTrait_Sound:
    setup_sound(w, e, &t->data_sound);
    return;
  case AssetPrefabTrait_LightPoint:
    setup_light_point(w, e, &t->data_lightPoint);
    return;
  case AssetPrefabTrait_LightDir:
    setup_light_dir(w, e, &t->data_lightDir);
    return;
  case AssetPrefabTrait_LightAmbient:
    setup_light_ambient(w, e, &t->data_lightAmbient);
    return;
  case AssetPrefabTrait_Lifetime:
    setup_lifetime(w, e, &t->data_lifetime);
    return;
  case AssetPrefabTrait_Movement:
    setup_movement(w, navEnv, e, &t->data_movement);
    return;
  case AssetPrefabTrait_Footstep:
    setup_footstep(w, e, &t->data_footstep);
    return;
  case AssetPrefabTrait_Health:
    setup_health(w, e, &t->data_health);
    return;
  case AssetPrefabTrait_Attack:
    setup_attack(w, e, &t->data_attack);
    return;
  case AssetPrefabTrait_Collision:
    setup_collision(w, e, s, p, &t->data_collision);
    return;
  case AssetPrefabTrait_Script:
    setup_script(w, e, m, &t->data_script);
    return;
  case AssetPrefabTrait_Bark:
    setup_bark(w, e, &t->data_bark);
    return;
  case AssetPrefabTrait_Location:
    setup_location(w, e, &t->data_location);
    return;
  case AssetPrefabTrait_Status:
    setup_status(w, e, &t->data_status);
    return;
  case AssetPrefabTrait_Vision:
    setup_vision(w, e, &t->data_vision);
    return;
  case AssetPrefabTrait_Attachment:
    setup_attachment(w, e, s, &t->data_attachment);
    return;
  case AssetPrefabTrait_Production:
    setup_production(w, e, &t->data_production);
    return;
  case AssetPrefabTrait_Scalable:
    setup_scale(w, e, s->scale);
    return;
  case AssetPrefabTrait_Count:
    break;
  }
  diag_crash_msg("Unsupported prefab trait: '{}'", fmt_int(t->type));
}

static bool setup_prefab(
    EcsWorld*                 w,
    SceneNavEnvComp*          navEnv,
    const SceneTerrainComp*   terrain,
    const EcsEntityId         e,
    const ScenePrefabSpec*    spec,
    const AssetPrefabMapComp* map) {

  if ((spec->flags & ScenePrefabFlags_SnapToTerrain) && !scene_terrain_loaded(terrain)) {
    return false; // Wait until the terrain is loaded.
  }

  diag_assert_msg(spec->prefabId, "Invalid prefab id: {}", string_hash_fmt(spec->prefabId));
  ScenePrefabInstanceComp* instanceComp =
      ecs_world_add_t(w, e, ScenePrefabInstanceComp, .id = spec->id, .prefabId = spec->prefabId);

  const AssetPrefab* prefab = asset_prefab_get(map, spec->prefabId);
  if (UNLIKELY(!prefab)) {
    log_e("Prefab not found", log_param("entity", ecs_entity_fmt(e)));
    return true; // No point in retrying; mark the prefab as done.
  }
  instanceComp->assetFlags = prefab->flags;
  if ((spec->flags & ScenePrefabFlags_Volatile) || (prefab->flags & AssetPrefabFlags_Volatile)) {
    instanceComp->isVolatile = true;
  }
  GeoVector spawnPos = spec->position;
  if (spec->flags & ScenePrefabFlags_SnapToTerrain) {
    scene_terrain_snap(terrain, &spawnPos);
  }
  prefab_validate_pos(spawnPos);
  ecs_world_add_empty_t(w, e, SceneLevelInstanceComp);
  ecs_world_add_t(w, e, SceneTransformComp, .position = spawnPos, .rotation = spec->rotation);
  ecs_world_add_t(w, e, SceneVelocityComp);
  ecs_world_add_t(w, e, SceneTagComp, .tags = SceneTags_Default);
  scene_debug_init(w, e);

  if (prefab->flags & AssetPrefabFlags_Unit) {
    ecs_world_add_t(w, e, SceneVisibilityComp);
    ecs_world_add_t(w, e, SceneHealthStatsComp);
  }

  if (spec->faction != SceneFaction_None) {
    ecs_world_add_t(w, e, SceneFactionComp, .id = spec->faction);
  }

  for (u16 i = 0; i != prefab->traitCount; ++i) {
    const AssetPrefabTrait* trait = &map->traits.values[prefab->traitIndex + i];
    setup_trait(w, navEnv, e, spec, map, prefab, trait);
  }

  return true; // Prefab done processing.
}

ecs_system_define(ScenePrefabSpawnSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalSpawnView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  ScenePrefabEnvComp*     prefabEnv = ecs_view_write_t(globalItr, ScenePrefabEnvComp);
  SceneNavEnvComp*        navEnv    = ecs_view_write_t(globalItr, SceneNavEnvComp);
  const SceneTerrainComp* terrain   = ecs_view_read_t(globalItr, SceneTerrainComp);

  EcsView*     mapAssetView = ecs_world_view_t(world, PrefabMapAssetView);
  EcsIterator* mapAssetItr  = ecs_view_maybe_at(mapAssetView, prefabEnv->mapEntity);
  if (!mapAssetItr) {
    return;
  }
  const AssetPrefabMapComp* map = ecs_view_read_t(mapAssetItr, AssetPrefabMapComp);

  // Process global requests.
  for (usize i = prefabEnv->requests.size; i-- != 0;) {
    const ScenePrefabRequest* req = dynarray_at_t(&prefabEnv->requests, i, ScenePrefabRequest);
    if (setup_prefab(world, navEnv, terrain, req->entity, &req->spec, map)) {
      dynarray_remove_unordered(&prefabEnv->requests, i, 1);
    }
  }

  // Process entity requests.
  EcsView* spawnView = ecs_world_view_t(world, PrefabSpawnView);
  for (EcsIterator* itr = ecs_view_itr(spawnView); ecs_view_walk(itr);) {
    const EcsEntityId             entity  = ecs_view_entity(itr);
    const ScenePrefabRequestComp* request = ecs_view_read_t(itr, ScenePrefabRequestComp);

    if (setup_prefab(world, navEnv, terrain, entity, &request->spec, map)) {
      ecs_world_remove_t(world, entity, ScenePrefabRequestComp);
    }
  }
}

ecs_module_init(scene_prefab_module) {
  ecs_register_comp(ScenePrefabEnvComp, .destructor = ecs_destruct_prefab_env);
  ecs_register_comp(ScenePrefabRequestComp);
  ecs_register_comp(ScenePrefabInstanceComp);

  ecs_register_view(GlobalResourceUpdateView);
  ecs_register_view(GlobalSpawnView);
  ecs_register_view(InstanceLayerUpdateView);
  ecs_register_view(PrefabMapAssetView);
  ecs_register_view(PrefabSpawnView);

  ecs_register_system(ScenePrefabResourceInitSys, ecs_view_id(GlobalResourceUpdateView));
  ecs_register_system(ScenePrefabResourceUnloadChangedSys, ecs_view_id(GlobalResourceUpdateView));

  ecs_register_system(ScenePrefabInstanceLayerUpdateSys, ecs_view_id(InstanceLayerUpdateView));

  ecs_register_system(
      ScenePrefabSpawnSys,
      ecs_view_id(GlobalSpawnView),
      ecs_view_id(PrefabMapAssetView),
      ecs_view_id(PrefabSpawnView));
}

void scene_prefab_init(EcsWorld* world, const String prefabMapId) {
  diag_assert_msg(prefabMapId.size, "Invalid prefabMapId");

  ecs_world_add_t(
      world,
      ecs_world_global(world),
      ScenePrefabEnvComp,
      .mapId    = string_dup(g_allocHeap, prefabMapId),
      .requests = dynarray_create_t(g_allocHeap, ScenePrefabRequest, 32));
}

EcsEntityId scene_prefab_map(const ScenePrefabEnvComp* env) { return env->mapEntity; }

u32 scene_prefab_map_version(const ScenePrefabEnvComp* env) { return env->mapVersion; }

EcsEntityId scene_prefab_spawn(EcsWorld* world, const ScenePrefabSpec* spec) {
  diag_assert_msg(spec->prefabId, "Invalid prefab id: {}", string_hash_fmt(spec->prefabId));

  const EcsEntityId e = ecs_world_entity_create(world);
  ecs_world_add_t(world, e, ScenePrefabRequestComp, .spec = *spec);
  return e;
}

void scene_prefab_spawn_onto(
    ScenePrefabEnvComp* env, const ScenePrefabSpec* spec, const EcsEntityId e) {
  diag_assert_msg(spec->prefabId, "Invalid prefab id: {}", string_hash_fmt(spec->prefabId));

  *dynarray_push_t(&env->requests, ScenePrefabRequest) = (ScenePrefabRequest){
      .spec   = *spec,
      .entity = e,
  };
}
