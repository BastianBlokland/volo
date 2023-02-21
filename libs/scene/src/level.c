#include "asset_level.h"
#include "asset_manager.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_rng.h"
#include "core_stringtable.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "scene_faction.h"
#include "scene_prefab.h"
#include "scene_transform.h"

ASSERT(AssetLevelFaction_Count == SceneFaction_Count, "Mismatching faction counts");
ASSERT(AssetLevelFaction_None == SceneFaction_None, "Mismatching faction sentinel");

typedef enum {
  LevelLoadState_Start,
  LevelLoadState_Unload,
  LevelLoadState_AssetAcquire,
  LevelLoadState_AssetWait,
  LevelLoadState_Create,
} LevelLoadState;

ecs_comp_define(SceneLevelManagerComp) {
  bool   isLoading;
  String loadedLevelId;
};
ecs_comp_define(SceneLevelRequestLoadComp) {
  String         levelId;
  EcsEntityId    levelAsset;
  LevelLoadState state;
};
ecs_comp_define(SceneLevelRequestSaveComp) { String levelId; };

static void ecs_destruct_level_manager(void* data) {
  SceneLevelManagerComp* comp = data;
  string_maybe_free(g_alloc_heap, comp->loadedLevelId);
}

static void ecs_destruct_level_request_load(void* data) {
  SceneLevelRequestLoadComp* comp = data;
  string_maybe_free(g_alloc_heap, comp->levelId);
}

static void ecs_destruct_level_request_save(void* data) {
  SceneLevelRequestSaveComp* comp = data;
  string_maybe_free(g_alloc_heap, comp->levelId);
}

static i8 level_compare_object_id(const void* a, const void* b) {
  return compare_u32(field_ptr(a, AssetLevelObject, id), field_ptr(b, AssetLevelObject, id));
}

ecs_view_define(InstanceView) {
  ecs_access_maybe_read(SceneFactionComp);
  ecs_access_maybe_read(SceneTransformComp);
  ecs_access_read(ScenePrefabInstanceComp);
}

static void scene_level_process_unload(EcsWorld* world, EcsView* instView) {
  u32 objectCount = 0;
  for (EcsIterator* itr = ecs_view_itr(instView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_world_entity_destroy(world, entity);
    ++objectCount;
  }
  log_i("Level unloaded", log_param("objects", fmt_int(objectCount)));
}

static void scene_level_process_load(EcsWorld* world, const AssetLevel* level) {
  array_ptr_for_t(level->objects, AssetLevelObject, obj) {
    const StringHash prefabId    = string_hash(obj->prefab);
    const GeoVector  rotAngleRad = geo_vector_mul(obj->rotation, math_deg_to_rad);
    const GeoQuat    rot         = geo_quat_from_euler(rotAngleRad);
    scene_prefab_spawn(
        world,
        &(ScenePrefabSpec){
            .id       = obj->id,
            .prefabId = prefabId,
            .position = obj->position,
            .rotation = rot,
            .faction  = (SceneFaction)obj->faction,
        });
  }
  log_i("Level loaded", log_param("objects", fmt_int(level->objects.count)));
}

ecs_view_define(LoadGlobalView) {
  ecs_access_write(AssetManagerComp);
  ecs_access_maybe_write(SceneLevelManagerComp);
}
ecs_view_define(LoadAssetView) { ecs_access_read(AssetLevelComp); }
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
      manager->isLoading = true;
      ++req->state;
      // Fallthrough.
    case LevelLoadState_Unload:
      scene_level_process_unload(world, instanceView);
      ++req->state;
      // Fallthrough.
    case LevelLoadState_AssetAcquire:
      req->levelAsset = asset_lookup(world, assets, req->levelId);
      asset_acquire(world, req->levelAsset);
      ++req->state;
      goto Wait;
    case LevelLoadState_AssetWait:
      if (ecs_world_has_t(world, req->levelAsset, AssetFailedComp)) {
        log_e("Failed to load level asset", log_param("id", fmt_text(req->levelId)));
        manager->isLoading = false;
        goto Done;
      }
      if (!ecs_world_has_t(world, req->levelAsset, AssetLoadedComp)) {
        goto Wait; // Wait for asset to finish loading.
      }
      ++req->state;
      // Fallthrough.
    case LevelLoadState_Create:
      if (ecs_view_maybe_jump(assetItr, req->levelAsset) == null) {
        log_e("Invalid level asset", log_param("id", fmt_text(req->levelId)));
        manager->isLoading = false;
        goto Done;
      }
      const AssetLevelComp* levelComp = ecs_view_read_t(assetItr, AssetLevelComp);
      scene_level_process_load(world, &levelComp->level);
      mem_swap(mem_var(manager->loadedLevelId), mem_var(req->levelId));
      manager->isLoading = false;
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

static void scene_level_object_push(
    DynArray*    objects, // AssetLevelObject[], sorted on id.
    EcsIterator* instanceItr) {

  const ScenePrefabInstanceComp* prefabInst = ecs_view_read_t(instanceItr, ScenePrefabInstanceComp);
  const SceneTransformComp*      maybeTrans = ecs_view_read_t(instanceItr, SceneTransformComp);
  const SceneFactionComp*        maybeFaction = ecs_view_read_t(instanceItr, SceneFactionComp);

  const String prefabName = stringtable_lookup(g_stringtable, prefabInst->prefabId);
  if (string_is_empty(prefabName)) {
    log_w("Prefab name not found", log_param("prefab-id", fmt_int(prefabInst->prefabId)));
    return;
  }

  const GeoQuat   rot         = maybeTrans ? maybeTrans->rotation : geo_quat_ident;
  const GeoVector rotEulerDeg = geo_vector_mul(geo_quat_to_euler(rot), math_rad_to_deg);

  AssetLevelObject obj = {
      .id       = prefabInst->id ? prefabInst->id : rng_sample_u32(g_rng),
      .prefab   = prefabName,
      .position = maybeTrans ? maybeTrans->position : geo_vector(0),
      .rotation = rotEulerDeg,
      .faction  = (AssetLevelFaction)(maybeFaction ? maybeFaction->id : SceneFaction_None),
  };

  // Guarantee unique object id.
  while (dynarray_search_binary(objects, level_compare_object_id, &obj)) {
    obj.id = rng_sample_u32(g_rng);
  }

  // Insert sorted on object id.
  *dynarray_insert_sorted_t(objects, AssetLevelObject, level_compare_object_id, &obj) = obj;
}

static void scene_level_process_save(AssetManagerComp* assets, const String id, EcsView* instView) {
  DynArray objects = dynarray_create_t(g_alloc_heap, AssetLevelObject, 1024);
  for (EcsIterator* itr = ecs_view_itr(instView); ecs_view_walk(itr);) {
    scene_level_object_push(&objects, itr);
  }

  const AssetLevel level = {
      .objects.values = dynarray_begin_t(&objects, AssetLevelObject),
      .objects.count  = objects.size,
  };
  asset_level_save(assets, id, level);

  log_i("Level saved", log_param("id", fmt_text(id)), log_param("objects", fmt_int(objects.size)));

  dynarray_destroy(&objects);
}

ecs_view_define(SaveGlobalView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(SaveRequestView) { ecs_access_read(SceneLevelRequestSaveComp); }

ecs_system_define(SceneLevelSaveSys) {
  EcsView*     globalView = ecs_world_view_t(world, SaveGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }

  AssetManagerComp* assets       = ecs_view_write_t(globalItr, AssetManagerComp);
  EcsView*          requestView  = ecs_world_view_t(world, SaveRequestView);
  EcsView*          instanceView = ecs_world_view_t(world, InstanceView);

  for (EcsIterator* itr = ecs_view_itr(requestView); ecs_view_walk(itr);) {
    const SceneLevelRequestSaveComp* req = ecs_view_read_t(itr, SceneLevelRequestSaveComp);
    scene_level_process_save(assets, req->levelId, instanceView);
    ecs_world_entity_destroy(world, ecs_view_entity(itr));
  }
}

ecs_module_init(scene_level_module) {
  ecs_register_comp(SceneLevelManagerComp, .destructor = ecs_destruct_level_manager);
  ecs_register_comp(SceneLevelRequestLoadComp, .destructor = ecs_destruct_level_request_load);
  ecs_register_comp(SceneLevelRequestSaveComp, .destructor = ecs_destruct_level_request_save);

  ecs_register_view(InstanceView);

  ecs_register_system(
      SceneLevelLoadSys,
      ecs_view_id(InstanceView),
      ecs_register_view(LoadGlobalView),
      ecs_register_view(LoadAssetView),
      ecs_register_view(LoadRequestView));

  ecs_register_system(
      SceneLevelSaveSys,
      ecs_view_id(InstanceView),
      ecs_register_view(SaveGlobalView),
      ecs_register_view(SaveRequestView));
}

bool scene_level_is_loading(const SceneLevelManagerComp* manager) { return manager->isLoading; }

String scene_level_current_id(const SceneLevelManagerComp* manager) {
  return manager->loadedLevelId;
}

void scene_level_load(EcsWorld* world, const String levelId) {
  diag_assert(!string_is_empty(levelId));

  const EcsEntityId reqEntity = ecs_world_entity_create(world);
  ecs_world_add_t(
      world, reqEntity, SceneLevelRequestLoadComp, .levelId = string_dup(g_alloc_heap, levelId));
}

void scene_level_save(EcsWorld* world, const String levelId) {
  diag_assert(!string_is_empty(levelId));

  const EcsEntityId reqEntity = ecs_world_entity_create(world);
  ecs_world_add_t(
      world, reqEntity, SceneLevelRequestSaveComp, .levelId = string_dup(g_alloc_heap, levelId));
}
