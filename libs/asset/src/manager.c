#include "asset_register.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_path.h"
#include "core_search.h"
#include "core_time.h"
#include "data_write.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "trace_tracer.h"

#include "loader_internal.h"
#include "manager_internal.h"
#include "repo_internal.h"

#define asset_max_load_time_per_task time_milliseconds(2)
#define asset_num_load_tasks 2
#define asset_id_chunk_size (16 * usize_kibibyte)

/**
 * Amount of frames to delay unloading of assets.
 * This prevents loading the same asset multiple times if different systems request and release the
 * asset in quick succession.
 */
#define asset_max_unload_delay 500

typedef struct {
  StringHash  idHash;
  EcsEntityId asset;
} AssetEntry;

typedef enum {
  AssetFlags_Loading = 1 << 0,
  AssetFlags_Loaded  = 1 << 1,
  AssetFlags_Failed  = 1 << 2,
  AssetFlags_Cleanup = 1 << 3,
  AssetFlags_Active  = AssetFlags_Loading | AssetFlags_Loaded | AssetFlags_Failed,
} AssetFlags;

ecs_comp_define(AssetManagerComp) {
  AssetRepo*        repo;
  Allocator*        idAlloc; // (chunked) bump allocator for asset ids.
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
};

ecs_comp_define(AssetLoadedComp);
ecs_comp_define(AssetFailedComp);
ecs_comp_define(AssetChangedComp);
ecs_comp_define(AssetDirtyComp) { u32 numAcquire, numRelease; };
ecs_comp_define(AssetInstantUnloadComp);

typedef enum {
  AssetDependencyStorage_Single,
  AssetDependencyStorage_Many,
} AssetDependencyStorage;

ecs_comp_define(AssetDependencyComp) {
  struct {
    AssetDependencyStorage type;
    union {
      EcsEntityId single;
      DynArray    many; // EcsEntityId[].
    };
  } dependents;
};

ecs_comp_define(AssetCacheRequest) {
  DataMeta blobMeta;
  usize    blobSize;
  Mem      blobMem;
};

static void ecs_destruct_manager_comp(void* data) {
  AssetManagerComp* comp = data;
  asset_repo_destroy(comp->repo);
  alloc_chunked_destroy(comp->idAlloc);
  dynarray_destroy(&comp->lookup);
}

static void ecs_combine_asset_dirty(void* dataA, void* dataB) {
  AssetDirtyComp* compA = dataA;
  AssetDirtyComp* compB = dataB;
  compA->numAcquire += compB->numAcquire;
  compA->numRelease += compB->numRelease;
}

static void ecs_destruct_asset_dependency(void* data) {
  AssetDependencyComp* comp = data;
  if (comp->dependents.type == AssetDependencyStorage_Many) {
    dynarray_destroy(&comp->dependents.many);
  }
}

static void asset_add_dependent(AssetDependencyComp* comp, const EcsEntityId dep) {
  if (comp->dependents.type == AssetDependencyStorage_Single) {
    const EcsEntityId existingDep = comp->dependents.single;
    comp->dependents.type         = AssetDependencyStorage_Many;
    comp->dependents.many         = dynarray_create_t(g_allocHeap, EcsEntityId, 8);
    *dynarray_push_t(&comp->dependents.many, EcsEntityId) = existingDep;
  }
  if (!dynarray_search_linear(&comp->dependents.many, ecs_compare_entity, &dep)) {
    *dynarray_push_t(&comp->dependents.many, EcsEntityId) = dep;
  }
}

static void ecs_combine_asset_dependency(void* dataA, void* dataB) {
  AssetDependencyComp* compA = dataA;
  AssetDependencyComp* compB = dataB;

  switch (compB->dependents.type) {
  case AssetDependencyStorage_Single:
    asset_add_dependent(compA, compB->dependents.single);
    break;
  case AssetDependencyStorage_Many:
    dynarray_for_t(&compB->dependents.many, EcsEntityId, dep) { asset_add_dependent(compA, *dep); }
    dynarray_destroy(&compB->dependents.many);
    break;
  }
}

static void ecs_destruct_cache_request_comp(void* data) {
  AssetCacheRequest* comp = data;
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
      .repo    = repo,
      .idAlloc = alloc_chunked_create(g_allocHeap, alloc_bump_create, asset_id_chunk_size),
      .flags   = flags,
      .lookup  = dynarray_create_t(g_allocHeap, AssetEntry, 128));
}

static EcsEntityId asset_entity_create(EcsWorld* world, Allocator* idAlloc, const String id) {
  diag_assert_msg(!string_is_empty(id), "Empty asset-id is invalid");

  const EcsEntityId entity = ecs_world_entity_create(world);
  ecs_world_add_t(world, entity, AssetComp, .id = string_dup(idAlloc, id));
  return entity;
}

static bool asset_manager_load(
    EcsWorld*               world,
    const AssetManagerComp* manager,
    AssetComp*              asset,
    const EcsEntityId       assetEntity) {

  AssetSource* source = asset_repo_source_open(manager->repo, asset->id);
  if (!source) {
    return false;
  }
  trace_begin_msg("asset_manager_load", TraceColor_Blue, "{}", fmt_text(path_filename(asset->id)));

  if (manager->flags & AssetManagerFlags_TrackChanges) {
    asset_repo_changes_watch(manager->repo, asset->id, (u64)assetEntity);
  }

  ++asset->loadCount;
  asset->loadFormat  = source->format;
  asset->loadModTime = source->modTime;

  log_d(
      "Asset load started",
      log_param("id", fmt_path(asset->id)),
      log_param("format", fmt_text(asset_format_str(source->format))),
      log_param("size", fmt_size(source->data.size)));

  AssetLoader loader = asset_loader(source->format);

  bool success = true;
  if (LIKELY(loader)) {
    loader(world, asset->id, assetEntity, source);
  } else {
    log_e(
        "Asset format cannot be loaded directly",
        log_param("id", fmt_path(asset->id)),
        log_param("format", fmt_text(asset_format_str(source->format))));
    success = false;
  }

  trace_end();

  if (!success) {
    asset_repo_source_close(source);
  }
  return success;
}

ecs_view_define(DirtyAssetView) {
  ecs_access_write(AssetComp);
  ecs_access_write(AssetDirtyComp);
  ecs_access_maybe_read(AssetDependencyComp);
}

ecs_view_define(AssetDependencyView) { ecs_access_read(AssetDependencyComp); }

ecs_view_define(GlobalView) { ecs_access_read(AssetManagerComp); }

static const AssetManagerComp* asset_manager_readonly(EcsWorld* world) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  return globalItr ? ecs_view_read_t(globalItr, AssetManagerComp) : null;
}

static void
asset_mark_dependents(EcsWorld* world, const AssetDependencyComp* depComp, const EcsCompId comp) {
  switch (depComp->dependents.type) {
  case AssetDependencyStorage_Single:
    ecs_utils_maybe_add(world, depComp->dependents.single, comp);
    break;
  case AssetDependencyStorage_Many:
    dynarray_for_t(&depComp->dependents.many, EcsEntityId, dependent) {
      ecs_utils_maybe_add(world, *dependent, comp);
    }
    break;
  }
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
  const AssetManagerComp* manager = asset_manager_readonly(world);
  if (!manager) {
    /**
     * The manager has not been created yet, we delay the processing of asset requests until a
     * manager has been created.
     * NOTE: No requests get lost, they just stay unprocessed.
     */
    return;
  }

  TimeDuration loadTime   = 0;
  EcsView*     assetsView = ecs_world_view_t(world, DirtyAssetView);

  for (EcsIterator* itr = ecs_view_itr_step(assetsView, parCount, parIndex); ecs_view_walk(itr);) {
    const EcsEntityId          entity         = ecs_view_entity(itr);
    AssetComp*                 assetComp      = ecs_view_write_t(itr, AssetComp);
    AssetDirtyComp*            dirtyComp      = ecs_view_write_t(itr, AssetDirtyComp);
    const AssetDependencyComp* dependencyComp = ecs_view_read_t(itr, AssetDependencyComp);

    assetComp->refCount += dirtyComp->numAcquire;
    diag_assert_msg(assetComp->refCount >= dirtyComp->numRelease, "Unbalanced Acquire / Release");
    assetComp->refCount -= dirtyComp->numRelease;

    // Loading assets should be continuously updated to track their progress.
    bool updateRequired = true;

    if (assetComp->flags & AssetFlags_Failed) {
      /**
       * This asset failed before (but was now acquired again); clear the state to retry.
       */
      assetComp->flags &= ~AssetFlags_Failed;
      ecs_world_remove_t(world, entity, AssetFailedComp);
      goto AssetUpdateDone;
    }

    if (assetComp->flags & AssetFlags_Cleanup) {
      /**
       * Actual data cleanup will be performed by the loader responsible for this asset-type.
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
      const bool canLoad = loadTime < asset_max_load_time_per_task;
      if (canLoad) {
        assetComp->flags |= AssetFlags_Loading;
        const TimeSteady loadStart = time_steady_clock();
        if (asset_manager_load(world, manager, assetComp, entity)) {
          loadTime += time_steady_duration(loadStart, time_steady_clock());
        } else {
          ecs_world_add_empty_t(world, entity, AssetFailedComp);
        }
        ecs_utils_maybe_remove_t(world, entity, AssetChangedComp);
      }
      goto AssetUpdateDone;
    }

    if (assetComp->flags & AssetFlags_Loading && ecs_world_has_t(world, entity, AssetFailedComp)) {
      /**
       * Asset has failed loading.
       */

      if (dependencyComp) {
        /*
         * Mark the assets that depend on this asset to be instantly unloaded (instead of waiting
         * for the unload delay). Reason is that if this asset fails to load most likely the asset
         * that depends on this one should be reloaded as well.
         */
        asset_mark_dependents(world, dependencyComp, ecs_comp_id(AssetInstantUnloadComp));
      }

      assetComp->flags &= ~AssetFlags_Loading;
      assetComp->flags |= AssetFlags_Failed;
      updateRequired = false;
      goto AssetUpdateDone;
    }

    if (assetComp->flags & AssetFlags_Loading && ecs_world_has_t(world, entity, AssetLoadedComp)) {
      /**
       * Asset has finished loading.
       */
      assetComp->flags &= ~AssetFlags_Loading;
      assetComp->flags |= AssetFlags_Loaded;
      updateRequired = false;
      goto AssetUpdateDone;
    }

    const u32  unloadDelay = asset_unload_delay(world, manager, entity);
    const bool unload      = !assetComp->refCount && ++assetComp->unloadTicks >= unloadDelay;
    if (unload && assetComp->flags & AssetFlags_Loaded) {
      /**
       * Asset should be unloaded.
       */
      ecs_world_remove_t(world, entity, AssetLoadedComp);
      ecs_utils_maybe_remove_t(world, entity, AssetInstantUnloadComp);
      assetComp->flags &= ~AssetFlags_Loaded;
      assetComp->flags |= AssetFlags_Cleanup;
      goto AssetUpdateDone;
    }

    if (assetComp->refCount && assetComp->flags & AssetFlags_Loaded) {
      /**
       * Asset was already loaded, no need for further updates.
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
      asset_mark_dependents(world, depComp, ecs_comp_id(AssetChangedComp));
      asset_mark_dependents(world, depComp, ecs_comp_id(AssetInstantUnloadComp));
    }
  }
}

ecs_view_define(AssetCacheView) {
  ecs_access_read(AssetComp);
  ecs_access_read(AssetCacheRequest);
}

ecs_system_define(AssetCacheSys) {
  const AssetManagerComp* manager = asset_manager_readonly(world);
  if (!manager) {
    return;
  }

  AssetRepoDep deps[256];
  usize        depCount = 0;

  EcsView* cacheView = ecs_world_view_t(world, AssetCacheView);
  for (EcsIterator* itr = ecs_view_itr(cacheView); ecs_view_walk(itr);) {
    const EcsEntityId        assetEntity = ecs_view_entity(itr);
    const AssetComp*         assetComp   = ecs_view_read_t(itr, AssetComp);
    const AssetCacheRequest* request     = ecs_view_read_t(itr, AssetCacheRequest);

    diag_assert(assetComp->loadCount); // Caching an asset without loading it makes no sense.

    // Collect asset data.
    const String   id      = assetComp->id;
    const Mem      blob    = mem_slice(request->blobMem, 0, request->blobSize);
    const TimeReal modTime = assetComp->loadModTime;

    // Collect asset dependencies.
    depCount = 0;

    // Save the asset in the repo cache.
    asset_repo_cache(manager->repo, id, request->blobMeta, modTime, blob, deps, depCount);

    ecs_world_remove_t(world, assetEntity, AssetCacheRequest);
  }
}

ecs_module_init(asset_manager_module) {
  ecs_register_comp(AssetManagerComp, .destructor = ecs_destruct_manager_comp, .destructOrder = 30);
  ecs_register_comp(AssetComp);
  ecs_register_comp_empty(AssetFailedComp);
  ecs_register_comp_empty(AssetLoadedComp);
  ecs_register_comp_empty(AssetChangedComp);
  ecs_register_comp_empty(AssetInstantUnloadComp);
  ecs_register_comp(AssetDirtyComp, .combinator = ecs_combine_asset_dirty);
  ecs_register_comp(
      AssetDependencyComp,
      .destructor = ecs_destruct_asset_dependency,
      .combinator = ecs_combine_asset_dependency);
  ecs_register_comp(AssetCacheRequest, .destructor = ecs_destruct_cache_request_comp);

  ecs_register_view(DirtyAssetView);
  ecs_register_view(AssetDependencyView);
  ecs_register_view(GlobalView);

  ecs_register_system(AssetUpdateDirtySys, ecs_view_id(DirtyAssetView), ecs_view_id(GlobalView));
  ecs_parallel(AssetUpdateDirtySys, asset_num_load_tasks);
  ecs_order(AssetUpdateDirtySys, AssetOrder_Update);

  ecs_register_system(
      AssetPollChangedSys, ecs_view_id(AssetDependencyView), ecs_view_id(GlobalView));

  ecs_register_system(AssetCacheSys, ecs_register_view(AssetCacheView), ecs_view_id(GlobalView));
}

String asset_id(const AssetComp* comp) { return comp->id; }

bool asset_path(const AssetManagerComp* manager, const AssetComp* asset, DynString* out) {
  return asset_repo_path(manager->repo, asset->id, out);
}

bool asset_path_by_id(const AssetManagerComp* manager, const String id, DynString* out) {
  return asset_repo_path(manager->repo, id, out);
}

AssetManagerComp*
asset_manager_create_fs(EcsWorld* world, const AssetManagerFlags flags, const String rootPath) {
  return asset_manager_create_internal(world, asset_repo_create_fs(rootPath), flags);
}

AssetManagerComp* asset_manager_create_mem(
    EcsWorld*               world,
    const AssetManagerFlags flags,
    const AssetMemRecord*   records,
    const usize             recordCount) {
  return asset_manager_create_internal(world, asset_repo_create_mem(records, recordCount), flags);
}

EcsEntityId asset_lookup(EcsWorld* world, AssetManagerComp* manager, const String id) {
  diag_assert_msg(!string_is_empty(id), "Asset id cannot be empty");

  const AssetEntry tgt = {.idHash = string_hash(id)};
  AssetEntry* entry = dynarray_find_or_insert_sorted(&manager->lookup, asset_compare_entry, &tgt);

  if (entry->idHash != tgt.idHash) {
    diag_assert(!entry->idHash && !entry->asset);
    entry->idHash = tgt.idHash;
    entry->asset  = asset_entity_create(world, manager->idAlloc, id);
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
  /**
   * Mark the asset as changed and bypass the normal unload delay.
   * NOTE: Does not mark dependent assets as changed.
   */
  ecs_utils_maybe_add_t(world, assetEntity, AssetChangedComp);
  ecs_utils_maybe_add_t(world, assetEntity, AssetInstantUnloadComp);
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
   * Track the upwards dependency ('asset' being dependent on 'dependency'), so when the
   * 'dependency' changes we also mark the 'asset' as changed.
   */
  ecs_world_add_t(
      world,
      dependency,
      AssetDependencyComp,
      .dependents.type   = AssetDependencyStorage_Single,
      .dependents.single = asset);
}

AssetSource* asset_source_open(const AssetManagerComp* manager, const String id) {
  return asset_repo_source_open(manager->repo, id);
}

EcsEntityId asset_watch(EcsWorld* world, AssetManagerComp* manager, const String id) {
  const EcsEntityId assetEntity = asset_lookup(world, manager, id);

  if (manager->flags & AssetManagerFlags_TrackChanges) {
    asset_repo_changes_watch(manager->repo, id, (u64)assetEntity);
  }

  return assetEntity;
}

void asset_cache(
    EcsWorld* world, const EcsEntityId asset, const DataMeta dataMeta, const Mem data) {
  DynString blobBuffer = dynstring_create(g_allocHeap, 256);
  data_write_bin(g_dataReg, &blobBuffer, dataMeta, data);

  ecs_world_add_t(
      world,
      asset,
      AssetCacheRequest,
      .blobMeta = dataMeta,
      .blobSize = blobBuffer.size,
      .blobMem  = blobBuffer.data);
}
