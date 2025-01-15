#include "asset_manager.h"
#include "asset_prefab.h"
#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_float.h"
#include "core_math.h"
#include "core_rng.h"
#include "ecs_entity.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "scene_attachment.h"
#include "scene_attack.h"
#include "scene_bark.h"
#include "scene_collision.h"
#include "scene_debug.h"
#include "scene_footstep.h"
#include "scene_health.h"
#include "scene_lifetime.h"
#include "scene_light.h"
#include "scene_location.h"
#include "scene_locomotion.h"
#include "scene_name.h"
#include "scene_nav.h"
#include "scene_prefab.h"
#include "scene_product.h"
#include "scene_property.h"
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
#include "script_mem.h"

ASSERT(AssetPrefabTrait_Count < 64, "Prefab trait masks need to be representable with 64 bits")

// clang-format off

static const u64 g_prefabVariantTraitMask[ScenePrefabVariant_Count] = {
    [ScenePrefabVariant_Normal]  = ~u64_lit(0),

    [ScenePrefabVariant_Preview] = (u64_lit(1) << AssetPrefabTrait_Renderable)   |
                                   (u64_lit(1) << AssetPrefabTrait_Decal)        |
                                   (u64_lit(1) << AssetPrefabTrait_LightPoint)   |
                                   (u64_lit(1) << AssetPrefabTrait_LightDir)     |
                                   (u64_lit(1) << AssetPrefabTrait_LightAmbient) |
                                   (u64_lit(1) << AssetPrefabTrait_Attachment)   |
                                   (u64_lit(1) << AssetPrefabTrait_Scalable),

    [ScenePrefabVariant_Edit]    = (u64_lit(1) << AssetPrefabTrait_Renderable)   |
                                   (u64_lit(1) << AssetPrefabTrait_Decal)        |
                                   (u64_lit(1) << AssetPrefabTrait_LightPoint)   |
                                   (u64_lit(1) << AssetPrefabTrait_LightDir)     |
                                   (u64_lit(1) << AssetPrefabTrait_LightAmbient) |
                                   (u64_lit(1) << AssetPrefabTrait_Collision)    |
                                   (u64_lit(1) << AssetPrefabTrait_Script)       |
                                   (u64_lit(1) << AssetPrefabTrait_Attachment)   |
                                   (u64_lit(1) << AssetPrefabTrait_Scalable),
};

// clang-format on

typedef enum {
  PrefabResource_MapAcquired  = 1 << 0,
  PrefabResource_MapUnloading = 1 << 1,
} PrefabResourceFlags;

/**
 * Different kinds of spawn requests are supported:
 * - 'scene_prefab_spawn()': Does not require a global write but will be processed next frame.
 * - 'scene_prefab_spawn_onto()': Requires a global write but can be processed this frame.
 * - 'scene_prefab_spawn_replace()': Resets the entity before spawn, takes an additional frame.
 */

typedef struct {
  ScenePrefabSpec spec;
  bool            entityReset; // Remove all entity components before setting up the prefab.
  EcsEntityId     entity;
} ScenePrefabRequest;

ecs_comp_define(ScenePrefabEnvComp) {
  PrefabResourceFlags flags;
  String              mapId;
  EcsEntityId         mapEntity;
  u32                 mapVersion, mapVersionApplied;
  DynArray            requests; // ScenePrefabRequest[]
};

ecs_comp_define(ScenePrefabRequestComp) { ScenePrefabSpec spec; };
ecs_comp_define_public(ScenePrefabInstanceComp);

static ScenePrefabSpec prefab_spec_dup(const ScenePrefabSpec* spec, Allocator* alloc) {
  ScenePrefabSpec res = *spec;
  if (spec->propertyCount) {
    diag_assert(spec->properties);
    const usize propertyMemSize = sizeof(ScenePrefabProperty) * spec->propertyCount;
    const Mem   propertyMemDup  = alloc_alloc(alloc, propertyMemSize, alignof(ScenePrefabProperty));
    mem_cpy(propertyMemDup, mem_create(spec->properties, propertyMemSize));
    res.properties = propertyMemDup.ptr;
  }
  return res;
}

static void prefab_spec_destroy(const ScenePrefabSpec* spec, Allocator* alloc) {
  if (spec->propertyCount) {
    const usize propertyMemSize = sizeof(ScenePrefabProperty) * spec->propertyCount;
    alloc_free(alloc, mem_create(spec->properties, propertyMemSize));
  }
}

static void ecs_destruct_prefab_env(void* data) {
  ScenePrefabEnvComp* comp = data;
  string_free(g_allocHeap, comp->mapId);
  dynarray_for_t(&comp->requests, ScenePrefabRequest, req) {
    prefab_spec_destroy(&req->spec, g_allocHeap);
  }
  dynarray_destroy(&comp->requests);
}

static void ecs_destruct_prefab_request(void* data) {
  ScenePrefabRequestComp* comp = data;
  prefab_spec_destroy(&comp->spec, g_allocHeap);
}

ecs_view_define(GlobalResourceUpdateView) {
  ecs_access_write(ScenePrefabEnvComp);
  ecs_access_write(AssetManagerComp);
}

ecs_view_define(GlobalRefreshView) { ecs_access_write(ScenePrefabEnvComp); }

ecs_view_define(GlobalSpawnView) {
  ecs_access_read(SceneTerrainComp);
  ecs_access_write(SceneNavEnvComp);
  ecs_access_write(ScenePrefabEnvComp);
}

ecs_view_define(InstanceRefreshView) {
  ecs_access_read(ScenePrefabInstanceComp);
  ecs_access_read(SceneTransformComp);
  ecs_access_maybe_read(SceneFactionComp);
  ecs_access_maybe_read(ScenePropertyComp);
  ecs_access_maybe_read(SceneScaleComp);
  ecs_access_maybe_read(SceneSetMemberComp);
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

static const ScenePrefabRequest*
prefab_request_find(const ScenePrefabEnvComp* env, const EcsEntityId entity) {
  dynarray_for_t(&env->requests, ScenePrefabRequest, req) {
    if (req->entity == entity) {
      return req;
    }
  }
  return null;
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

static bool prefab_is_volatile(const AssetPrefab* prefab, const ScenePrefabSpec* spec) {
  if (prefab->flags & AssetPrefabFlags_Volatile) {
    return true;
  }
  if (spec->flags & ScenePrefabFlags_Volatile) {
    return true;
  }
  if (spec->variant == ScenePrefabVariant_Preview) {
    return true;
  }
  return false;
}

ecs_system_define(ScenePrefabResourceUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalResourceUpdateView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return; // Global dependencies not ready.
  }
  AssetManagerComp*   assets = ecs_view_write_t(globalItr, AssetManagerComp);
  ScenePrefabEnvComp* env    = ecs_view_write_t(globalItr, ScenePrefabEnvComp);

  if (!env->mapEntity) {
    env->mapEntity = asset_lookup(world, assets, env->mapId);
  }

  const bool isLoaded   = ecs_world_has_t(world, env->mapEntity, AssetLoadedComp);
  const bool isFailed   = ecs_world_has_t(world, env->mapEntity, AssetFailedComp);
  const bool hasChanged = ecs_world_has_t(world, env->mapEntity, AssetChangedComp);

  if (isFailed) {
    log_e("Failed to load prefab-map", log_param("id", fmt_text(env->mapId)));
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
  if (env->flags & PrefabResource_MapAcquired && (isLoaded || isFailed) && hasChanged) {
    asset_release(world, env->mapEntity);
    env->flags &= ~PrefabResource_MapAcquired;
    env->flags |= PrefabResource_MapUnloading;
  }
  if (env->flags & PrefabResource_MapUnloading && !(isLoaded || isFailed)) {
    env->flags &= ~PrefabResource_MapUnloading; // Unload finished.
  }
}

static void prefab_extract_props(const ScenePropertyComp* comp, ScenePrefabSpec* out) {
  enum { MaxResults = 128 };

  ScenePrefabProperty* res      = alloc_array_t(g_allocScratch, ScenePrefabProperty, MaxResults);
  u16                  resCount = 0;

  const ScriptMem* memory = scene_prop_memory(comp);
  for (ScriptMemItr itr = script_mem_begin(memory); itr.key; itr = script_mem_next(memory, itr)) {
    const ScriptVal val = script_mem_load(memory, itr.key);
    if (script_type(val) != ScriptType_Null) {
      if (resCount == MaxResults) {
        break; // Maximum properties reached. TODO: Should this be an error?
      }
      res[resCount++] = (ScenePrefabProperty){.key = itr.key, .value = val};
    }
  }

  out->properties    = res;
  out->propertyCount = resCount;
}

static void prefab_refresh(ScenePrefabEnvComp* prefabEnv, EcsIterator* itr) {
  const EcsEntityId              entity         = ecs_view_entity(itr);
  const SceneTransformComp*      transComp      = ecs_view_read_t(itr, SceneTransformComp);
  const SceneScaleComp*          scaleComp      = ecs_view_read_t(itr, SceneScaleComp);
  const SceneFactionComp*        factionComp    = ecs_view_read_t(itr, SceneFactionComp);
  const ScenePrefabInstanceComp* prefabInstComp = ecs_view_read_t(itr, ScenePrefabInstanceComp);
  diag_assert(prefabInstComp && prefabInstComp->variant == ScenePrefabVariant_Edit);

  ScenePrefabSpec spec = {
      .id       = prefabInstComp->id,
      .prefabId = prefabInstComp->prefabId,
      .variant  = ScenePrefabVariant_Edit,
      .faction  = factionComp ? factionComp->id : SceneFaction_None,
      .scale    = scaleComp ? scaleComp->scale : 1.0f,
      .position = transComp->position,
      .rotation = transComp->rotation,
  };
  const ScenePropertyComp* propComp = ecs_view_read_t(itr, ScenePropertyComp);
  if (propComp) {
    prefab_extract_props(propComp, &spec);
  }
  const SceneSetMemberComp* setMember = ecs_view_read_t(itr, SceneSetMemberComp);
  if (setMember) {
    ASSERT(array_elems(spec.sets) >= scene_set_member_max_sets, "Insufficient set storage");
    scene_set_member_all(setMember, spec.sets);
  }
  scene_prefab_spawn_replace(prefabEnv, &spec, entity);
}

ecs_system_define(ScenePrefabInstanceRefreshSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalRefreshView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return; // Global dependencies not ready.
  }
  ScenePrefabEnvComp* prefabEnv = ecs_view_write_t(globalItr, ScenePrefabEnvComp);

  EcsView*     mapAssetView = ecs_world_view_t(world, PrefabMapAssetView);
  EcsIterator* mapAssetItr  = ecs_view_maybe_at(mapAssetView, prefabEnv->mapEntity);
  if (!mapAssetItr) {
    return; // Map asset is being loaded.
  }
  const AssetPrefabMapComp* map = ecs_view_read_t(mapAssetItr, AssetPrefabMapComp);
  if (prefabEnv->mapVersion == prefabEnv->mapVersionApplied) {
    return; // No need to refresh.
  }

  EcsView* instanceView = ecs_world_view_t(world, InstanceRefreshView);
  for (EcsIterator* itr = ecs_view_itr(instanceView); ecs_view_walk(itr);) {
    const ScenePrefabInstanceComp* prefabInstComp = ecs_view_read_t(itr, ScenePrefabInstanceComp);
    if (prefabInstComp->variant != ScenePrefabVariant_Edit) {
      continue; // Not a edit variant; do nothing.
    }
    const AssetPrefab* prefab = asset_prefab_find(map, prefabInstComp->prefabId);
    if (!prefab) {
      continue; // Prefab has been removed from the map; do nothing.
    }
    if (prefabInstComp->assetHash != prefab->hash) {
      log_d("Refreshing prefab", log_param("entity", ecs_entity_fmt(ecs_view_entity(itr))));
      prefab_refresh(prefabEnv, itr);
    }
  }
  prefabEnv->mapVersionApplied = prefabEnv->mapVersion;
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

typedef struct {
  EcsWorld*                 world;
  SceneNavEnvComp*          navEnv;
  const AssetPrefabMapComp* prefabMap;
  const AssetPrefab*        prefab;
  EcsEntityId               entity;
  const ScenePrefabSpec*    spec;
  ScenePropertyComp*        propComp; // Added on-demand to the resulting entity.
} PrefabSetupContext;

static void setup_name(PrefabSetupContext* ctx, const AssetPrefabTraitName* t) {
  ecs_world_add_t(ctx->world, ctx->entity, SceneNameComp, .name = t->name);
}

static void setup_set_member(PrefabSetupContext* ctx, const AssetPrefabTraitSetMember* t) {
  scene_set_member_create(ctx->world, ctx->entity, t->sets, array_elems(t->sets));
}

static void setup_renderable(PrefabSetupContext* ctx, const AssetPrefabTraitRenderable* t) {
  GeoColor color;
  if (ctx->spec->variant == ScenePrefabVariant_Preview) {
    color = geo_color(1, 1, 1, 0.5f);
  } else {
    color = geo_color_white;
  }
  ecs_world_add_t(
      ctx->world, ctx->entity, SceneRenderableComp, .graphic = t->graphic.entity, .color = color);
}

static void setup_vfx_system(PrefabSetupContext* ctx, const AssetPrefabTraitVfx* t) {
  ecs_world_add_t(
      ctx->world,
      ctx->entity,
      SceneVfxSystemComp,
      .asset          = t->asset.entity,
      .alpha          = 1.0f,
      .emitMultiplier = 1.0f);
}

static void setup_vfx_decal(PrefabSetupContext* ctx, const AssetPrefabTraitDecal* t) {
  ecs_world_add_t(
      ctx->world,
      ctx->entity,
      SceneVfxDecalComp,
      .asset = t->asset.entity,
      .alpha = ctx->spec->variant == ScenePrefabVariant_Preview ? 0.5f : 1.0f);
}

static void setup_sound(PrefabSetupContext* ctx, const AssetPrefabTraitSound* t) {
  u32 count = 0;
  for (; count != array_elems(t->assets) && ecs_entity_valid(t->assets[count].entity); ++count)
    ;

  if (LIKELY(count)) {
    ecs_world_add_t(
        ctx->world,
        ctx->entity,
        SceneSoundComp,
        .asset   = t->assets[(u32)(count * rng_sample_f32(g_rng))].entity,
        .gain    = rng_sample_range(g_rng, t->gainMin, t->gainMax),
        .pitch   = rng_sample_range(g_rng, t->pitchMin, t->pitchMax),
        .looping = t->looping);
  }
}

static void setup_light_point(PrefabSetupContext* ctx, const AssetPrefabTraitLightPoint* t) {
  ecs_world_add_t(
      ctx->world, ctx->entity, SceneLightPointComp, .radiance = t->radiance, .radius = t->radius);
}

static void setup_light_dir(PrefabSetupContext* ctx, const AssetPrefabTraitLightDir* t) {
  ecs_world_add_t(
      ctx->world,
      ctx->entity,
      SceneLightDirComp,
      .radiance = t->radiance,
      .shadows  = t->shadows,
      .coverage = t->coverage);
}

static void setup_light_ambient(PrefabSetupContext* ctx, const AssetPrefabTraitLightAmbient* t) {
  ecs_world_add_t(ctx->world, ctx->entity, SceneLightAmbientComp, .intensity = t->intensity);
}

static void setup_lifetime(PrefabSetupContext* ctx, const AssetPrefabTraitLifetime* t) {
  ecs_world_add_t(ctx->world, ctx->entity, SceneLifetimeDurationComp, .duration = t->duration);
}

static void setup_movement(PrefabSetupContext* ctx, const AssetPrefabTraitMovement* t) {
  ecs_world_add_t(
      ctx->world,
      ctx->entity,
      SceneLocomotionComp,
      .maxSpeed         = t->speed,
      .rotationSpeedRad = t->rotationSpeed,
      .radius           = t->radius,
      .weight           = t->weight,
      .moveAnimation    = t->moveAnimation);

  if (t->wheeled) {
    ecs_world_add_t(
        ctx->world,
        ctx->entity,
        SceneLocomotionWheeledComp,
        .acceleration  = t->wheeledAcceleration,
        .terrainNormal = geo_up);
  }

  diag_assert(t->navLayer < SceneNavLayer_Count);
  scene_nav_add_agent(ctx->world, ctx->navEnv, ctx->entity, (SceneNavLayer)t->navLayer);
}

static void setup_footstep(PrefabSetupContext* ctx, const AssetPrefabTraitFootstep* t) {
  ecs_world_add_t(
      ctx->world,
      ctx->entity,
      SceneFootstepComp,
      .jointNames[0]  = t->jointA,
      .jointNames[1]  = t->jointB,
      .decalAssets[0] = t->decalA.entity,
      .decalAssets[1] = t->decalB.entity);
}

static void setup_health(PrefabSetupContext* ctx, const AssetPrefabTraitHealth* t) {
  ecs_world_add_t(
      ctx->world,
      ctx->entity,
      SceneHealthComp,
      .norm              = 1.0f,
      .max               = t->amount,
      .deathDestroyDelay = t->deathDestroyDelay,
      .deathEffectPrefab = t->deathEffectPrefab);

  ecs_world_add_t(ctx->world, ctx->entity, SceneHealthRequestComp);
}

static void setup_attack(PrefabSetupContext* ctx, const AssetPrefabTraitAttack* t) {
  ecs_world_add_t(
      ctx->world,
      ctx->entity,
      SceneAttackComp,
      .weaponName        = t->weapon,
      .lastHasTargetTime = -time_hour,
      .lastFireTime      = -time_hour);
  if (t->aimJoint) {
    ecs_world_add_t(
        ctx->world,
        ctx->entity,
        SceneAttackAimComp,
        .aimJoint       = t->aimJoint,
        .aimSpeedRad    = t->aimSpeed,
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
      ctx->world,
      ctx->entity,
      SceneTargetFinderComp,
      .config   = config,
      .rangeMin = t->targetRangeMin,
      .rangeMax = t->targetRangeMax);
}

static void setup_collision(PrefabSetupContext* ctx, const AssetPrefabTraitCollision* t) {
  if (t->navBlocker) {
    scene_nav_add_blocker(ctx->world, ctx->entity, SceneNavBlockerMask_All);
  }
  const SceneLayer layer = prefab_instance_layer(ctx->spec->faction, ctx->prefab->flags);

  SceneCollisionShape collisionShapes[32];
  const u32           shapeCount = math_min(array_elems(collisionShapes), t->shapeCount);

  for (u32 i = 0; i != shapeCount; ++i) {
    AssetPrefabShape* prefabShape = &ctx->prefabMap->shapes.values[t->shapeIndex + i];
    switch (prefabShape->type) {
    case AssetPrefabShape_Sphere:
      collisionShapes[i].type   = SceneCollisionType_Sphere;
      collisionShapes[i].sphere = prefabShape->data_sphere;
      break;
    case AssetPrefabShape_Capsule: {
      collisionShapes[i].type    = SceneCollisionType_Capsule;
      collisionShapes[i].capsule = prefabShape->data_capsule;
    } break;
    case AssetPrefabShape_Box:
      collisionShapes[i].type = SceneCollisionType_Box;
      collisionShapes[i].box  = prefabShape->data_box;
      break;
    }
  }
  scene_collision_add(ctx->world, ctx->entity, layer, collisionShapes, shapeCount);
}

static void setup_script(PrefabSetupContext* ctx, const AssetPrefabTraitScript* t) {
  u32 scriptCount = 0;
  for (; scriptCount != array_elems(t->scripts) && t->scripts[scriptCount]; ++scriptCount)
    ;

  SceneScriptComp* comp = scene_script_add(ctx->world, ctx->entity, t->scripts, scriptCount);

  if (!ctx->propComp) {
    ctx->propComp = scene_prop_add(ctx->world, ctx->entity);
  }

  if (ctx->spec->variant == ScenePrefabVariant_Normal) {
    scene_script_flags_set(comp, SceneScriptFlags_Enabled);

    for (u16 i = 0; i != t->propCount; ++i) {
      const AssetProperty* p = &ctx->prefabMap->properties.values[t->propIndex + i];
      switch (p->type) {
      case AssetProperty_Num:
        scene_prop_store(ctx->propComp, p->name, script_num(p->data_num));
        break;
      case AssetProperty_Bool:
        scene_prop_store(ctx->propComp, p->name, script_bool(p->data_bool));
        break;
      case AssetProperty_Vec3:
        scene_prop_store(ctx->propComp, p->name, script_vec3(p->data_vec3));
        break;
      case AssetProperty_Quat:
        scene_prop_store(ctx->propComp, p->name, script_quat(p->data_quat));
        break;
      case AssetProperty_Color:
        scene_prop_store(ctx->propComp, p->name, script_color(p->data_color));
        break;
      case AssetProperty_Str:
        scene_prop_store(ctx->propComp, p->name, script_str_or_null(p->data_str));
        break;
      case AssetProperty_LevelEntity:
        log_e("Level references are not supported in prefabs");
        break;
      case AssetProperty_Asset:
        scene_prop_store(ctx->propComp, p->name, script_entity(p->data_asset.entity));
        break;
      case AssetProperty_Count:
        UNREACHABLE
      }
    }
  }
}

static void setup_bark(PrefabSetupContext* ctx, const AssetPrefabTraitBark* t) {
  ecs_world_add_t(
      ctx->world,
      ctx->entity,
      SceneBarkComp,
      .priority                           = t->priority,
      .barkPrefabs[SceneBarkType_Death]   = t->barkDeathPrefab,
      .barkPrefabs[SceneBarkType_Confirm] = t->barkConfirmPrefab);
}

static void setup_location(PrefabSetupContext* ctx, const AssetPrefabTraitLocation* t) {
  static const struct {
    SceneLocationType type;
    u32               traitOffset;
  } g_mappings[] = {
      {SceneLocationType_AimTarget, offsetof(AssetPrefabTraitLocation, aimTarget)},
  };

  SceneLocationComp* loc = ecs_world_add_t(ctx->world, ctx->entity, SceneLocationComp);
  for (u32 i = 0; i != array_elems(g_mappings); ++i) {
    const GeoBox* box                = bits_ptr_offset(t, g_mappings[i].traitOffset);
    loc->volumes[g_mappings[i].type] = *box;
  }
}

static void setup_status(PrefabSetupContext* ctx, const AssetPrefabTraitStatus* t) {
  ecs_world_add_t(
      ctx->world,
      ctx->entity,
      SceneStatusComp,
      .supported   = (u8)t->supportedStatus,
      .effectJoint = t->effectJoint);
  ecs_world_add_t(ctx->world, ctx->entity, SceneStatusRequestComp);
}

static void setup_vision(PrefabSetupContext* ctx, const AssetPrefabTraitVision* t) {
  SceneVisionFlags flags = 0;
  flags |= t->showInHud ? SceneVisionFlags_ShowInHud : 0;

  ecs_world_add_t(ctx->world, ctx->entity, SceneVisionComp, .flags = flags, .radius = t->radius);
}

static void setup_attachment(PrefabSetupContext* ctx, const AssetPrefabTraitAttachment* t) {

  const EcsEntityId attachEntity = scene_prefab_spawn(
      ctx->world,
      &(ScenePrefabSpec){
          .flags    = ScenePrefabFlags_Volatile,
          .prefabId = t->attachmentPrefab,
          .variant  = ctx->spec->variant,
          .faction  = ctx->spec->faction,
          .position = ctx->spec->position,
          .rotation = ctx->spec->rotation,
          .scale    = t->attachmentScale});

  ecs_world_add_t(ctx->world, attachEntity, SceneLifetimeOwnerComp, .owners[0] = ctx->entity);
  ecs_world_add_t(
      ctx->world,
      attachEntity,
      SceneAttachmentComp,
      .target     = ctx->entity,
      .jointIndex = sentinel_u32,
      .jointName  = t->joint,
      .offset     = t->offset);
}

static void setup_production(PrefabSetupContext* ctx, const AssetPrefabTraitProduction* t) {
  ecs_world_add_t(
      ctx->world,
      ctx->entity,
      SceneProductionComp,
      .productSetId    = t->productSetId,
      .flags           = SceneProductFlags_RallyLocalSpace,
      .rallySoundAsset = t->rallySound.entity,
      .rallySoundGain  = t->rallySoundGain,
      .placementRadius = t->placementRadius,
      .spawnPos        = t->spawnPos,
      .rallyPos        = t->rallyPos);
}

static void setup_scale(PrefabSetupContext* ctx, const f32 scale) {
  ecs_world_add_t(
      ctx->world, ctx->entity, SceneScaleComp, .scale = scale < f32_epsilon ? 1.0 : scale);
}

static void setup_trait(PrefabSetupContext* ctx, const AssetPrefabTrait* t) {
  switch (t->type) {
  case AssetPrefabTrait_Name:
    setup_name(ctx, &t->data_name);
    return;
  case AssetPrefabTrait_SetMember:
    setup_set_member(ctx, &t->data_setMember);
    return;
  case AssetPrefabTrait_Renderable:
    setup_renderable(ctx, &t->data_renderable);
    return;
  case AssetPrefabTrait_Vfx:
    setup_vfx_system(ctx, &t->data_vfx);
    return;
  case AssetPrefabTrait_Decal:
    setup_vfx_decal(ctx, &t->data_decal);
    return;
  case AssetPrefabTrait_Sound:
    setup_sound(ctx, &t->data_sound);
    return;
  case AssetPrefabTrait_LightPoint:
    setup_light_point(ctx, &t->data_lightPoint);
    return;
  case AssetPrefabTrait_LightDir:
    setup_light_dir(ctx, &t->data_lightDir);
    return;
  case AssetPrefabTrait_LightAmbient:
    setup_light_ambient(ctx, &t->data_lightAmbient);
    return;
  case AssetPrefabTrait_Lifetime:
    setup_lifetime(ctx, &t->data_lifetime);
    return;
  case AssetPrefabTrait_Movement:
    setup_movement(ctx, &t->data_movement);
    return;
  case AssetPrefabTrait_Footstep:
    setup_footstep(ctx, &t->data_footstep);
    return;
  case AssetPrefabTrait_Health:
    setup_health(ctx, &t->data_health);
    return;
  case AssetPrefabTrait_Attack:
    setup_attack(ctx, &t->data_attack);
    return;
  case AssetPrefabTrait_Collision:
    setup_collision(ctx, &t->data_collision);
    return;
  case AssetPrefabTrait_Script:
    setup_script(ctx, &t->data_script);
    return;
  case AssetPrefabTrait_Bark:
    setup_bark(ctx, &t->data_bark);
    return;
  case AssetPrefabTrait_Location:
    setup_location(ctx, &t->data_location);
    return;
  case AssetPrefabTrait_Status:
    setup_status(ctx, &t->data_status);
    return;
  case AssetPrefabTrait_Vision:
    setup_vision(ctx, &t->data_vision);
    return;
  case AssetPrefabTrait_Attachment:
    setup_attachment(ctx, &t->data_attachment);
    return;
  case AssetPrefabTrait_Production:
    setup_production(ctx, &t->data_production);
    return;
  case AssetPrefabTrait_Scalable:
    setup_scale(ctx, ctx->spec->scale);
    return;
  case AssetPrefabTrait_Count:
    break;
  }
  diag_crash_msg("Unsupported prefab trait: '{}'", fmt_int(t->type));
}

static bool setup_prefab(
    EcsWorld*                 world,
    const AssetPrefabMapComp* prefabMap,
    SceneNavEnvComp*          navEnv,
    const SceneTerrainComp*   terrain,
    const EcsEntityId         e,
    const ScenePrefabSpec*    spec) {

  if ((spec->flags & ScenePrefabFlags_SnapToTerrain) && !scene_terrain_loaded(terrain)) {
    return false; // Wait until the terrain is loaded.
  }

  diag_assert_msg(spec->prefabId, "Invalid prefab id: {}", string_hash_fmt(spec->prefabId));
  ScenePrefabInstanceComp* instanceComp = ecs_world_add_t(
      world,
      e,
      ScenePrefabInstanceComp,
      .id       = spec->id,
      .prefabId = spec->prefabId,
      .variant  = spec->variant);

  const AssetPrefab* prefab = asset_prefab_find(prefabMap, spec->prefabId);
  if (UNLIKELY(!prefab)) {
    log_e("Prefab not found", log_param("entity", ecs_entity_fmt(e)));
    return true; // No point in retrying; mark the prefab as done.
  }
  instanceComp->assetHash  = prefab->hash;
  instanceComp->assetFlags = prefab->flags;
  if (prefab_is_volatile(prefab, spec)) {
    instanceComp->isVolatile = true;
  }
  GeoVector spawnPos = spec->position;
  if (spec->flags & ScenePrefabFlags_SnapToTerrain) {
    scene_terrain_snap(terrain, &spawnPos);
  }
  prefab_validate_pos(spawnPos);
  ecs_world_add_empty_t(world, e, SceneLevelInstanceComp);
  ecs_world_add_t(world, e, SceneTransformComp, .position = spawnPos, .rotation = spec->rotation);

  switch (spec->variant) {
  case ScenePrefabVariant_Normal:
    ecs_world_add_t(world, e, SceneVelocityComp);
    ecs_world_add_t(world, e, SceneTagComp, .tags = SceneTags_Default);
    scene_debug_init(world, e);

    if (prefab->flags & AssetPrefabFlags_Unit) {
      ecs_world_add_t(world, e, SceneVisibilityComp);
      ecs_world_add_t(world, e, SceneHealthStatsComp);
    }
    break;
  case ScenePrefabVariant_Edit:
    ecs_world_add_t(world, e, SceneTagComp, .tags = SceneTags_Default);
    break;
  default:
    break;
  }

  if (spec->faction != SceneFaction_None) {
    ecs_world_add_t(world, e, SceneFactionComp, .id = spec->faction);
  }
  if (!mem_all(mem_var(spec->sets), 0)) {
    scene_set_member_create(world, e, spec->sets, array_elems(spec->sets));
  }

  PrefabSetupContext ctx = {
      .world     = world,
      .navEnv    = navEnv,
      .prefabMap = prefabMap,
      .prefab    = prefab,
      .entity    = e,
      .spec      = spec,
  };

  const u64 traitMask = g_prefabVariantTraitMask[spec->variant];
  for (u16 i = 0; i != prefab->traitCount; ++i) {
    const AssetPrefabTrait* trait = &prefabMap->traits.values[prefab->traitIndex + i];
    if (traitMask & (u64_lit(1) << trait->type)) {
      setup_trait(&ctx, trait);
    }
  }

  // NOTE: Set instance properties after the traits as it should override in case of conflicts.
  if (spec->propertyCount) {
    diag_assert(spec->properties);
    if (!ctx.propComp) {
      ctx.propComp = scene_prop_add(world, e);
    }
    for (u16 i = 0; i != spec->propertyCount; ++i) {
      scene_prop_store(ctx.propComp, spec->properties[i].key, spec->properties[i].value);
    }
  }

  return true; // Prefab done processing.
}

ecs_system_define(ScenePrefabSpawnSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalSpawnView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return; // Global dependencies not ready.
  }
  ScenePrefabEnvComp*     prefabEnv = ecs_view_write_t(globalItr, ScenePrefabEnvComp);
  SceneNavEnvComp*        navEnv    = ecs_view_write_t(globalItr, SceneNavEnvComp);
  const SceneTerrainComp* terrain   = ecs_view_read_t(globalItr, SceneTerrainComp);

  EcsView*     mapAssetView = ecs_world_view_t(world, PrefabMapAssetView);
  EcsIterator* mapAssetItr  = ecs_view_maybe_at(mapAssetView, prefabEnv->mapEntity);
  if (!mapAssetItr) {
    return; // Map asset is being loaded.
  }
  const AssetPrefabMapComp* map = ecs_view_read_t(mapAssetItr, AssetPrefabMapComp);

  // Process global requests.
  for (usize i = prefabEnv->requests.size; i-- != 0;) {
    ScenePrefabRequest* req = dynarray_at_t(&prefabEnv->requests, i, ScenePrefabRequest);
    if (req->entityReset) {
      ecs_world_entity_reset(world, req->entity);
      req->entityReset = false;
      continue; // Wait for the reset to take effect.
    }
    if (setup_prefab(world, map, navEnv, terrain, req->entity, &req->spec)) {
      prefab_spec_destroy(&req->spec, g_allocHeap);
      dynarray_remove_unordered(&prefabEnv->requests, i, 1);
    }
  }

  // Process entity requests.
  EcsView* spawnView = ecs_world_view_t(world, PrefabSpawnView);
  for (EcsIterator* itr = ecs_view_itr(spawnView); ecs_view_walk(itr);) {
    const EcsEntityId             entity  = ecs_view_entity(itr);
    const ScenePrefabRequestComp* request = ecs_view_read_t(itr, ScenePrefabRequestComp);

    if (setup_prefab(world, map, navEnv, terrain, entity, &request->spec)) {
      ecs_world_remove_t(world, entity, ScenePrefabRequestComp);
    }
  }
}

ecs_module_init(scene_prefab_module) {
  ecs_register_comp(ScenePrefabEnvComp, .destructor = ecs_destruct_prefab_env);
  ecs_register_comp(ScenePrefabRequestComp, .destructor = ecs_destruct_prefab_request);
  ecs_register_comp(ScenePrefabInstanceComp);

  ecs_register_view(GlobalResourceUpdateView);
  ecs_register_view(GlobalRefreshView);
  ecs_register_view(GlobalSpawnView);
  ecs_register_view(InstanceRefreshView);
  ecs_register_view(InstanceLayerUpdateView);
  ecs_register_view(PrefabMapAssetView);
  ecs_register_view(PrefabSpawnView);

  ecs_register_system(ScenePrefabResourceUpdateSys, ecs_view_id(GlobalResourceUpdateView));

  ecs_register_system(
      ScenePrefabInstanceRefreshSys,
      ecs_view_id(GlobalRefreshView),
      ecs_view_id(PrefabMapAssetView),
      ecs_view_id(InstanceRefreshView));
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
  ecs_world_add_t(world, e, ScenePrefabRequestComp, .spec = prefab_spec_dup(spec, g_allocHeap));
  return e;
}

void scene_prefab_spawn_onto(
    ScenePrefabEnvComp* env, const ScenePrefabSpec* spec, const EcsEntityId e) {
  diag_assert_msg(spec->prefabId, "Invalid prefab id: {}", string_hash_fmt(spec->prefabId));

  if (prefab_request_find(env, e)) {
    log_w("Duplicate prefab request", log_param("entity", ecs_entity_fmt(e)));
    return;
  }

  *dynarray_push_t(&env->requests, ScenePrefabRequest) = (ScenePrefabRequest){
      .spec   = prefab_spec_dup(spec, g_allocHeap),
      .entity = e,
  };
}

void scene_prefab_spawn_replace(
    ScenePrefabEnvComp* env, const ScenePrefabSpec* spec, const EcsEntityId e) {
  diag_assert_msg(spec->prefabId, "Invalid prefab id: {}", string_hash_fmt(spec->prefabId));

  if (prefab_request_find(env, e)) {
    log_w("Duplicate prefab request", log_param("entity", ecs_entity_fmt(e)));
    return;
  }

  *dynarray_push_t(&env->requests, ScenePrefabRequest) = (ScenePrefabRequest){
      .spec        = prefab_spec_dup(spec, g_allocHeap),
      .entityReset = true,
      .entity      = e,
  };
}
