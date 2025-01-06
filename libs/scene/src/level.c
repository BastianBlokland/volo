#include "asset_manager.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_math.h"
#include "core_rng.h"
#include "ecs_entity.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "scene_faction.h"
#include "scene_level.h"
#include "scene_prefab.h"
#include "scene_property.h"
#include "scene_set.h"
#include "scene_transform.h"
#include "script_mem.h"
#include "trace_tracer.h"

typedef enum {
  LevelLoadState_Start,
  LevelLoadState_Unload,
  LevelLoadState_AssetAcquire,
  LevelLoadState_AssetWait,
  LevelLoadState_Create,
} LevelLoadState;

ecs_comp_define(SceneLevelManagerComp) {
  bool           isLoading;
  u32            loadCounter;
  SceneLevelMode levelMode;
  EcsEntityId    levelAsset;
  String         levelName;
  EcsEntityId    levelTerrain;
  AssetLevelFog  levelFog;
  GeoVector      levelStartpoint;
};

ecs_comp_define_public(SceneLevelInstanceComp);

ecs_comp_define(SceneLevelRequestLoadComp) {
  SceneLevelMode levelMode;
  EcsEntityId    levelAsset; // 0 indicates reloading the current level.
  LevelLoadState state;
};

ecs_comp_define(SceneLevelRequestUnloadComp);
ecs_comp_define(SceneLevelRequestSaveComp) { EcsEntityId levelAsset; };

static const String g_levelModeNames[] = {
    string_static("Play"),
    string_static("Edit"),
};
ASSERT(array_elems(g_levelModeNames) == SceneLevelMode_Count, "Incorrect number of names");

static void ecs_destruct_level_manager_comp(void* data) {
  SceneLevelManagerComp* comp = data;
  string_maybe_free(g_allocHeap, comp->levelName);
}

static i8 level_compare_object_id(const void* a, const void* b) {
  return compare_u32(field_ptr(a, AssetLevelObject, id), field_ptr(b, AssetLevelObject, id));
}

static AssetLevelFaction scene_to_asset_faction(const SceneFaction sceneFaction) {
  switch (sceneFaction) {
  case SceneFaction_A:
    return AssetLevelFaction_A;
  case SceneFaction_B:
    return AssetLevelFaction_B;
  case SceneFaction_C:
    return AssetLevelFaction_C;
  case SceneFaction_D:
    return AssetLevelFaction_D;
  case SceneFaction_None:
    return AssetLevelFaction_None;
  default:
    UNREACHABLE
  }
}

static SceneFaction scene_from_asset_faction(const AssetLevelFaction assetFaction) {
  switch (assetFaction) {
  case AssetLevelFaction_A:
    return SceneFaction_A;
  case AssetLevelFaction_B:
    return SceneFaction_B;
  case AssetLevelFaction_C:
    return SceneFaction_C;
  case AssetLevelFaction_D:
    return SceneFaction_D;
  case AssetLevelFaction_None:
    return SceneFaction_None;
  default:
    UNREACHABLE
  }
}

static bool scene_prop_is_persistable(const ScriptVal val) {
  switch (script_type(val)) {
  case ScriptType_Num:
  case ScriptType_Bool:
  case ScriptType_Vec3:
  case ScriptType_Quat:
  case ScriptType_Color:
  case ScriptType_Str:
    return true;
  case ScriptType_Null:
  case ScriptType_Entity:
    return false;
  case ScriptType_Count:
    break;
  }
  UNREACHABLE
}

static u32 scene_prop_count_persistable(const ScenePropertyComp* c) {
  const ScriptMem* memory = scene_prop_memory(c);
  u32              res    = 0;
  for (ScriptMemItr itr = script_mem_begin(memory); itr.key; itr = script_mem_next(memory, itr)) {
    res += scene_prop_is_persistable(script_mem_load(memory, itr.key));
  }
  return res;
}

ecs_view_define(InstanceView) {
  ecs_access_with(SceneLevelInstanceComp);
  ecs_access_maybe_read(SceneFactionComp);
  ecs_access_maybe_read(ScenePrefabInstanceComp);
  ecs_access_maybe_read(ScenePropertyComp);
  ecs_access_maybe_read(SceneScaleComp);
  ecs_access_maybe_read(SceneSetMemberComp);
  ecs_access_maybe_read(SceneTransformComp);
}

static void
scene_level_process_unload(EcsWorld* world, SceneLevelManagerComp* manager, EcsView* instanceView) {
  trace_begin("level_unload", TraceColor_White);

  u32 unloadedObjectCount = 0;
  for (EcsIterator* itr = ecs_view_itr(instanceView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_world_entity_destroy(world, entity);
    ++unloadedObjectCount;
  }

  string_maybe_free(g_allocHeap, manager->levelName);

  manager->levelMode       = SceneLevelMode_Play;
  manager->levelAsset      = 0;
  manager->levelName       = string_empty;
  manager->levelTerrain    = 0;
  manager->levelFog        = AssetLevelFog_Disabled;
  manager->levelStartpoint = geo_vector(0);

  trace_end();

  log_i("Level unloaded", log_param("objects", fmt_int(unloadedObjectCount)));
}

static ScenePrefabVariant scene_level_prefab_variant(const SceneLevelMode levelMode) {
  switch (levelMode) {
  case SceneLevelMode_Play:
    return ScenePrefabVariant_Normal;
  case SceneLevelMode_Edit:
    return ScenePrefabVariant_Edit;
  case SceneLevelMode_Count:
    break;
  }
  diag_crash();
}

static void scene_level_process_load(
    EcsWorld*              world,
    SceneLevelManagerComp* manager,
    AssetManagerComp*      assets,
    const SceneLevelMode   levelMode,
    const EcsEntityId      levelAsset,
    const AssetLevel*      level) {
  diag_assert(!ecs_entity_valid(manager->levelAsset));
  diag_assert(string_is_empty(manager->levelName));
  diag_assert(!ecs_entity_valid(manager->levelTerrain));

  trace_begin("level_load", TraceColor_White);

  const ScenePrefabVariant prefabVariant = scene_level_prefab_variant(levelMode);
  heap_array_for_t(level->objects, AssetLevelObject, obj) {
    ScenePrefabKnowledge knowledge[128];
    const u16 knowledgeCount = (u16)math_min(obj->properties.count, array_elems(knowledge));
    for (u16 i = 0; i != knowledgeCount; ++i) {
      const AssetProperty* prop = &obj->properties.values[i];
      knowledge[i].key          = prop->name;
      switch (prop->type) {
      case AssetPropertyType_Num:
        knowledge[i].value = script_num(prop->data_num);
        continue;
      case AssetPropertyType_Bool:
        knowledge[i].value = script_bool(prop->data_bool);
        continue;
      case AssetPropertyType_Vec3:
        knowledge[i].value = script_vec3(prop->data_vec3);
        continue;
      case AssetPropertyType_Quat:
        knowledge[i].value = script_quat(prop->data_quat);
        continue;
      case AssetPropertyType_Color:
        knowledge[i].value = script_color(prop->data_color);
        continue;
      case AssetPropertyType_Str:
        knowledge[i].value = script_str_or_null(prop->data_str);
        continue;
      case AssetPropertyType_Count:
        break;
      }
      UNREACHABLE
    }
    ScenePrefabSpec spec = {
        .id             = obj->id,
        .prefabId       = obj->prefab,
        .variant        = prefabVariant,
        .position       = obj->position,
        .rotation       = obj->rotation,
        .scale          = obj->scale,
        .faction        = scene_from_asset_faction(obj->faction),
        .knowledge      = knowledge,
        .knowledgeCount = knowledgeCount,
    };
    mem_cpy(mem_var(spec.sets), mem_var(obj->sets));
    scene_prefab_spawn(world, &spec);
  }

  manager->levelMode       = levelMode;
  manager->levelAsset      = levelAsset;
  manager->levelName       = string_maybe_dup(g_allocHeap, level->name);
  manager->levelStartpoint = level->startpoint;
  manager->levelFog        = level->fogMode;
  if (!string_is_empty(level->terrainId)) {
    manager->levelTerrain = asset_lookup(world, assets, level->terrainId);
  }

  trace_end();

  log_i(
      "Level loaded",
      log_param("mode", fmt_text(g_levelModeNames[levelMode])),
      log_param("name", fmt_text(level->name)),
      log_param("terrain", fmt_text(level->terrainId)),
      log_param("objects", fmt_int(level->objects.count)));
}

ecs_view_define(LoadGlobalView) {
  ecs_access_maybe_write(SceneLevelManagerComp);
  ecs_access_write(AssetManagerComp);
}
ecs_view_define(LoadAssetView) {
  ecs_access_read(AssetComp);
  ecs_access_maybe_read(AssetLevelComp);
}
ecs_view_define(LoadRequestView) { ecs_access_write(SceneLevelRequestLoadComp); }

ecs_system_define(SceneLevelLoadSys) {
  EcsView*     globalView = ecs_world_view_t(world, LoadGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  AssetManagerComp*      assets  = ecs_view_write_t(globalItr, AssetManagerComp);
  SceneLevelManagerComp* manager = ecs_view_write_t(globalItr, SceneLevelManagerComp);
  if (!manager) {
    manager = ecs_world_add_t(world, ecs_world_global(world), SceneLevelManagerComp);
  }

  EcsView* requestView  = ecs_world_view_t(world, LoadRequestView);
  EcsView* assetView    = ecs_world_view_t(world, LoadAssetView);
  EcsView* instanceView = ecs_world_view_t(world, InstanceView);

  EcsIterator* assetItr = ecs_view_itr(assetView);

  for (EcsIterator* itr = ecs_view_itr(requestView); ecs_view_walk(itr);) {
    SceneLevelRequestLoadComp* req = ecs_view_write_t(itr, SceneLevelRequestLoadComp);
    switch (req->state) {
    case LevelLoadState_Start:
      if (manager->isLoading) {
        log_w("Level load already in progress");
        goto Done;
      }
      if (!req->levelAsset) {
        // levelAsset of 0 indicates that the currently loaded level should be reloaded.
        if (!manager->levelAsset) {
          log_w("Failed to reload level: No level is currently loaded");
          goto Done;
        }
        req->levelAsset = manager->levelAsset;
      }
      manager->isLoading = true;
      ++req->state;
      // Fallthrough.
    case LevelLoadState_Unload:
      scene_level_process_unload(world, manager, instanceView);
      ++req->state;
      // Fallthrough.
    case LevelLoadState_AssetAcquire:
      asset_acquire(world, req->levelAsset);
      ++req->state;
      goto Wait; // Wait for the acquire to take effect.
    case LevelLoadState_AssetWait:
      if (ecs_world_has_t(world, req->levelAsset, AssetFailedComp)) {
        ecs_view_jump(assetItr, req->levelAsset);
        const String assetId = asset_id(ecs_view_read_t(assetItr, AssetComp));
        log_e("Failed to load level asset", log_param("id", fmt_text(assetId)));
        manager->isLoading = false;
        goto Done;
      }
      if (!ecs_world_has_t(world, req->levelAsset, AssetLoadedComp)) {
        goto Wait; // Wait for asset to finish loading.
      }
      ++req->state;
      // Fallthrough.
    case LevelLoadState_Create:
      ecs_view_jump(assetItr, req->levelAsset);
      const AssetLevelComp* levelComp = ecs_view_read_t(assetItr, AssetLevelComp);
      if (!levelComp) {
        const String assetId = asset_id(ecs_view_read_t(assetItr, AssetComp));
        log_e("Invalid level asset", log_param("id", fmt_text(assetId)));
        manager->isLoading = false;
        goto Done;
      }
      scene_level_process_load(
          world, manager, assets, req->levelMode, req->levelAsset, &levelComp->level);
      manager->isLoading = false;
      ++manager->loadCounter;
      goto Done;
    }
    diag_crash_msg("Unexpected load state");
  Wait:
    continue;
  Done:
    if (req->levelAsset) {
      asset_release(world, req->levelAsset);
    }
    ecs_world_entity_destroy(world, ecs_view_entity(itr));
  }
}

ecs_view_define(UnloadGlobalView) { ecs_access_write(SceneLevelManagerComp); }
ecs_view_define(UnloadRequestView) { ecs_access_with(SceneLevelRequestUnloadComp); }

ecs_system_define(SceneLevelUnloadSys) {
  EcsView*     globalView = ecs_world_view_t(world, UnloadGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  SceneLevelManagerComp* manager = ecs_view_write_t(globalItr, SceneLevelManagerComp);

  EcsView* requestView  = ecs_world_view_t(world, UnloadRequestView);
  EcsView* instanceView = ecs_world_view_t(world, InstanceView);

  for (EcsIterator* itr = ecs_view_itr(requestView); ecs_view_walk(itr);) {
    if (manager->isLoading) {
      log_e("Level unload failed; load in progress");
    } else if (manager->levelAsset) {
      scene_level_process_unload(world, manager, instanceView);
    }
    ecs_world_entity_destroy(world, ecs_view_entity(itr));
  }
}

static void scene_level_object_push_knowledge(
    AssetLevelObject* obj, Allocator* alloc, const ScenePropertyComp* c) {
  const u32 count = scene_prop_count_persistable(c);
  if (!count) {
    return;
  }
  obj->properties.values = alloc_array_t(alloc, AssetProperty, count);
  obj->properties.count  = count;

  const ScriptMem* memory      = scene_prop_memory(c);
  u32              propertyIdx = 0;
  for (ScriptMemItr itr = script_mem_begin(memory); itr.key; itr = script_mem_next(memory, itr)) {
    const ScriptVal val = script_mem_load(memory, itr.key);
    if (!scene_prop_is_persistable(val)) {
      continue;
    }
    AssetProperty* prop = &obj->properties.values[propertyIdx++];
    prop->name          = itr.key;

    switch (script_type(val)) {
    case ScriptType_Num:
      prop->type     = AssetPropertyType_Num;
      prop->data_num = script_get_num(val, 0);
      continue;
    case ScriptType_Bool:
      prop->type      = AssetPropertyType_Bool;
      prop->data_bool = script_get_bool(val, false);
      continue;
    case ScriptType_Vec3:
      prop->type      = AssetPropertyType_Vec3;
      prop->data_vec3 = script_get_vec3(val, geo_vector(0));
      continue;
    case ScriptType_Quat:
      prop->type      = AssetPropertyType_Quat;
      prop->data_quat = script_get_quat(val, geo_quat_ident);
      continue;
    case ScriptType_Color:
      prop->type       = AssetPropertyType_Color;
      prop->data_color = script_get_color(val, geo_color_white);
      continue;
    case ScriptType_Str:
      prop->type     = AssetPropertyType_Str;
      prop->data_str = script_get_str(val, 0);
      continue;
    case ScriptType_Null:
    case ScriptType_Entity:
    case ScriptType_Count:
      break;
    }
    diag_assert_fail("Unsupported property");
    UNREACHABLE
  }
  diag_assert(propertyIdx == count);
}

static void scene_level_object_push_sets(AssetLevelObject* obj, const SceneSetMemberComp* c) {
  ASSERT(array_elems(obj->sets) >= scene_set_member_max_sets, "Insufficient set storage");
  scene_set_member_all_non_volatile(c, obj->sets);
}

static void scene_level_object_push(
    DynArray*    objects, // AssetLevelObject[], sorted on id.
    Allocator*   alloc,
    EcsIterator* instanceItr) {

  const ScenePrefabInstanceComp* prefabInst = ecs_view_read_t(instanceItr, ScenePrefabInstanceComp);
  if (!prefabInst || prefabInst->variant != ScenePrefabVariant_Edit) {
    return; // Only edit prefab instances are persisted.
  }
  if (prefabInst->isVolatile) {
    return; // Volatile prefabs should not be persisted.
  }

  const SceneTransformComp* maybeTrans     = ecs_view_read_t(instanceItr, SceneTransformComp);
  const SceneScaleComp*     maybeScale     = ecs_view_read_t(instanceItr, SceneScaleComp);
  const SceneFactionComp*   maybeFaction   = ecs_view_read_t(instanceItr, SceneFactionComp);
  const ScenePropertyComp*  maybeKnowledge = ecs_view_read_t(instanceItr, ScenePropertyComp);
  const SceneSetMemberComp* maybeSetMember = ecs_view_read_t(instanceItr, SceneSetMemberComp);
  const f32                 scaleVal       = maybeScale ? maybeScale->scale : 1.0f;

  AssetLevelObject obj = {
      .id       = prefabInst->id ? prefabInst->id : rng_sample_u32(g_rng),
      .prefab   = prefabInst->prefabId,
      .position = maybeTrans ? maybeTrans->position : geo_vector(0),
      .rotation = maybeTrans ? geo_quat_norm(maybeTrans->rotation) : geo_quat_ident,
      .scale    = scaleVal == 1.0f ? 0.0 : scaleVal, // Scale 0 is treated as unscaled (eg 1.0).
      .faction  = maybeFaction ? scene_to_asset_faction(maybeFaction->id) : AssetLevelFaction_None,
  };
  if (maybeKnowledge) {
    scene_level_object_push_knowledge(&obj, alloc, maybeKnowledge);
  }
  if (maybeSetMember) {
    scene_level_object_push_sets(&obj, maybeSetMember);
  }

  // Guarantee unique object id.
  while (dynarray_search_binary(objects, level_compare_object_id, &obj)) {
    obj.id = rng_sample_u32(g_rng);
  }

  // Insert sorted on object id.
  *dynarray_insert_sorted_t(objects, AssetLevelObject, level_compare_object_id, &obj) = obj;
}

static String scene_asset_id(EcsView* assetView, const EcsEntityId assetEntity) {
  EcsIterator* itr = ecs_view_maybe_at(assetView, assetEntity);
  return itr ? asset_id(ecs_view_read_t(itr, AssetComp)) : string_empty;
}

static void scene_level_process_save(
    const SceneLevelManagerComp* manager,
    AssetManagerComp*            assets,
    EcsView*                     assetView,
    const String                 id,
    EcsView*                     instanceView) {
  Allocator* tempAlloc = alloc_chunked_create(g_allocHeap, alloc_bump_create, usize_mebibyte);

  DynArray objects = dynarray_create_t(g_allocHeap, AssetLevelObject, 1024);
  for (EcsIterator* itr = ecs_view_itr(instanceView); ecs_view_walk(itr);) {
    scene_level_object_push(&objects, tempAlloc, itr);
  }

  const AssetLevel level = {
      .name           = manager->levelName,
      .terrainId      = scene_asset_id(assetView, manager->levelTerrain),
      .startpoint     = manager->levelStartpoint,
      .fogMode        = manager->levelFog,
      .objects.values = dynarray_begin_t(&objects, AssetLevelObject),
      .objects.count  = objects.size,
  };
  asset_level_save(assets, id, &level);

  log_i("Level saved", log_param("id", fmt_text(id)), log_param("objects", fmt_int(objects.size)));

  dynarray_destroy(&objects);
  alloc_chunked_destroy(tempAlloc);
}

ecs_view_define(SaveGlobalView) {
  ecs_access_write(AssetManagerComp);
  ecs_access_read(SceneLevelManagerComp);
}
ecs_view_define(SaveAssetView) { ecs_access_read(AssetComp); }
ecs_view_define(SaveRequestView) { ecs_access_read(SceneLevelRequestSaveComp); }

ecs_system_define(SceneLevelSaveSys) {
  EcsView*     globalView = ecs_world_view_t(world, SaveGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneLevelManagerComp* manager = ecs_view_read_t(globalItr, SceneLevelManagerComp);
  AssetManagerComp*            assets  = ecs_view_write_t(globalItr, AssetManagerComp);

  EcsView* requestView  = ecs_world_view_t(world, SaveRequestView);
  EcsView* assetView    = ecs_world_view_t(world, SaveAssetView);
  EcsView* instanceView = ecs_world_view_t(world, InstanceView);

  EcsIterator* assetItr = ecs_view_itr(assetView);

  for (EcsIterator* itr = ecs_view_itr(requestView); ecs_view_walk(itr);) {
    const SceneLevelRequestSaveComp* req = ecs_view_read_t(itr, SceneLevelRequestSaveComp);
    if (manager->isLoading) {
      log_e("Level save failed; load in progress");
    } else if (manager->levelMode != SceneLevelMode_Edit) {
      log_e("Level save failed; level not loaded for edit");
    } else {
      ecs_view_jump(assetItr, req->levelAsset);
      const String assetId = asset_id(ecs_view_read_t(assetItr, AssetComp));

      scene_level_process_save(manager, assets, assetView, assetId, instanceView);
    }
    ecs_world_entity_destroy(world, ecs_view_entity(itr));
  }
}

ecs_module_init(scene_level_module) {
  ecs_register_comp(SceneLevelManagerComp, .destructor = ecs_destruct_level_manager_comp);
  ecs_register_comp_empty(SceneLevelInstanceComp);
  ecs_register_comp(SceneLevelRequestLoadComp);
  ecs_register_comp_empty(SceneLevelRequestUnloadComp);
  ecs_register_comp(SceneLevelRequestSaveComp);

  ecs_register_view(InstanceView);

  ecs_register_system(
      SceneLevelLoadSys,
      ecs_view_id(InstanceView),
      ecs_register_view(LoadGlobalView),
      ecs_register_view(LoadAssetView),
      ecs_register_view(LoadRequestView));

  ecs_register_system(
      SceneLevelUnloadSys,
      ecs_view_id(InstanceView),
      ecs_register_view(UnloadGlobalView),
      ecs_register_view(UnloadRequestView));

  ecs_register_system(
      SceneLevelSaveSys,
      ecs_view_id(InstanceView),
      ecs_register_view(SaveGlobalView),
      ecs_register_view(SaveAssetView),
      ecs_register_view(SaveRequestView));
}

bool scene_level_loading(const SceneLevelManagerComp* m) { return m->isLoading; }

bool scene_level_loaded(const SceneLevelManagerComp* m) {
  return m->levelAsset != 0 && !m->isLoading;
}

SceneLevelMode scene_level_mode(const SceneLevelManagerComp* m) { return m->levelMode; }
EcsEntityId    scene_level_asset(const SceneLevelManagerComp* m) { return m->levelAsset; }
u32            scene_level_counter(const SceneLevelManagerComp* m) { return m->loadCounter; }
String         scene_level_name(const SceneLevelManagerComp* m) { return m->levelName; }

void scene_level_name_update(SceneLevelManagerComp* manager, const String name) {
  diag_assert_msg(manager->levelAsset, "Unable to update name: No level loaded");
  diag_assert_msg(name.size <= 32, "Unable to update name: Too long");

  string_maybe_free(g_allocHeap, manager->levelName);
  manager->levelName = string_maybe_dup(g_allocHeap, name);
}

EcsEntityId scene_level_terrain(const SceneLevelManagerComp* manager) {
  return manager->levelTerrain;
}

void scene_level_terrain_update(SceneLevelManagerComp* manager, const EcsEntityId terrainAsset) {
  diag_assert_msg(manager->levelAsset, "Unable to update terrain: No level loaded");
  manager->levelTerrain = terrainAsset;
}

GeoVector scene_level_startpoint(const SceneLevelManagerComp* manager) {
  return manager->levelStartpoint;
}

void scene_level_startpoint_update(SceneLevelManagerComp* manager, const GeoVector startpoint) {
  diag_assert_msg(manager->levelAsset, "Unable to update startpoint: No level loaded");
  manager->levelStartpoint = startpoint;
}

AssetLevelFog scene_level_fog(const SceneLevelManagerComp* manager) { return manager->levelFog; }

void scene_level_fog_update(SceneLevelManagerComp* manager, const AssetLevelFog fog) {
  diag_assert_msg(manager->levelAsset, "Unable to update fog: No level loaded");
  manager->levelFog = fog;
}

void scene_level_load(EcsWorld* world, const SceneLevelMode mode, const EcsEntityId levelAsset) {
  diag_assert(ecs_entity_valid(levelAsset));

  const EcsEntityId reqEntity = ecs_world_entity_create(world);
  ecs_world_add_t(
      world, reqEntity, SceneLevelRequestLoadComp, .levelMode = mode, .levelAsset = levelAsset);
}

void scene_level_reload(EcsWorld* world, const SceneLevelMode mode) {
  const EcsEntityId reqEntity = ecs_world_entity_create(world);
  ecs_world_add_t(world, reqEntity, SceneLevelRequestLoadComp, .levelMode = mode, .levelAsset = 0);
}

void scene_level_unload(EcsWorld* world) {
  const EcsEntityId reqEntity = ecs_world_entity_create(world);
  ecs_world_add_empty_t(world, reqEntity, SceneLevelRequestUnloadComp);
}

void scene_level_save(EcsWorld* world, const EcsEntityId levelAsset) {
  diag_assert(ecs_entity_valid(levelAsset));

  const EcsEntityId reqEntity = ecs_world_entity_create(world);
  ecs_world_add_t(world, reqEntity, SceneLevelRequestSaveComp, .levelAsset = levelAsset);
}
