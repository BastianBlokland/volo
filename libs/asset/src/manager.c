#include "asset_register.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_dynstring.h"
#include "core_math.h"
#include "core_path.h"
#include "core_stringtable.h"
#include "core_time.h"
#include "data_write.h"
#include "ecs_entity.h"
#include "ecs_utils.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "trace_tracer.h"

#include "import_internal.h"
#include "loader_internal.h"
#include "manager_internal.h"
#include "repo_internal.h"

#define VOLO_ASSET_LOGGING 0

#define asset_max_load_time_per_task time_millisecond
#define asset_num_load_tasks 2
#define asset_id_max_size 256

/**
 * Amount of frames to delay unloading of assets.
 * This prevents loading the same asset multiple times if different systems request and release the
 * asset in quick succession.
 */
#define asset_max_unload_delay 1000

typedef struct {
  StringHash  idHash;
  EcsEntityId asset;
} AssetEntry;

typedef enum {
  AssetFlags_Loading        = 1 << 0,
  AssetFlags_Loaded         = 1 << 1,
  AssetFlags_Failed         = 1 << 2,
  AssetFlags_Cleanup        = 1 << 3,
  AssetFlags_LoadedOrFailed = AssetFlags_Loaded | AssetFlags_Failed,
  AssetFlags_Active         = AssetFlags_Loading | AssetFlags_Loaded | AssetFlags_Failed,
} AssetFlags;

ecs_comp_define(AssetManagerComp) {
  AssetRepo*        repo;
  AssetManagerFlags flags;
  DynArray          lookup; // AssetEntry[], kept sorted on the idHash.
};

ecs_comp_define(AssetComp) {
  String      id;
  u16         refCount;
  u16         loadCount;
  u16         unloadTicks;
  AssetFlags  flags : 8;
  AssetFormat loadFormat : 8; // Source format of the last load (valid if loadCount > 0).
  TimeReal    loadModTime;    // Source modification of the last load (valid if loadCount > 0).
  u32         loaderHash;     // Hash of the loader at the time of the last load.
};

ecs_comp_define(AssetLoadedComp);
ecs_comp_define(AssetFailedComp) {
  String error;
  i32    errorCode;
};
ecs_comp_define(AssetChangedComp);
ecs_comp_define(AssetCacheInitComp);
ecs_comp_define(AssetDirtyComp) { u32 numAcquire, numRelease; };
ecs_comp_define(AssetInstantUnloadComp);

typedef enum {
  AssetDepStorageType_None,
  AssetDepStorageType_Single,
  AssetDepStorageType_Many,
} AssetDepStorageType;

typedef struct {
  AssetDepStorageType type;
  union {
    EcsEntityId single;
    DynArray    many; // EcsEntityId[].
  };
} AssetDepStorage;

ecs_comp_define(AssetDependencyComp) {
  AssetDepStorage dependencies; // Assets that are dependencies of this asset.
  AssetDepStorage dependents;   // Assets that depend on this asset.
};

ecs_comp_define(AssetCacheRequestComp) {
  DataMeta blobMeta;
  usize    blobSize;
  Mem      blobMem;
};

ecs_comp_define(AssetReloadRequestComp);

ecs_comp_define(AssetExtLoadComp) {
  u32         count;
  AssetFormat format;
  TimeReal    modTime;
};

static AssetDepStorage asset_dep_create(const EcsEntityId asset) {
  return (AssetDepStorage){.type = AssetDepStorageType_Single, .single = asset};
}

static void asset_dep_destroy(AssetDepStorage* storage) {
  if (storage->type == AssetDepStorageType_Many) {
    dynarray_destroy(&storage->many);
  }
}

static void asset_dep_push(AssetDepStorage* storage, const EcsEntityId asset) {
  switch (storage->type) {
  case AssetDepStorageType_None:
    storage->type   = AssetDepStorageType_Single;
    storage->single = asset;
    return;
  case AssetDepStorageType_Single: {
    const EcsEntityId existingAsset               = storage->single;
    storage->type                                 = AssetDepStorageType_Many;
    storage->many                                 = dynarray_create_t(g_allocHeap, EcsEntityId, 8);
    *dynarray_push_t(&storage->many, EcsEntityId) = existingAsset;
  } break;
  case AssetDepStorageType_Many:
    break;
  }
  if (!dynarray_search_linear(&storage->many, ecs_compare_entity, &asset)) {
    *dynarray_push_t(&storage->many, EcsEntityId) = asset;
  }
}

static void asset_dep_combine(AssetDepStorage* a, AssetDepStorage* b) {
  switch (b->type) {
  case AssetDepStorageType_None:
    break;
  case AssetDepStorageType_Single:
    asset_dep_push(a, b->single);
    break;
  case AssetDepStorageType_Many:
    dynarray_for_t(&b->many, EcsEntityId, dep) { asset_dep_push(a, *dep); }
    dynarray_destroy(&b->many);
    break;
  }
}

static void asset_dep_mark(const AssetDepStorage* storage, EcsWorld* world, const EcsCompId comp) {
  switch (storage->type) {
  case AssetDepStorageType_None:
    break;
  case AssetDepStorageType_Single:
    ecs_utils_maybe_add(world, storage->single, comp);
    break;
  case AssetDepStorageType_Many:
    dynarray_for_t(&storage->many, EcsEntityId, asset) { ecs_utils_maybe_add(world, *asset, comp); }
    break;
  }
}

static void ecs_destruct_manager_comp(void* data) {
  AssetManagerComp* comp = data;
  asset_repo_destroy(comp->repo);
  dynarray_destroy(&comp->lookup);
}

static void ecs_destruct_failed_comp(void* data) {
  AssetFailedComp* comp = data;
  string_maybe_free(g_allocHeap, comp->error);
}

static void ecs_combine_asset_dirty(void* dataA, void* dataB) {
  AssetDirtyComp* compA = dataA;
  AssetDirtyComp* compB = dataB;
  compA->numAcquire += compB->numAcquire;
  compA->numRelease += compB->numRelease;
}

static void ecs_destruct_asset_dependency(void* data) {
  AssetDependencyComp* comp = data;
  asset_dep_destroy(&comp->dependencies);
  asset_dep_destroy(&comp->dependents);
}

static void ecs_combine_asset_dependency(void* dataA, void* dataB) {
  AssetDependencyComp* compA = dataA;
  AssetDependencyComp* compB = dataB;

  asset_dep_combine(&compA->dependencies, &compB->dependencies);
  asset_dep_combine(&compA->dependents, &compB->dependents);
}

static void ecs_combine_asset_ext_load(void* dataA, void* dataB) {
  AssetExtLoadComp* compA = dataA;
  AssetExtLoadComp* compB = dataB;
  compA->count += compB->count;
  compA->modTime = math_max(compA->modTime, compB->modTime);
  diag_assert(compA->format == compB->format);
}

static void ecs_destruct_cache_request_comp(void* data) {
  AssetCacheRequestComp* comp = data;
  alloc_free(g_allocHeap, comp->blobMem);
}

static i8 asset_compare_entry(const void* a, const void* b) {
  return compare_stringhash(field_ptr(a, AssetEntry, idHash), field_ptr(b, AssetEntry, idHash));
}

static AssetManagerComp*
asset_manager_create_internal(EcsWorld* world, AssetRepo* repo, const AssetManagerFlags flags) {
  return ecs_world_add_t(
      world,
      ecs_world_global(world),
      AssetManagerComp,
      .repo   = repo,
      .flags  = flags,
      .lookup = dynarray_create_t(g_allocHeap, AssetEntry, 128));
}

static EcsEntityId asset_entity_create(EcsWorld* world, StringTable* stringTable, const String id) {
  diag_assert_msg(!string_is_empty(id), "Empty asset-id is invalid");

  const String idDup = stringtable_intern(stringTable, id);
  if (UNLIKELY(!idDup.ptr)) {
    diag_crash_msg("Asset id string-table ran out of space");
  }

  const EcsEntityId entity = ecs_world_entity_create(world);
  ecs_world_add_t(world, entity, AssetComp, .id = idDup);
  return entity;
}

static u32 asset_manager_loader_hash(const void* ctx, const String assetId) {
  const AssetImportEnvComp* importEnv = ctx;
  return asset_loader_hash(importEnv, assetId);
}

typedef enum {
  AssetLoadResult_Started,
  AssetLoadResult_Missing,
  AssetLoadResult_Unsupported,
} AssetLoadResult;

static String asset_manager_load_result_str(const AssetLoadResult res) {
  switch (res) {
  case AssetLoadResult_Started:
    return string_lit("Started");
  case AssetLoadResult_Missing:
    return string_lit("Source not found");
  case AssetLoadResult_Unsupported:
    return string_lit("Format unsupported");
  }
  UNREACHABLE
}

static AssetLoadResult asset_manager_load(
    EcsWorld*                 world,
    const AssetManagerComp*   manager,
    const AssetImportEnvComp* importEnv,
    AssetComp*                asset,
    const EcsEntityId         assetEntity) {
  diag_assert(asset_import_ready(importEnv, asset->id));

  const AssetRepoLoaderHasher loaderHasher = {
      .ctx         = importEnv,
      .computeHash = asset_manager_loader_hash,
  };

  AssetSource* source = asset_repo_open(manager->repo, asset->id, loaderHasher);
  if (!source) {
    return AssetLoadResult_Missing;
  }

  if (manager->flags & AssetManagerFlags_TrackChanges) {
    asset_repo_changes_watch(manager->repo, asset->id, (u64)assetEntity);
  }
  if (source->flags & AssetInfoFlags_Cached) {
    ecs_world_add_empty_t(world, assetEntity, AssetCacheInitComp);
  }

  ++asset->loadCount;
  asset->loadFormat  = source->format;
  asset->loadModTime = source->modTime;
  asset->loaderHash  = asset_loader_hash(importEnv, asset->id);

#if VOLO_ASSET_LOGGING
  log_d(
      "Asset load started",
      log_param("id", fmt_path(asset->id)),
      log_param("entity", ecs_entity_fmt(assetEntity)),
      log_param("format", fmt_text(asset_format_str(source->format))),
      log_param("size", fmt_size(source->data.size)));
#endif

  AssetLoader loader = asset_loader(source->format);

  AssetLoadResult result = AssetLoadResult_Started;
  if (LIKELY(loader)) {
    trace_begin("asset_loader", TraceColor_Red);
    loader(world, importEnv, asset->id, assetEntity, source);
    trace_end();
  } else {
    result = AssetLoadResult_Unsupported;
  }

  if (UNLIKELY(result != AssetLoadResult_Started)) {
    asset_repo_close(source);
  }
  return result;
}

ecs_view_define(GlobalUpdateView) {
  ecs_access_read(AssetImportEnvComp);
  ecs_access_read(AssetManagerComp);
}

ecs_view_define(DirtyAssetView) {
  ecs_access_write(AssetComp);
  ecs_access_write(AssetDirtyComp);
}

ecs_view_define(AssetDependencyView) { ecs_access_read(AssetDependencyComp); }

ecs_view_define(GlobalReadView) { ecs_access_read(AssetManagerComp); }
ecs_view_define(GlobalWriteView) { ecs_access_write(AssetManagerComp); }

static AssetManagerComp* asset_manager_mutable(EcsWorld* world) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalWriteView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  return globalItr ? ecs_view_write_t(globalItr, AssetManagerComp) : null;
}

static const AssetManagerComp* asset_manager_readonly(EcsWorld* world) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalReadView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  return globalItr ? ecs_view_read_t(globalItr, AssetManagerComp) : null;
}

static u32 asset_unload_delay(
    EcsWorld* world, const AssetManagerComp* manager, const EcsEntityId assetEntity) {
  if (ecs_world_has_t(world, assetEntity, AssetInstantUnloadComp)) {
    return 0;
  }
  if (manager->flags & AssetManagerFlags_DelayUnload) {
    return asset_max_unload_delay;
  }
  return 0;
}

ecs_system_define(AssetUpdateDirtySys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalUpdateView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return; // Global dependencies not initialized.
  }
  const AssetManagerComp*   man       = ecs_view_read_t(globalItr, AssetManagerComp);
  const AssetImportEnvComp* importEnv = ecs_view_read_t(globalItr, AssetImportEnvComp);

  TimeDuration loadTime   = 0;
  EcsView*     assetsView = ecs_world_view_t(world, DirtyAssetView);

  for (EcsIterator* itr = ecs_view_itr_step(assetsView, parCount, parIndex); ecs_view_walk(itr);) {
    const EcsEntityId entity    = ecs_view_entity(itr);
    AssetComp*        assetComp = ecs_view_write_t(itr, AssetComp);
    AssetDirtyComp*   dirtyComp = ecs_view_write_t(itr, AssetDirtyComp);

    assetComp->refCount += dirtyComp->numAcquire;
    diag_assert_msg(assetComp->refCount >= dirtyComp->numRelease, "Unbalanced Acquire / Release");
    assetComp->refCount -= dirtyComp->numRelease;

    // Loading assets should be continuously updated to track their progress.
    bool updateRequired = true;

    if (assetComp->flags & AssetFlags_Cleanup) {
      /**
       * Actual data cleanup will be performed by the loader responsible for this asset-type.
       * NOTE: Early out as the asset cannot be loaded again in the same frame as the cleanup.
       */
      assetComp->flags &= ~AssetFlags_Cleanup;
      updateRequired = assetComp->refCount > 0;
      goto AssetUpdateDone;
    }

    if (assetComp->refCount && !(assetComp->flags & AssetFlags_Active)) {
      assetComp->unloadTicks = 0;
      /**
       * Asset ref-count is non-zero; start loading.
       * NOTE: Loading can fail to start, for example the asset doesn't exist in the manager's repo.
       */
      if (asset_import_ready(importEnv, assetComp->id) && loadTime < asset_max_load_time_per_task) {
        assetComp->flags |= AssetFlags_Loading;
        const TimeSteady loadStart = time_steady_clock();

        MAYBE_UNUSED const String assetFileName = path_filename(assetComp->id);
        trace_begin_msg("asset_manager_load", TraceColor_Blue, "{}", fmt_text(assetFileName));
        {
          const AssetLoadResult res = asset_manager_load(world, man, importEnv, assetComp, entity);
          if (res == AssetLoadResult_Started) {
            loadTime += time_steady_duration(loadStart, time_steady_clock());
            ecs_utils_maybe_remove_t(world, entity, AssetInstantUnloadComp);
          } else {
            const String error = asset_manager_load_result_str(res);
            asset_mark_load_failure(world, entity, assetComp->id, error, (i32)res);
          }
          ecs_utils_maybe_remove_t(world, entity, AssetChangedComp);
        }
        trace_end();
      }
      goto AssetUpdateDone;
    }

    if (assetComp->flags & AssetFlags_Loading && ecs_world_has_t(world, entity, AssetFailedComp)) {
      /**
       * Asset has failed loading.
       */
      assetComp->flags &= ~AssetFlags_Loading;
      assetComp->flags |= AssetFlags_Failed;
      goto AssetUpdateDone;
    }
    if (assetComp->flags & AssetFlags_Loading && ecs_world_has_t(world, entity, AssetLoadedComp)) {
      /**
       * Asset has finished loading.
       */
      assetComp->flags &= ~AssetFlags_Loading;
      assetComp->flags |= AssetFlags_Loaded;
      goto AssetUpdateDone;
    }

    const u32  unloadDelay = asset_unload_delay(world, man, entity);
    const bool unload      = !assetComp->refCount && ++assetComp->unloadTicks >= unloadDelay;
    if (unload && assetComp->flags & AssetFlags_Failed) {
      /**
       * Asset was failed and should now be unloaded.
       */
      ecs_world_remove_t(world, entity, AssetFailedComp);
      assetComp->flags &= ~AssetFlags_Failed;
      goto AssetUpdateDone;
    }
    if (unload && assetComp->flags & AssetFlags_Loaded) {
      /**
       * Asset was loaded and should now be unloaded.
       */
#if VOLO_ASSET_LOGGING
      log_d(
          "Asset unload",
          log_param("id", fmt_path(assetComp->id)),
          log_param("entity", ecs_entity_fmt(entity)));
#endif
      ecs_world_remove_t(world, entity, AssetLoadedComp);
      assetComp->flags &= ~AssetFlags_Loaded;
      assetComp->flags |= AssetFlags_Cleanup; // Mark this asset as cleaning up (will take a frame).
      goto AssetUpdateDone;
    }

    if (!!assetComp->refCount == !!(assetComp->flags & AssetFlags_LoadedOrFailed)) {
      /**
       * Asset load state matches required state, no need for further updates.
       */
      updateRequired = false;
    }

  AssetUpdateDone:
    dirtyComp->numAcquire = 0;
    dirtyComp->numRelease = 0;
    if (!updateRequired) {
      ecs_world_remove_t(world, entity, AssetDirtyComp);
    }
  }
}

ecs_system_define(AssetPollChangedSys) {
  const AssetManagerComp* manager = asset_manager_readonly(world);
  if (!manager) {
    return;
  }
  if (!(manager->flags & AssetManagerFlags_TrackChanges)) {
    return;
  }

  EcsIterator* depItr = ecs_view_itr(ecs_world_view_t(world, AssetDependencyView));

  u64 userData;
  while (asset_repo_changes_poll(manager->repo, &userData)) {
    const EcsEntityId assetEntity = (EcsEntityId)userData;
    ecs_utils_maybe_add_t(world, assetEntity, AssetChangedComp);
    ecs_utils_maybe_add_t(world, assetEntity, AssetInstantUnloadComp);

    // Also mark the dependent assets as changed.
    if (ecs_view_maybe_jump(depItr, assetEntity)) {
      const AssetDependencyComp* depComp = ecs_view_read_t(depItr, AssetDependencyComp);
      asset_dep_mark(&depComp->dependents, world, ecs_comp_id(AssetChangedComp));
      asset_dep_mark(&depComp->dependents, world, ecs_comp_id(AssetInstantUnloadComp));
    }
  }
}

ecs_view_define(AssetReloadView) {
  ecs_access_with(AssetComp);
  ecs_access_with(AssetReloadRequestComp);
  ecs_access_maybe_read(AssetDependencyComp);
}

ecs_system_define(AssetReloadRequestSys) {
  EcsView* reloadView = ecs_world_view_t(world, AssetReloadView);
  for (EcsIterator* itr = ecs_view_itr(reloadView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_utils_maybe_add_t(world, entity, AssetChangedComp);
    ecs_utils_maybe_add_t(world, entity, AssetInstantUnloadComp);

    const AssetDependencyComp* depComp = ecs_view_read_t(itr, AssetDependencyComp);
    if (depComp) {
      asset_dep_mark(&depComp->dependents, world, ecs_comp_id(AssetChangedComp));
      asset_dep_mark(&depComp->dependents, world, ecs_comp_id(AssetInstantUnloadComp));
    }
    ecs_world_remove_t(world, entity, AssetReloadRequestComp);
  }
}

ecs_view_define(AssetLoadExtView) {
  ecs_access_write(AssetComp);
  ecs_access_read(AssetExtLoadComp);
}

ecs_system_define(AssetLoadExtSys) {
  EcsView* extView = ecs_world_view_t(world, AssetLoadExtView);
  for (EcsIterator* itr = ecs_view_itr(extView); ecs_view_walk(itr);) {
    const EcsEntityId       assetEntity = ecs_view_entity(itr);
    AssetComp*              assetComp   = ecs_view_write_t(itr, AssetComp);
    const AssetExtLoadComp* extLoadComp = ecs_view_read_t(itr, AssetExtLoadComp);

    assetComp->loadCount += extLoadComp->count;
    assetComp->loadFormat  = extLoadComp->format;
    assetComp->loadModTime = extLoadComp->modTime;

    ecs_utils_maybe_remove_t(world, assetEntity, AssetChangedComp);
    ecs_utils_maybe_remove_t(world, assetEntity, AssetInstantUnloadComp);

    ecs_world_remove_t(world, assetEntity, AssetExtLoadComp);
  }
}

ecs_view_define(AssetCacheRequestView) {
  ecs_access_read(AssetComp);
  ecs_access_read(AssetCacheRequestComp);
  ecs_access_maybe_read(AssetDependencyComp);
}

ecs_view_define(AssetCacheInitView) {
  ecs_access_read(AssetComp);
  ecs_access_with(AssetCacheInitComp);
}

ecs_view_define(AssetDepView) { ecs_access_read(AssetComp); }

ecs_system_define(AssetCacheSys) {
  AssetManagerComp* manager = asset_manager_mutable(world);
  if (!manager) {
    return;
  }

  EcsView* cacheRequestView = ecs_world_view_t(world, AssetCacheRequestView);
  EcsView* cacheInitView    = ecs_world_view_t(world, AssetCacheInitView);
  EcsView* depView          = ecs_world_view_t(world, AssetDepView);

  EcsIterator* depItr = ecs_view_itr(depView);

  AssetRepoDep deps[asset_repo_cache_deps_max];
  usize        depCount = 0;

  // Process cache requests.
  for (EcsIterator* itr = ecs_view_itr(cacheRequestView); ecs_view_walk(itr);) {
    const EcsEntityId            assetEntity = ecs_view_entity(itr);
    const AssetComp*             assetComp   = ecs_view_read_t(itr, AssetComp);
    const AssetCacheRequestComp* requestComp = ecs_view_read_t(itr, AssetCacheRequestComp);
    const AssetDependencyComp*   depComp     = ecs_view_read_t(itr, AssetDependencyComp);

    diag_assert(assetComp->loadCount); // Caching an asset without loading it makes no sense.

    // Collect asset data.
    const String   id         = assetComp->id;
    const DataMeta dataMeta   = requestComp->blobMeta;
    const Mem      blob       = mem_slice(requestComp->blobMem, 0, requestComp->blobSize);
    const TimeReal modTime    = assetComp->loadModTime;
    const u32      loaderHash = assetComp->loaderHash;

    // Collect asset dependencies.
    depCount = 0;
    if (depComp) {
      switch (depComp->dependencies.type) {
      case AssetDepStorageType_None:
        break;
      case AssetDepStorageType_Single: {
        ecs_view_jump(depItr, depComp->dependencies.single);
        const AssetComp* depAssetComp = ecs_view_read_t(depItr, AssetComp);

        deps[depCount++] = (AssetRepoDep){
            .id         = depAssetComp->id,
            .modTime    = depAssetComp->loadModTime,
            .loaderHash = depAssetComp->loaderHash,
        };
      } break;
      case AssetDepStorageType_Many:
        dynarray_for_t(&depComp->dependencies.many, EcsEntityId, asset) {
          if (depCount == array_elems(deps)) {
            break;
          }
          ecs_view_jump(depItr, *asset);
          const AssetComp* depAssetComp = ecs_view_read_t(depItr, AssetComp);

          deps[depCount++] = (AssetRepoDep){
              .id         = depAssetComp->id,
              .modTime    = depAssetComp->loadModTime,
              .loaderHash = depAssetComp->loaderHash,
          };
        }
        break;
      }
    }

    // Save the asset in the repo cache.
    asset_repo_cache(manager->repo, id, dataMeta, modTime, loaderHash, blob, deps, depCount);

    ecs_world_remove_t(world, assetEntity, AssetCacheRequestComp);
  }

  // Initialize cached assets.
  for (EcsIterator* itr = ecs_view_itr(cacheInitView); ecs_view_walk(itr);) {
    const EcsEntityId assetEntity = ecs_view_entity(itr);
    const AssetComp*  assetComp   = ecs_view_read_t(itr, AssetComp);

    // Register cached asset dependencies so this asset can be reloaded when they change.
    depCount = asset_repo_cache_deps(manager->repo, assetComp->id, deps);
    for (usize i = 0; i != depCount; ++i) {
      const EcsEntityId depEntity = asset_watch(world, manager, deps[i].id);
      asset_register_dep(world, assetEntity, depEntity);
    }

    ecs_world_remove_t(world, assetEntity, AssetCacheInitComp);
  }
}

ecs_module_init(asset_manager_module) {
  ecs_register_comp(AssetManagerComp, .destructor = ecs_destruct_manager_comp, .destructOrder = 30);
  ecs_register_comp(AssetComp);
  ecs_register_comp(AssetFailedComp, .destructor = ecs_destruct_failed_comp);
  ecs_register_comp_empty(AssetLoadedComp);
  ecs_register_comp_empty(AssetChangedComp);
  ecs_register_comp_empty(AssetCacheInitComp);
  ecs_register_comp_empty(AssetInstantUnloadComp);
  ecs_register_comp(AssetDirtyComp, .combinator = ecs_combine_asset_dirty);
  ecs_register_comp(
      AssetDependencyComp,
      .destructor = ecs_destruct_asset_dependency,
      .combinator = ecs_combine_asset_dependency);
  ecs_register_comp(AssetCacheRequestComp, .destructor = ecs_destruct_cache_request_comp);
  ecs_register_comp_empty(AssetReloadRequestComp);
  ecs_register_comp(AssetExtLoadComp, .combinator = ecs_combine_asset_ext_load);

  ecs_register_view(GlobalUpdateView);
  ecs_register_view(DirtyAssetView);
  ecs_register_view(AssetDependencyView);
  ecs_register_view(GlobalReadView);
  ecs_register_view(GlobalWriteView);

  ecs_register_system(
      AssetUpdateDirtySys, ecs_view_id(GlobalUpdateView), ecs_view_id(DirtyAssetView));
  ecs_parallel(AssetUpdateDirtySys, asset_num_load_tasks);
  ecs_order(AssetUpdateDirtySys, AssetOrder_Update);

  ecs_register_system(
      AssetPollChangedSys, ecs_view_id(AssetDependencyView), ecs_view_id(GlobalReadView));

  ecs_register_system(AssetReloadRequestSys, ecs_register_view(AssetReloadView));

  ecs_register_system(AssetLoadExtSys, ecs_register_view(AssetLoadExtView));
  ecs_order(AssetLoadExtSys, AssetOrder_Update);

  ecs_register_system(
      AssetCacheSys,
      ecs_register_view(AssetCacheRequestView),
      ecs_register_view(AssetCacheInitView),
      ecs_register_view(AssetDepView),
      ecs_view_id(GlobalWriteView));
}

String     asset_id(const AssetComp* comp) { return comp->id; }
StringHash asset_id_hash(const AssetComp* comp) { return string_hash(comp->id); }

String asset_error(const AssetFailedComp* comp) { return comp->error; }
i32    asset_error_code(const AssetFailedComp* comp) { return comp->errorCode; };

bool asset_path(const AssetManagerComp* manager, const AssetComp* asset, DynString* out) {
  return asset_repo_path(manager->repo, asset->id, out);
}

bool asset_path_by_id(const AssetManagerComp* manager, const String id, DynString* out) {
  return asset_repo_path(manager->repo, id, out);
}

AssetManagerComp*
asset_manager_create_fs(EcsWorld* world, const AssetManagerFlags flags, const String rootPath) {
  AssetRepo* repo = asset_repo_create_fs(rootPath);
  if (UNLIKELY(!repo)) {
    return null;
  }
  return asset_manager_create_internal(world, repo, flags);
}

AssetManagerComp*
asset_manager_create_pack(EcsWorld* world, const AssetManagerFlags flags, const String filePath) {
  AssetRepo* repo = asset_repo_create_pack(filePath);
  if (UNLIKELY(!repo)) {
    return null;
  }
  return asset_manager_create_internal(world, repo, flags);
}

AssetManagerComp* asset_manager_create_mem(
    EcsWorld*               world,
    const AssetManagerFlags flags,
    const AssetMemRecord*   records,
    const usize             recordCount) {
  AssetRepo* repo = asset_repo_create_mem(records, recordCount);
  if (UNLIKELY(!repo)) {
    return null;
  }
  return asset_manager_create_internal(world, repo, flags);
}

EcsEntityId asset_lookup(EcsWorld* world, AssetManagerComp* manager, const String id) {
  diag_assert_msg(!string_is_empty(id), "Asset id cannot be empty");
  diag_assert_msg(id.size <= asset_id_max_size, "Asset id size exceeds maximum");

  const AssetEntry tgt = {.idHash = string_hash(id)};
  AssetEntry* entry = dynarray_find_or_insert_sorted(&manager->lookup, asset_compare_entry, &tgt);

  if (entry->idHash != tgt.idHash) {
    diag_assert(!entry->idHash && !entry->asset);
    entry->idHash = tgt.idHash;
    entry->asset  = asset_entity_create(world, g_stringtable, id);
  }
  return entry->asset;
}

EcsEntityId asset_maybe_lookup(EcsWorld* world, AssetManagerComp* manager, const String id) {
  return string_is_empty(id) ? 0 : asset_lookup(world, manager, id);
}

void asset_acquire(EcsWorld* world, const EcsEntityId asset) {
  ecs_world_add_t(world, asset, AssetDirtyComp, .numAcquire = 1);
}

void asset_release(EcsWorld* world, const EcsEntityId asset) {
  ecs_world_add_t(world, asset, AssetDirtyComp, .numRelease = 1);
}

void asset_reload_request(EcsWorld* world, const EcsEntityId assetEntity) {
  ecs_utils_maybe_add_t(world, assetEntity, AssetReloadRequestComp);
}

u32 asset_ref_count(const AssetComp* asset) { return asset->refCount; }

u32 asset_load_count(const AssetComp* asset) { return asset->loadCount; }

bool asset_is_loading(const AssetComp* asset) { return (asset->flags & AssetFlags_Loading) != 0; }

u32 asset_ticks_until_unload(const AssetComp* asset) {
  return asset_max_unload_delay - asset->unloadTicks;
}

bool asset_save(AssetManagerComp* manager, const String id, const String data) {
  diag_assert_msg(path_extension(id).size, "Asset id's must have an extension");
  return asset_repo_save(manager->repo, id, data);
}

bool asset_save_supported(const AssetManagerComp* manager) {
  return asset_repo_save_supported(manager->repo);
}

typedef struct {
  EcsWorld*         world;
  AssetManagerComp* manager;
  u32               count;
  EcsEntityId*      out;
} AssetQueryContext;

static void asset_query_output(void* ctxRaw, const String id) {
  AssetQueryContext* ctx = ctxRaw;
  if (LIKELY(ctx->count != asset_query_max_results)) {
    ctx->out[ctx->count++] = asset_lookup(ctx->world, ctx->manager, id);
  }
}

u32 asset_query(
    EcsWorld*         world,
    AssetManagerComp* manager,
    const String      pattern,
    EcsEntityId       out[PARAM_ARRAY_SIZE(asset_query_max_results)]) {
  AssetQueryContext ctx = {.world = world, .manager = manager, .out = out};
  asset_repo_query(manager->repo, pattern, &ctx, asset_query_output);
  return ctx.count;
}

void asset_register_dep(EcsWorld* world, EcsEntityId asset, const EcsEntityId dependency) {
  diag_assert(asset);
  diag_assert(dependency);

  /**
   * Track the dependencies both upwards and downwards.
   */
  ecs_world_add_t(world, dependency, AssetDependencyComp, .dependents = asset_dep_create(asset));
  ecs_world_add_t(world, asset, AssetDependencyComp, .dependencies = asset_dep_create(dependency));
}

bool asset_source_stat(
    const AssetManagerComp*   manager,
    const AssetImportEnvComp* importEnv,
    const String              id,
    AssetInfo*                out) {
  diag_assert(asset_import_ready(importEnv, id));

  const AssetRepoLoaderHasher loaderHasher = {
      .ctx         = importEnv,
      .computeHash = asset_manager_loader_hash,
  };
  return asset_repo_stat(manager->repo, id, loaderHasher, out);
}

AssetSource* asset_source_open(
    const AssetManagerComp* manager, const AssetImportEnvComp* importEnv, const String id) {
  diag_assert(asset_import_ready(importEnv, id));

  const AssetRepoLoaderHasher loaderHasher = {
      .ctx         = importEnv,
      .computeHash = asset_manager_loader_hash,
  };
  return asset_repo_open(manager->repo, id, loaderHasher);
}

EcsEntityId asset_watch(EcsWorld* world, AssetManagerComp* manager, const String id) {
  const EcsEntityId assetEntity = asset_lookup(world, manager, id);

  if (manager->flags & AssetManagerFlags_TrackChanges) {
    asset_repo_changes_watch(manager->repo, id, (u64)assetEntity);
  }

  return assetEntity;
}

void asset_mark_load_failure(
    EcsWorld*         world,
    const EcsEntityId asset,
    const String      id,
    const String      error,
    const i32         errorCode) {
  const String errorTrimmed = string_trim_whitespace(error);

  log_e(
      "Failed to load asset",
      log_param("id", fmt_text(id)),
      log_param("entity", ecs_entity_fmt(asset)),
      log_param("error", fmt_text(errorTrimmed)),
      log_param("error-code", fmt_int(errorCode)));

  ecs_world_add_t(
      world,
      asset,
      AssetFailedComp,
      .error     = string_maybe_dup(g_allocHeap, errorTrimmed),
      .errorCode = errorCode);
}

void asset_mark_load_success(EcsWorld* world, const EcsEntityId asset) {
  ecs_world_add_empty_t(world, asset, AssetLoadedComp);
}

void asset_mark_external_load(
    EcsWorld* world, const EcsEntityId asset, const AssetFormat format, const TimeReal modTime) {

  ecs_world_add_t(world, asset, AssetExtLoadComp, .count = 1, .format = format, .modTime = modTime);
}

void asset_cache(
    EcsWorld* world, const EcsEntityId asset, const DataMeta dataMeta, const Mem data) {
  DynString blobBuffer = dynstring_create(g_allocHeap, 256);
  data_write_bin(g_dataReg, &blobBuffer, dataMeta, data);

  ecs_world_add_t(
      world,
      asset,
      AssetCacheRequestComp,
      .blobMeta = dataMeta,
      .blobSize = blobBuffer.size,
      .blobMem  = blobBuffer.data);
}
