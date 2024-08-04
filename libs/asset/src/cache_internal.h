#pragma once
#include "core_string.h"
#include "data_registry.h"

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

// Forward declare from 'core_file.h'.
typedef struct sFile File;

// Forward declare from 'core_time.h'.
typedef i64 TimeReal;

typedef struct sAssetCache AssetCache;

extern DataMeta g_assetCacheDataDef;

AssetCache* asset_cache_create(Allocator*, String rootPath);
void        asset_cache_destroy(AssetCache*);
void        asset_cache_flush(AssetCache*);

/**
 * Save the given blob in the cache.
 * NOTE: Overwrites any existing blobs with the same id.
 */
void asset_cache_set(AssetCache*, String id, DataMeta blobMeta, TimeReal blobModTime, Mem blob);

/**
 * Lookup a file containing a cache blob with the given id.
 * NOTE: Returns null if no compatible entry with the same id was found.
 */
File* asset_cache_get(AssetCache*, String id);
