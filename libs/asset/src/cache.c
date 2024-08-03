#include "core_alloc.h"
#include "core_diag.h"
#include "core_file.h"

#include "cache_internal.h"

typedef struct {
  u32 dummy;
} AssetCacheRegistry;

struct sAssetCache {
  Allocator*         alloc;
  String             path;
  bool               error;
  AssetCacheRegistry registry;
};

DataMeta g_assetCacheDataDef;

static void cache_ensure_dir(AssetCache* cache) {
  const FileResult createRes = file_create_dir_sync(cache->path);
  if (UNLIKELY(createRes != FileResult_Success && createRes != FileResult_AlreadyExists)) {
    cache->error = true;
  }
}

void asset_data_init_cache(void) {
  // clang-format off
  data_reg_struct_t(g_dataReg, AssetCacheRegistry);
  data_reg_field_t(g_dataReg, AssetCacheRegistry, dummy, data_prim_t(u32));
  // clang-format on

  g_assetCacheDataDef = data_meta_t(t_AssetCacheRegistry);
}

AssetCache* asset_cache_create(Allocator* alloc, const String path) {
  diag_assert(!string_is_empty(path));

  AssetCache* cache = alloc_alloc_t(alloc, AssetCache);

  *cache = (AssetCache){
      .alloc = alloc,
      .path  = string_dup(alloc, path),
  };

  cache_ensure_dir(cache);

  return cache;
}

void asset_cache_destroy(AssetCache* cache) {
  string_free(cache->alloc, cache->path);
  alloc_free_t(cache->alloc, cache);
}
