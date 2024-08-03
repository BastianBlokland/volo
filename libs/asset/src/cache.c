#include "core_alloc.h"
#include "core_diag.h"

#include "cache_internal.h"

struct sAssetCache {
  Allocator* alloc;
  String     path;
};

AssetCache* asset_cache_create(Allocator* alloc, const String path) {
  diag_assert(!string_is_empty(path));

  AssetCache* cache = alloc_alloc_t(alloc, AssetCache);

  *cache = (AssetCache){
      .alloc = alloc,
      .path  = string_dup(alloc, path),
  };

  return cache;
}

void asset_cache_destroy(AssetCache* cache) {
  string_free(cache->alloc, cache->path);
  alloc_free_t(cache->alloc, cache);
}
