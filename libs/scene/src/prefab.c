#include "asset_manager.h"
#include "asset_prefab.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_float.h"
#include "core_math.h"
#include "core_rng.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "scene_attack.h"
#include "scene_blink.h"
#include "scene_brain.h"
#include "scene_collision.h"
#include "scene_explosive.h"
#include "scene_footstep.h"
#include "scene_health.h"
#include "scene_lifetime.h"
#include "scene_location.h"
#include "scene_locomotion.h"
#include "scene_name.h"
#include "scene_nav.h"
#include "scene_prefab.h"
#include "scene_renderable.h"
#include "scene_sound.h"
#include "scene_spawner.h"
#include "scene_status.h"
#include "scene_tag.h"
#include "scene_target.h"
#include "scene_taunt.h"
#include "scene_terrain.h"
#include "scene_transform.h"
#include "scene_unit.h"
#include "scene_vfx.h"
#include "scene_visibility.h"

typedef enum {
  PrefabResource_MapAcquired  = 1 << 0,
  PrefabResource_MapUnloading = 1 << 1,
} PrefabResourceFlags;

ecs_comp_define(ScenePrefabResourceComp) {
  PrefabResourceFlags flags;
  String              mapId;
  EcsEntityId         mapEntity;
  u32                 mapVersion;
};

ecs_comp_define(ScenePrefabRequestComp) { ScenePrefabSpec spec; };
ecs_comp_define_public(ScenePrefabInstanceComp);

static void ecs_destruct_prefab_resource(void* data) {
  ScenePrefabResourceComp* comp = data;
  string_free(g_alloc_heap, comp->mapId);
}

ecs_view_define(GlobalResourceUpdateView) {
  ecs_access_write(ScenePrefabResourceComp);
  ecs_access_write(AssetManagerComp);
}

ecs_view_define(GlobalSpawnView) {
  ecs_access_maybe_read(SceneTerrainComp);
  ecs_access_read(ScenePrefabResourceComp);
}

ecs_view_define(PrefabMapAssetView) { ecs_access_read(AssetPrefabMapComp); }
ecs_view_define(PrefabSpawnView) { ecs_access_read(ScenePrefabRequestComp); }

ecs_system_define(ScenePrefabResourceInitSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalResourceUpdateView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  AssetManagerComp*        assets   = ecs_view_write_t(globalItr, AssetManagerComp);
  ScenePrefabResourceComp* resource = ecs_view_write_t(globalItr, ScenePrefabResourceComp);

  if (!resource->mapEntity) {
    resource->mapEntity = asset_lookup(world, assets, resource->mapId);
  }

  if (!(resource->flags & (PrefabResource_MapAcquired | PrefabResource_MapUnloading))) {
    asset_acquire(world, resource->mapEntity);
    resource->flags |= PrefabResource_MapAcquired;
    ++resource->mapVersion;

    log_i(
        "Acquiring prefab-map",
        log_param("id", fmt_text(resource->mapId)),
        log_param("version", fmt_int(resource->mapVersion)));
  }
}

ecs_system_define(ScenePrefabResourceUnloadChangedSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalResourceUpdateView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  ScenePrefabResourceComp* resource = ecs_view_write_t(globalItr, ScenePrefabResourceComp);
  if (!resource->mapEntity) {
    return;
  }

  const bool isLoaded   = ecs_world_has_t(world, resource->mapEntity, AssetLoadedComp);
  const bool isFailed   = ecs_world_has_t(world, resource->mapEntity, AssetFailedComp);
  const bool hasChanged = ecs_world_has_t(world, resource->mapEntity, AssetChangedComp);

  if (resource->flags & PrefabResource_MapAcquired && (isLoaded || isFailed) && hasChanged) {
    log_i(
        "Unloading prefab-map",
        log_param("id", fmt_text(resource->mapId)),
        log_param("reason", fmt_text_lit("Asset changed")));

    asset_release(world, resource->mapEntity);
    resource->flags &= ~PrefabResource_MapAcquired;
    resource->flags |= PrefabResource_MapUnloading;
  }
  if (resource->flags & PrefabResource_MapUnloading && !isLoaded) {
    resource->flags &= ~PrefabResource_MapUnloading;
  }
}

static SceneLayer prefab_instance_layer(const AssetPrefabFlags flags, const SceneFaction faction) {
  if (flags & AssetPrefabFlags_Infantry) {
    switch (faction) {
    case SceneFaction_A:
      return SceneLayer_InfantryFactionA;
    case SceneFaction_B:
      return SceneLayer_InfantryFactionB;
    case SceneFaction_C:
      return SceneLayer_InfantryFactionC;
    case SceneFaction_D:
      return SceneLayer_InfantryFactionD;
    case SceneFaction_None:
      return SceneLayer_InfantryFactionNone;
    default:
      UNREACHABLE
    }
  }
  if (flags & AssetPrefabFlags_Structure) {
    switch (faction) {
    case SceneFaction_A:
      return SceneLayer_StructureFactionA;
    case SceneFaction_B:
      return SceneLayer_StructureFactionB;
    case SceneFaction_C:
      return SceneLayer_StructureFactionC;
    case SceneFaction_D:
      return SceneLayer_StructureFactionD;
    case SceneFaction_None:
      return SceneLayer_StructureFactionNone;
    default:
      UNREACHABLE
    }
  }
  if (flags & AssetPrefabFlags_Destructible) {
    return SceneLayer_Destructible;
  }
  return SceneLayer_Environment;
}

static void setup_name(EcsWorld* w, EcsEntityId e, const AssetPrefabTraitName* t) {
  ecs_world_add_t(w, e, SceneNameComp, .name = t->name);
}

static void setup_renderable(EcsWorld* w, EcsEntityId e, const AssetPrefabTraitRenderable* t) {
  ecs_world_add_t(w, e, SceneRenderableComp, .graphic = t->graphic, .alpha = 1.0f);
}

static void setup_vfx_system(EcsWorld* w, EcsEntityId e, const AssetPrefabTraitVfx* t) {
  ecs_world_add_t(w, e, SceneVfxSystemComp, .asset = t->asset, .alpha = 1.0f);
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

static void setup_lifetime(EcsWorld* w, EcsEntityId e, const AssetPrefabTraitLifetime* t) {
  ecs_world_add_t(w, e, SceneLifetimeDurationComp, .duration = t->duration);
}

static void setup_movement(EcsWorld* w, const EcsEntityId e, const AssetPrefabTraitMovement* t) {
  ecs_world_add_t(
      w,
      e,
      SceneLocomotionComp,
      .maxSpeed         = t->speed,
      .rotationSpeedRad = t->rotationSpeedRad,
      .radius           = t->radius,
      .moveAnimation    = t->moveAnimation);

  scene_nav_add_agent(w, e);
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

  ecs_world_add_t(w, e, SceneDamageComp);
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
        .aimJoint    = t->aimJoint,
        .aimSpeedRad = t->aimSpeedRad,
        .aimRotLocal = geo_quat_ident);
  }
  if (t->aimSoundAsset) {
    ecs_world_add_t(w, e, SceneAttackSoundComp, .aimSoundAsset = t->aimSoundAsset);
  }
  SceneTargetFlags flags = 0;
  if (t->targetExcludeUnreachable) {
    flags |= SceneTarget_ConfigExcludeUnreachable;
  }
  if (t->targetExcludeObscured) {
    flags |= SceneTarget_ConfigExcludeObscured;
  }
  ecs_world_add_t(
      w,
      e,
      SceneTargetFinderComp,
      .flags             = flags,
      .distanceMin       = t->targetDistanceMin,
      .distanceMax       = t->targetDistanceMax,
      .lineOfSightRadius = t->targetLineOfSightRadius);
  ecs_world_add_t(w, e, SceneDamageStatsComp);
}

static void setup_collision(
    EcsWorld*                        w,
    const EcsEntityId                e,
    const ScenePrefabSpec*           s,
    const AssetPrefab*               p,
    const AssetPrefabTraitCollision* t) {
  if (t->navBlocker) {
    scene_nav_add_blocker(w, e);
  }
  const SceneLayer layer = prefab_instance_layer(p->flags, s->faction);
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

static void setup_brain(EcsWorld* w, const EcsEntityId e, const AssetPrefabTraitBrain* t) {
  scene_brain_add(w, e, t->behavior);
}

static void setup_spawner(EcsWorld* w, const EcsEntityId e, const AssetPrefabTraitSpawner* t) {
  ecs_world_add_t(
      w,
      e,
      SceneSpawnerComp,
      .prefabId     = t->prefabId,
      .radius       = t->radius,
      .count        = t->count,
      .maxInstances = t->maxInstances,
      .intervalMin  = t->intervalMin,
      .intervalMax  = t->intervalMax);
}

static void setup_blink(EcsWorld* w, const EcsEntityId e, const AssetPrefabTraitBlink* t) {
  ecs_world_add_t(w, e, SceneBlinkComp, .frequency = t->frequency, .effectPrefab = t->effectPrefab);
}

static void setup_taunt(EcsWorld* w, const EcsEntityId e, const AssetPrefabTraitTaunt* t) {
  ecs_world_add_t(
      w,
      e,
      SceneTauntComp,
      .priority                             = t->priority,
      .tauntPrefabs[SceneTauntType_Death]   = t->tauntDeathPrefab,
      .tauntPrefabs[SceneTauntType_Confirm] = t->tauntConfirmPrefab);
}

static void setup_location(EcsWorld* w, const EcsEntityId e, const AssetPrefabTraitLocation* t) {
  ecs_world_add_t(w, e, SceneLocationComp, .offsets[SceneLocationType_AimTarget] = t->aimTarget);
}

static void setup_explosive(EcsWorld* w, const EcsEntityId e, const AssetPrefabTraitExplosive* t) {
  ecs_world_add_t(
      w, e, SceneExplosiveComp, .delay = t->delay, .radius = t->radius, .damage = t->damage);
}

static void setup_status(EcsWorld* w, const EcsEntityId e, const AssetPrefabTraitStatus* t) {
  SceneStatusMask supported = 0;
  supported |= t->burnable ? (1 << SceneStatusType_Burning) : 0;

  ecs_world_add_t(w, e, SceneStatusComp, .supported = supported, .effectJoint = t->effectJoint);
  ecs_world_add_t(w, e, SceneStatusRequestComp);
}

static void setup_vision(EcsWorld* w, const EcsEntityId e, const AssetPrefabTraitVision* t) {
  ecs_world_add_t(w, e, SceneVisionComp, .radius = t->radius);
}

static void setup_scale(EcsWorld* w, const EcsEntityId e, const f32 scale) {
  ecs_world_add_t(w, e, SceneScaleComp, .scale = UNLIKELY(scale < f32_epsilon) ? 1.0 : scale);
}

static void setup_trait(
    EcsWorld*               w,
    const EcsEntityId       e,
    const ScenePrefabSpec*  s,
    const AssetPrefab*      p,
    const AssetPrefabTrait* t) {
  switch (t->type) {
  case AssetPrefabTrait_Name:
    setup_name(w, e, &t->data_name);
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
  case AssetPrefabTrait_Lifetime:
    setup_lifetime(w, e, &t->data_lifetime);
    return;
  case AssetPrefabTrait_Movement:
    setup_movement(w, e, &t->data_movement);
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
  case AssetPrefabTrait_Brain:
    setup_brain(w, e, &t->data_brain);
    return;
  case AssetPrefabTrait_Spawner:
    setup_spawner(w, e, &t->data_spawner);
    return;
  case AssetPrefabTrait_Blink:
    setup_blink(w, e, &t->data_blink);
    return;
  case AssetPrefabTrait_Taunt:
    setup_taunt(w, e, &t->data_taunt);
    return;
  case AssetPrefabTrait_Location:
    setup_location(w, e, &t->data_location);
    return;
  case AssetPrefabTrait_Explosive:
    setup_explosive(w, e, &t->data_explosive);
    return;
  case AssetPrefabTrait_Status:
    setup_status(w, e, &t->data_status);
    return;
  case AssetPrefabTrait_Vision:
    setup_vision(w, e, &t->data_vision);
    return;
  case AssetPrefabTrait_Scalable:
    setup_scale(w, e, s->scale);
    return;
  case AssetPrefabTrait_Count:
    break;
  }
  diag_crash_msg("Unsupported prefab trait: '{}'", fmt_int(t->type));
}

static void setup_prefab(
    EcsWorld*                 w,
    const SceneTerrainComp*   terrain,
    const EcsEntityId         e,
    const ScenePrefabSpec*    spec,
    const AssetPrefabMapComp* map) {

  ScenePrefabInstanceComp* instanceComp =
      ecs_world_add_t(w, e, ScenePrefabInstanceComp, .id = spec->id, .prefabId = spec->prefabId);

  const AssetPrefab* prefab = asset_prefab_get(map, spec->prefabId);
  if (UNLIKELY(!prefab)) {
    log_e("Prefab not found", log_param("entity", fmt_int(e, .base = 16)));
    return;
  }
  instanceComp->isVolatile = (prefab->flags & AssetPrefabFlags_Volatile) != 0;

  GeoVector spawnPos = spec->position;
  if (spec->flags & ScenePrefabFlags_SnapToTerrain) {
    scene_terrain_snap(terrain, &spawnPos);
  }
  ecs_world_add_t(w, e, SceneTransformComp, .position = spawnPos, .rotation = spec->rotation);
  ecs_world_add_t(w, e, SceneVelocityComp);

  SceneTagComp* tagComp = ecs_world_add_t(w, e, SceneTagComp, .tags = SceneTags_Default);
  if (prefab->flags & (AssetPrefabFlags_Infantry | AssetPrefabFlags_Structure)) {
    ecs_world_add_empty_t(w, e, SceneUnitComp);
    if (prefab->flags & AssetPrefabFlags_Infantry) {
      ecs_world_add_empty_t(w, e, SceneUnitInfantryComp);
    } else if (prefab->flags & AssetPrefabFlags_Structure) {
      ecs_world_add_empty_t(w, e, SceneUnitStructureComp);
    }
    ecs_world_add_t(w, e, SceneVisibilityComp);
    tagComp->tags |= SceneTags_Unit;
  }

  if (spec->faction != SceneFaction_None) {
    ecs_world_add_t(w, e, SceneFactionComp, .id = spec->faction);
  }

  for (u16 i = 0; i != prefab->traitCount; ++i) {
    const AssetPrefabTrait* trait = &map->traits[prefab->traitIndex + i];
    setup_trait(w, e, spec, prefab, trait);
  }
}

ecs_system_define(ScenePrefabSpawnSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalSpawnView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const ScenePrefabResourceComp* resource = ecs_view_read_t(globalItr, ScenePrefabResourceComp);
  const SceneTerrainComp*        terrain  = ecs_view_read_t(globalItr, SceneTerrainComp);

  EcsView*     mapAssetView = ecs_world_view_t(world, PrefabMapAssetView);
  EcsIterator* mapAssetItr  = ecs_view_maybe_at(mapAssetView, resource->mapEntity);
  if (!mapAssetItr) {
    return;
  }
  const AssetPrefabMapComp* map = ecs_view_read_t(mapAssetItr, AssetPrefabMapComp);

  EcsView* spawnView = ecs_world_view_t(world, PrefabSpawnView);
  for (EcsIterator* itr = ecs_view_itr(spawnView); ecs_view_walk(itr);) {
    const EcsEntityId             entity  = ecs_view_entity(itr);
    const ScenePrefabRequestComp* request = ecs_view_read_t(itr, ScenePrefabRequestComp);

    if (!scene_terrain_loaded(terrain) && (request->spec.flags & ScenePrefabFlags_SnapToTerrain)) {
      continue; // Wait until the terrain is loaded.
    }

    setup_prefab(world, terrain, entity, &request->spec, map);
    ecs_world_remove_t(world, entity, ScenePrefabRequestComp);
  }
}

ecs_module_init(scene_prefab_module) {
  ecs_register_comp(ScenePrefabResourceComp, .destructor = ecs_destruct_prefab_resource);
  ecs_register_comp(ScenePrefabRequestComp);
  ecs_register_comp(ScenePrefabInstanceComp);

  ecs_register_view(GlobalResourceUpdateView);
  ecs_register_view(GlobalSpawnView);
  ecs_register_view(PrefabMapAssetView);
  ecs_register_view(PrefabSpawnView);

  ecs_register_system(ScenePrefabResourceInitSys, ecs_view_id(GlobalResourceUpdateView));

  ecs_register_system(ScenePrefabResourceUnloadChangedSys, ecs_view_id(GlobalResourceUpdateView));

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
      ScenePrefabResourceComp,
      .mapId = string_dup(g_alloc_heap, prefabMapId));
}

EcsEntityId scene_prefab_map(const ScenePrefabResourceComp* resource) {
  return resource->mapEntity;
}

u32 scene_prefab_map_version(const ScenePrefabResourceComp* resource) {
  return resource->mapVersion;
}

EcsEntityId scene_prefab_spawn(EcsWorld* world, const ScenePrefabSpec* spec) {
  const EcsEntityId e = ecs_world_entity_create(world);
  ecs_world_add_t(world, e, ScenePrefabRequestComp, .spec = *spec);
  return e;
}
