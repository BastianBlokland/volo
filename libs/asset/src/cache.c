#include "core_alloc.h"
#include "core_diag.h"
#include "core_file.h"
#include "core_path.h"
#include "data.h"
#include "log_logger.h"

#include "cache_internal.h"

static const String g_assetCacheRegName = string_static("registry.blob");

typedef struct {
  u32 dummy;
} AssetCacheRegistry;

struct sAssetCache {
  Allocator*         alloc;
  String             path;
  bool               error;
  AssetCacheRegistry reg;
  File*              regFile;
};

DataMeta g_assetCacheDataDef;

static bool cache_ensure_dir(AssetCache* cache) {
  const FileResult createRes = file_create_dir_sync(cache->path);
  if (UNLIKELY(createRes != FileResult_Success && createRes != FileResult_AlreadyExists)) {
    log_e(
        "Failed to create asset cache dir",
        log_param("path", fmt_path(cache->path)),
        log_param("error", fmt_text(file_result_str(createRes))));
    return false;
  }
  return true;
}

static bool cache_registry_open(AssetCache* cache) {
  const String          regPath   = path_build_scratch(cache->path, g_assetCacheRegName);
  const FileAccessFlags regAccess = FileAccess_Read | FileAccess_Write;

  FileResult fileRes;
  fileRes = file_create(cache->alloc, regPath, FileMode_Open, regAccess, &cache->regFile);
  if (fileRes == FileResult_NotFound) {
    return false;
  }
  if (UNLIKELY(fileRes != FileResult_Success)) {
    log_w(
        "Failed to open asset cache registry",
        log_param("path", fmt_path(regPath)),
        log_param("error", fmt_text(file_result_str(fileRes))));
    return false;
  }

  String data;
  fileRes = file_map(cache->regFile, &data);
  if (UNLIKELY(fileRes != FileResult_Success)) {
    log_w(
        "Failed to map asset cache registry",
        log_param("path", fmt_path(regPath)),
        log_param("error", fmt_text(file_result_str(fileRes))));
    file_destroy(cache->regFile);
    return false;
  }

  DataReadResult readRes;
  data_read_bin(g_dataReg, data, cache->alloc, g_assetCacheDataDef, mem_var(cache->reg), &readRes);
  if (UNLIKELY(readRes.error)) {
    log_w(
        "Failed to read asset cache registry",
        log_param("path", fmt_path(regPath)),
        log_param("error", fmt_text(readRes.errorMsg)));
    file_destroy(cache->regFile);
    return false;
  }

  file_unmap(cache->regFile);
  return true;
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

  if (!cache_ensure_dir(cache)) {
    cache->error = true;
    goto Ret;
  }
  if (!cache_registry_open(cache)) {
    cache->error = true;
    goto Ret;
  }

Ret:
  return cache;
}

void asset_cache_destroy(AssetCache* cache) {
  if (cache->regFile) {
    // TODO: Flush changes.
    file_destroy(cache->regFile);
  }
  data_destroy(g_dataReg, cache->alloc, g_assetCacheDataDef, mem_var(cache->reg));

  string_free(cache->alloc, cache->path);
  alloc_free_t(cache->alloc, cache);
}
