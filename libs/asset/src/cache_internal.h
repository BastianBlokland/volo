#pragma once

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

typedef struct sAssetCache AssetCache;

AssetCache* asset_cache_create(Allocator*);
void        asset_cache_destroy(AssetCache*);
