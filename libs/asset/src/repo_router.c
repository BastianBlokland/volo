#include "core/alloc.h"
#include "log/logger.h"

#include "repo.h"

#define asset_router_lanes_max 4

typedef struct {
  AssetRepo  api;
  u32        laneCount;
  String     laneIds[asset_router_lanes_max];
  AssetRepo* laneRepos[asset_router_lanes_max];
} AssetRepoRouter;

static String asset_repo_router_id(const AssetRepoRouter* router, const u32 lane, const String id) {
  if (string_is_empty(router->laneIds[lane])) {
    return id;
  }
  return fmt_write_scratch("{}:{}", fmt_text(router->laneIds[lane]), fmt_text(id));
}

typedef struct {
  u32    lane;
  String innerId;
} AssetRepoLaneEntry;

static bool
asset_repo_router_find(AssetRepoRouter* router, const String id, AssetRepoLaneEntry* out) {
  // Find a named lane that matches the id.
  for (u32 i = 0; i != router->laneCount; ++i) {
    if (string_is_empty(router->laneIds[i])) {
      continue; // Not a named lane.
    }
    if (id.size < router->laneIds[i].size + 2) {
      continue; // Too small to fit the id + ':' + 1 char of inner id.
    }
    if (!string_starts_with(id, router->laneIds[i])) {
      continue; // Not a match.
    }
    if (*string_at(id, router->laneIds[i].size) != ':') {
      continue; // Not a valid lane prefix.
    }
    out->lane    = i;
    out->innerId = string_consume(id, router->laneIds[i].size + 1);
    return true;
  }

  // Use any unnamed lane.
  for (u32 i = 0; i != router->laneCount; ++i) {
    if (string_is_empty(router->laneIds[i])) {
      out->lane    = i;
      out->innerId = id;
      return true;
    }
  }

  return false;
}

static bool asset_source_router_path(AssetRepo* repo, const String id, DynString* out) {
  AssetRepoRouter* repoRouter = (AssetRepoRouter*)repo;

  AssetRepoLaneEntry laneEntry;
  if (asset_repo_router_find(repoRouter, id, &laneEntry)) {
    return asset_repo_path(repoRouter->laneRepos[laneEntry.lane], laneEntry.innerId, out);
  }

  return false;
}

static bool asset_source_router_stat(
    AssetRepo* repo, const String id, const AssetRepoLoaderHasher loaderHasher, AssetInfo* out) {

  AssetRepoRouter* repoRouter = (AssetRepoRouter*)repo;

  AssetRepoLaneEntry laneEntry;
  if (asset_repo_router_find(repoRouter, id, &laneEntry)) {
    return asset_repo_stat(
        repoRouter->laneRepos[laneEntry.lane], laneEntry.innerId, loaderHasher, out);
  }

  return false;
}

static AssetSource* asset_source_router_open(
    AssetRepo* repo, const String id, const AssetRepoLoaderHasher loaderHasher) {

  AssetRepoRouter* repoRouter = (AssetRepoRouter*)repo;

  AssetRepoLaneEntry laneEntry;
  if (asset_repo_router_find(repoRouter, id, &laneEntry)) {
    return asset_repo_open(repoRouter->laneRepos[laneEntry.lane], laneEntry.innerId, loaderHasher);
  }

  return null;
}

static bool asset_repo_router_save(AssetRepo* repo, const String id, const String data) {
  AssetRepoRouter* repoRouter = (AssetRepoRouter*)repo;

  AssetRepoLaneEntry laneEntry;
  if (asset_repo_router_find(repoRouter, id, &laneEntry)) {
    return asset_repo_save(repoRouter->laneRepos[laneEntry.lane], laneEntry.innerId, data);
  }

  return false;
}

static void asset_repo_router_changes_watch(AssetRepo* repo, const String id, const u64 userData) {
  AssetRepoRouter* repoRouter = (AssetRepoRouter*)repo;

  AssetRepoLaneEntry laneEntry;
  if (asset_repo_router_find(repoRouter, id, &laneEntry)) {
    asset_repo_changes_watch(repoRouter->laneRepos[laneEntry.lane], laneEntry.innerId, userData);
  }
}

static bool asset_repo_router_changes_poll(AssetRepo* repo, u64* outUserData) {
  AssetRepoRouter* repoRouter = (AssetRepoRouter*)repo;

  for (u32 i = 0; i != repoRouter->laneCount; ++i) {
    if (asset_repo_changes_poll(repoRouter->laneRepos[i], outUserData)) {
      return true;
    }
  }
  return false;
}

typedef struct {
  const AssetRepoRouter* router;
  u32                    lane;
  void*                  userCtx;
  AssetRepoQueryHandler  userHandler;
} RouterQueryContext;

typedef void (*AssetRepoQueryHandler)(void* ctx, String assetId);

static void asset_repo_router_query_handler(void* ctx, const String assetId) {
  RouterQueryContext* queryCtx = ctx;
  queryCtx->userHandler(
      queryCtx->userCtx, asset_repo_router_id(queryCtx->router, queryCtx->lane, assetId));
}

static AssetRepoQueryResult asset_repo_router_query(
    AssetRepo* repo, const String pattern, void* ctx, const AssetRepoQueryHandler handler) {
  AssetRepoRouter* repoRouter = (AssetRepoRouter*)repo;

  RouterQueryContext queryCtx = {
      .router      = repoRouter,
      .userCtx     = ctx,
      .userHandler = handler,
  };

  for (; queryCtx.lane != repoRouter->laneCount; ++queryCtx.lane) {
    AssetRepoQueryResult result = asset_repo_query(
        repoRouter->laneRepos[queryCtx.lane], pattern, &queryCtx, asset_repo_router_query_handler);
    if (result != AssetRepoQueryResult_Success) {
      return result;
    }
  }

  return AssetRepoQueryResult_Success;
}

static void asset_repo_router_cache(
    AssetRepo*          repo,
    const Mem           blob,
    const DataMeta      blobMeta,
    const AssetRepoDep* source,
    const AssetRepoDep* deps,
    const usize         depCount) {
  AssetRepoRouter* repoRouter = (AssetRepoRouter*)repo;

  AssetRepoLaneEntry sourceLaneEntry;
  if (asset_repo_router_find(repoRouter, source->id, &sourceLaneEntry)) {

    const AssetRepoDep wrappedSource = {
        .id         = sourceLaneEntry.innerId,
        .checksum   = source->checksum,
        .loaderHash = source->loaderHash,
        .modTime    = source->modTime,
    };

    AssetRepoDep* wrappedDeps =
        depCount ? alloc_array_t(g_allocScratch, AssetRepoDep, depCount) : null;
    for (u32 i = 0; i != depCount; ++i) {
      AssetRepoLaneEntry depLaneEntry;
      if (!asset_repo_router_find(repoRouter, deps[i].id, &depLaneEntry)) {
        log_e("No lane found for cache dependency", log_param("id", fmt_text(deps[i].id)));
        return;
      }
      wrappedDeps[i] = (AssetRepoDep){
          .id         = depLaneEntry.innerId,
          .checksum   = deps[i].checksum,
          .loaderHash = deps[i].loaderHash,
          .modTime    = deps[i].modTime,
      };
    }

    asset_repo_cache(
        repoRouter->laneRepos[sourceLaneEntry.lane],
        blob,
        blobMeta,
        &wrappedSource,
        wrappedDeps,
        depCount);
  }
}

static usize asset_repo_router_cache_deps(
    AssetRepo*   repo,
    const String id,
    AssetRepoDep out[PARAM_ARRAY_SIZE(asset_repo_cache_deps_max)]) {
  AssetRepoRouter* repoRouter = (AssetRepoRouter*)repo;

  AssetRepoLaneEntry laneEntry;
  if (asset_repo_router_find(repoRouter, id, &laneEntry)) {
    return asset_repo_cache_deps(repoRouter->laneRepos[laneEntry.lane], laneEntry.innerId, out);
  }
  return 0;
}

static void asset_repo_router_destroy(AssetRepo* repo) {
  AssetRepoRouter* repoRouter = (AssetRepoRouter*)repo;

  for (u32 i = 0; i != repoRouter->laneCount; ++i) {
    string_maybe_free(g_allocHeap, repoRouter->laneIds[i]);
    asset_repo_destroy(repoRouter->laneRepos[i]);
  }

  alloc_free_t(g_allocHeap, repoRouter);
}

AssetRepo* asset_repo_create_router(const AssetRepoLane lanes[], const u32 laneCount) {
  if (UNLIKELY(!laneCount)) {
    log_e("Not enough lanes in router");
    return null;
  }
  if (UNLIKELY(laneCount > asset_router_lanes_max)) {
    log_e("Too many lanes in router");
    return null;
  }

  AssetRepoRouter* repo = alloc_alloc_t(g_allocHeap, AssetRepoRouter);

  *repo = (AssetRepoRouter){
      .api =
          {
              .path         = asset_source_router_path,
              .stat         = asset_source_router_stat,
              .open         = asset_source_router_open,
              .save         = asset_repo_router_save,
              .changesWatch = asset_repo_router_changes_watch,
              .changesPoll  = asset_repo_router_changes_poll,
              .destroy      = asset_repo_router_destroy,
              .query        = asset_repo_router_query,
              .cache        = asset_repo_router_cache,
              .cacheDeps    = asset_repo_router_cache_deps,
          },
  };

  repo->laneCount = laneCount;
  for (u32 i = 0; i != laneCount; ++i) {
    repo->laneIds[i]   = string_maybe_dup(g_allocHeap, lanes[i].id);
    repo->laneRepos[i] = lanes[i].repo;
  }

  return (AssetRepo*)repo;
}
