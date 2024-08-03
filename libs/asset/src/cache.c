#include "core_alloc.h"
#include "core_diag.h"
#include "core_file.h"
#include "core_path.h"
#include "core_thread.h"
#include "data.h"
#include "log_logger.h"

#include "cache_internal.h"

static const String g_assetCachePath    = string_static(".cache");
static const String g_assetCacheRegName = string_static("registry.blob");

typedef struct {
  String     id;
  StringHash idHash;
  u32        formatHash;
} AssetCacheEntry;

typedef struct {
  DynArray entries; // AssetCacheEntry[], sorted on idHash.
} AssetCacheRegistry;

struct sAssetCache {
  Allocator*         alloc;
  bool               error;
  String             rootPath;
  AssetCacheRegistry reg;
  ThreadMutex        regMutex;
  File*              regFile;
};

DataMeta g_assetCacheDataDef;

static i8 cache_compare_entry(const void* a, const void* b) {
  const AssetCacheEntry* entryA = a;
  const AssetCacheEntry* entryB = b;
  return compare_stringhash(&entryA->idHash, &entryB->idHash);
}

static bool cache_ensure_dir(AssetCache* c) {
  const String     path      = path_build_scratch(c->rootPath, g_assetCachePath);
  const FileResult createRes = file_create_dir_sync(path);
  if (UNLIKELY(createRes != FileResult_Success && createRes != FileResult_AlreadyExists)) {
    log_e(
        "Failed to create asset cache dir",
        log_param("path", fmt_path(path)),
        log_param("error", fmt_text(file_result_str(createRes))));
    return false;
  }
  return true;
}

static bool cache_reg_save(AssetCache* c) {
  bool result = true;

  DynString blobBuffer = dynstring_create(c->alloc, 256);
  data_write_bin(g_dataReg, &blobBuffer, g_assetCacheDataDef, mem_var(c->reg));

  const FileResult fileRes = file_write_sync(c->regFile, dynstring_view(&blobBuffer));
  if (UNLIKELY(fileRes != FileResult_Success)) {
    log_w(
        "Failed to save asset cache registry",
        log_param("error", fmt_text(file_result_str(fileRes))));
    result = false;
  }

  dynstring_destroy(&blobBuffer);
  return result;
}

static bool cache_reg_open(AssetCache* c) {
  diag_assert(!c->regFile);

  const String path = path_build_scratch(c->rootPath, g_assetCachePath, g_assetCacheRegName);
  const FileAccessFlags access = FileAccess_Read | FileAccess_Write;

  FileResult fileRes;
  fileRes = file_create(c->alloc, path, FileMode_Open, access, &c->regFile);
  if (fileRes == FileResult_NotFound) {
    return false;
  }
  if (UNLIKELY(fileRes != FileResult_Success)) {
    log_w(
        "Failed to open asset cache registry",
        log_param("path", fmt_path(path)),
        log_param("error", fmt_text(file_result_str(fileRes))));
    return false;
  }

  String data;
  fileRes = file_map(c->regFile, &data);
  if (UNLIKELY(fileRes != FileResult_Success)) {
    log_w(
        "Failed to map asset cache registry",
        log_param("path", fmt_path(path)),
        log_param("error", fmt_text(file_result_str(fileRes))));
    file_destroy(c->regFile);
    c->regFile = null;
    return false;
  }

  DataReadResult readRes;
  data_read_bin(g_dataReg, data, c->alloc, g_assetCacheDataDef, mem_var(c->reg), &readRes);
  if (UNLIKELY(readRes.error)) {
    log_w(
        "Failed to read asset cache registry",
        log_param("path", fmt_path(path)),
        log_param("error", fmt_text(readRes.errorMsg)));
    file_destroy(c->regFile);
    c->regFile = null;
    return false;
  }

  /**
   * Sort by idHash.
   * NOTE: Technically not necessary assuming the file was not tampered with.
   */
  dynarray_sort(&c->reg.entries, cache_compare_entry);

  log_i("Opened asset cache registry", log_param("path", fmt_path(path)));

  file_unmap(c->regFile);
  return true;
}

static bool cache_reg_create(AssetCache* c) {
  diag_assert(!c->regFile);

  const String path = path_build_scratch(c->rootPath, g_assetCachePath, g_assetCacheRegName);
  const FileAccessFlags access = FileAccess_Read | FileAccess_Write;

  FileResult fileRes;
  fileRes = file_create(c->alloc, path, FileMode_Create, access, &c->regFile);
  if (UNLIKELY(fileRes != FileResult_Success)) {
    log_e(
        "Failed to create asset cache registry",
        log_param("path", fmt_path(path)),
        log_param("error", fmt_text(file_result_str(fileRes))));
    return false;
  }

  c->reg = (AssetCacheRegistry){
      .entries = dynarray_create_t(c->alloc, AssetCacheEntry, 32),
  };

  return cache_reg_save(c);
}

static bool cache_reg_open_or_create(AssetCache* c) {
  if (cache_reg_open(c)) {
    return true;
  }
  return cache_reg_create(c);
}

/**
 * Pre-condition: cache->regMutex is held by this thread.
 */
static AssetCacheEntry* cache_reg_add(AssetCache* c, const String id, const StringHash idHash) {
  const AssetCacheEntry key = {.idHash = idHash};

  AssetCacheEntry* res = dynarray_find_or_insert_sorted(&c->reg.entries, cache_compare_entry, &key);

  if (res->idHash == idHash) {
    // Existing entry.
    diag_assert_msg(string_eq(res->id, id), "Asset id hash collision detected");
  } else {
    // New entry.
    res->id     = string_dup(c->alloc, id);
    res->idHash = idHash;
  }

  return res;
}

void asset_data_init_cache(void) {
  // clang-format off
  data_reg_struct_t(g_dataReg, AssetCacheEntry);
  data_reg_field_t(g_dataReg, AssetCacheEntry, id, data_prim_t(String));
  data_reg_field_t(g_dataReg, AssetCacheEntry, idHash, data_prim_t(u32));
  data_reg_field_t(g_dataReg, AssetCacheEntry, formatHash, data_prim_t(u32));

  data_reg_struct_t(g_dataReg, AssetCacheRegistry);
  data_reg_field_t(g_dataReg, AssetCacheRegistry, entries, t_AssetCacheEntry, .container = DataContainer_DynArray);
  // clang-format on

  g_assetCacheDataDef = data_meta_t(t_AssetCacheRegistry);
}

AssetCache* asset_cache_create(Allocator* alloc, const String rootPath) {
  diag_assert(!string_is_empty(rootPath));

  AssetCache* cache = alloc_alloc_t(alloc, AssetCache);

  *cache = (AssetCache){
      .alloc    = alloc,
      .rootPath = string_dup(alloc, rootPath),
      .regMutex = thread_mutex_create(alloc),
  };

  if (UNLIKELY(!cache_ensure_dir(cache))) {
    cache->error = true;
    goto Ret;
  }
  if (UNLIKELY(!cache_reg_open_or_create(cache))) {
    cache->error = true;
    goto Ret;
  }

Ret:
  return cache;
}

void asset_cache_destroy(AssetCache* cache) {
  if (!cache->error) {
    cache_reg_save(cache);
  }
  if (cache->regFile) {
    file_destroy(cache->regFile);
  }
  data_destroy(g_dataReg, cache->alloc, g_assetCacheDataDef, mem_var(cache->reg));
  thread_mutex_destroy(cache->regMutex);

  string_free(cache->alloc, cache->rootPath);
  alloc_free_t(cache->alloc, cache);
}

void asset_cache_add(AssetCache* cache, const String id, const DataMeta blobMeta, const Mem blob) {
  if (UNLIKELY(cache->error)) {
    return;
  }
  const StringHash idHash     = string_hash(id);
  const u32        formatHash = data_hash(g_dataReg, blobMeta, DataHashFlags_ExcludeIds);

  // Save the blob to disk.
  const String     blobName     = fmt_write_scratch("{}.blob", fmt_int(idHash));
  const String     blobPath     = path_build_scratch(cache->rootPath, g_assetCachePath, blobName);
  const FileResult blobWriteRes = file_write_to_path_sync(blobPath, blob);
  if (UNLIKELY(blobWriteRes != FileResult_Success)) {
    log_w(
        "Failed to cache blob",
        log_param("path", fmt_path(blobPath)),
        log_param("error", fmt_text(file_result_str(blobWriteRes))));
    return;
  }

  // Add an entry to the registry.
  thread_mutex_lock(cache->regMutex);
  {
    AssetCacheEntry* entry = cache_reg_add(cache, id, idHash);
    entry->formatHash      = formatHash;
  }
  thread_mutex_unlock(cache->regMutex);
}

void asset_cache_flush(AssetCache* cache) {
  if (UNLIKELY(cache->error)) {
    return;
  }
  thread_mutex_lock(cache->regMutex);

  cache_reg_save(cache);

  thread_mutex_unlock(cache->regMutex);
}
