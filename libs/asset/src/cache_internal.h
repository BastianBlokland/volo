#pragma once
#include "core_string.h"
#include "data_registry.h"

#include "repo_internal.h"

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

// Forward declare from 'core_file.h'.
typedef struct sFile File;

// Forward declare from 'core_time.h'.
typedef i64 TimeReal;

typedef struct sAssetCache AssetCache;

extern DataMeta g_assetCacheMeta;

AssetCache* asset_cache_create(Allocator*, String rootPath);
void        asset_cache_destroy(AssetCache*);
void        asset_cache_flush(AssetCache*);

/**
 * Save the given blob in the cache.
 * NOTE: Overwrites any existing blobs with the same id.
 */
void asset_cache_set(
    AssetCache*,
    String              id,
    DataMeta            blobMeta,
    TimeReal            blobModTime,
    Mem                 blob,
    const AssetRepoDep* deps,
    usize               depCount);

typedef struct {
  File*    blobFile; // NOTE: Caller is responsible for destroying the handle.
  DataMeta meta;
  TimeReal modTime;
  u32      importHash;
} AssetCacheRecord;

/**
 * Lookup a cache record containing with the given id.
 * Returns true when a compatible cache entry was found.
 * NOTE: When successful the caller is responsible for destroying the blob file handle.
 */
bool asset_cache_get(AssetCache*, String id, AssetCacheRecord* out);

/**
 * Lookup cache dependencies for the given id.
 * Returns the amount of dependencies written to the out pointer.
 * NOTE: Dependency ids are allocated in scratch memory; should not be stored.
 */
usize asset_cache_deps(
    AssetCache*, String id, AssetRepoDep out[PARAM_ARRAY_SIZE(asset_repo_cache_deps_max)]);
