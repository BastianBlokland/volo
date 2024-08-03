#pragma once
#include "core_string.h"
#include "data_registry.h"

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

typedef struct sAssetCache AssetCache;

extern DataMeta g_assetCacheDataDef;

AssetCache* asset_cache_create(Allocator*, String rootPath);
void        asset_cache_destroy(AssetCache*);
void        asset_cache_add(AssetCache*, String id, DataMeta blobMeta, Mem blob);
