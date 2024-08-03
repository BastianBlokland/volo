#pragma once
#include "core_string.h"

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

typedef struct sAssetCache AssetCache;

AssetCache* asset_cache_create(Allocator*, String path);
void        asset_cache_destroy(AssetCache*);
