#include "core_alloc.h"

#include "cache_internal.h"

struct sAssetCache {
  Allocator* alloc;
};

AssetCache* asset_cache_create(Allocator* alloc) {
  AssetCache* cache = alloc_alloc_t(alloc, AssetCache);

  *cache = (AssetCache){
      .alloc = alloc,
  };

  return cache;
}

void asset_cache_destroy(AssetCache* cache) { alloc_free_t(cache->alloc, cache); }
