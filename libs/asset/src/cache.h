#pragma once
#include "core/string.h"
#include "data/registry.h"

#include "repo.h"

typedef struct sAssetCache AssetCache;

typedef enum {
  AssetCacheFlags_None     = 0,
  AssetCacheFlags_Portable = 1 << 0, // Support a cache produced on a different asset directory.
} AssetCacheFlags;

extern DataMeta g_assetCacheMeta;

AssetCache* asset_cache_create(Allocator*, String rootPath, AssetCacheFlags);
void        asset_cache_destroy(AssetCache*);
void        asset_cache_flush(AssetCache*);

/**
 * Save the given blob in the cache.
 * NOTE: Overwrites any existing blobs with the same id.
 */
void asset_cache_set(
    AssetCache*,
    Mem                 blob,
    DataMeta            blobMeta,
    const AssetRepoDep* source,
    const AssetRepoDep* deps,
    usize               depCount);

typedef struct {
  File*    blobFile; // NOTE: Caller is responsible for destroying the handle.
  DataMeta meta;
  TimeReal sourceModTime;
  u32      sourceLoaderHash;
  u32      sourceChecksum;
} AssetCacheRecord;

/**
 * Lookup a cache record containing with the given id.
 * Returns true when a compatible cache entry was found.
 * NOTE: When successful the caller is responsible for destroying the blob file handle.
 */
bool asset_cache_get(AssetCache*, String id, AssetRepoLoaderHasher, AssetCacheRecord* out);

/**
 * Lookup cache dependencies for the given id.
 * Returns the amount of dependencies written to the out pointer.
 */
usize asset_cache_deps(
    AssetCache*, String id, AssetRepoDep out[PARAM_ARRAY_SIZE(asset_repo_cache_deps_max)]);
