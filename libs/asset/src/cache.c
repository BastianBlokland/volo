#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_file.h"
#include "core_math.h"
#include "core_path.h"
#include "core_thread.h"
#include "data.h"
#include "log_logger.h"
#include "trace_tracer.h"

#include "cache_internal.h"

static const String g_assetCachePath    = string_static(".cache");
static const String g_assetCacheRegName = string_static("registry.blob");

typedef struct {
  u32 typeNameHash; // Hash of the type's name.
  u32 formatHash;   // Deep hash of the type's format ('data_hash()').
  u8  container;    // DataContainer
  u8  flags;        // DataFlags
  u16 fixedCount;   // Size of fixed size containers (for example inline-array).
} AssetCacheMeta;

typedef struct {
  String   id;
  TimeReal modTime;
  u32      importHash;
} AssetCacheDependency;

typedef struct {
  String         id;
  StringHash     idHash;
  AssetCacheMeta meta;
  TimeReal       modTime;
  u32            importHash;
  HeapArray_t(AssetCacheDependency) dependencies;
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
  bool               regDirty;
  File*              regFile;
};

DataMeta g_assetCacheMeta;

static i8 cache_compare_entry(const void* a, const void* b) {
  const AssetCacheEntry* entryA = a;
  const AssetCacheEntry* entryB = b;
  return compare_stringhash(&entryA->idHash, &entryB->idHash);
}

static String cache_blob_path_scratch(AssetCache* c, const StringHash idHash) {
  const String blobName = fmt_write_scratch("{}.blob", fmt_int(idHash));
  return path_build_scratch(c->rootPath, g_assetCachePath, blobName);
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
  data_write_bin(g_dataReg, &blobBuffer, g_assetCacheMeta, mem_var(c->reg));

  FileResult fileRes;
  if ((fileRes = file_seek_sync(c->regFile, 0))) {
    log_w(
        "Failed to rewind asset cache registry file",
        log_param("error", fmt_text(file_result_str(fileRes))));
    result = false;
  }
  if ((fileRes = file_write_sync(c->regFile, dynstring_view(&blobBuffer)))) {
    log_w(
        "Failed to write asset cache registry",
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
  fileRes = file_map(c->regFile, &data, FileHints_Prefetch);
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
  data_read_bin(g_dataReg, data, c->alloc, g_assetCacheMeta, mem_var(c->reg), &readRes);
  if (UNLIKELY(readRes.error)) {
    log_w(
        "Failed to read asset cache registry",
        log_param("path", fmt_path(path)),
        log_param("error", fmt_text(readRes.errorMsg)));
    file_destroy(c->regFile);
    c->regFile = null;
    return false;
  }

  log_i(
      "Opened asset cache registry",
      log_param("path", fmt_path(path)),
      log_param("size", fmt_size(data.size)),
      log_param("entries", fmt_int(c->reg.entries.size)));

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
    *res = (AssetCacheEntry){
        .id     = string_dup(c->alloc, id),
        .idHash = idHash,
    };
  }
  return res;
}

/**
 * Pre-condition: cache->regMutex is held by this thread.
 */
static const AssetCacheEntry* cache_reg_get(AssetCache* c, const StringHash idHash) {
  const AssetCacheEntry key = {.idHash = idHash};
  return dynarray_search_binary(&c->reg.entries, cache_compare_entry, &key);
}

static bool cache_reg_validate_file(const AssetCache* c, const String id, const TimeReal modTime) {
  const String   sourcePath = path_build_scratch(c->rootPath, id);
  const FileInfo sourceInfo = file_stat_path_sync(sourcePath);
  if (sourceInfo.type != FileType_Regular) {
    return false; // Source file has been deleted.
  }
  if (sourceInfo.modTime > modTime) {
    return false; // Source file has been modified.
  }
  return true;
}

/**
 * Pre-condition: cache->regMutex is held by this thread.
 */
static bool cache_reg_validate(const AssetCache* c, const AssetCacheEntry* entry) {
  if (!cache_reg_validate_file(c, entry->id, entry->modTime)) {
    return false;
  }
  heap_array_for_t(entry->dependencies, AssetCacheDependency, dep) {
    if (!cache_reg_validate_file(c, dep->id, dep->modTime)) {
      return false;
    }
  }
  return true;
}

static AssetCacheMeta cache_meta_create(const DataReg* reg, const DataMeta meta) {
  return (AssetCacheMeta){
      .typeNameHash = data_name_hash(reg, meta.type),
      .formatHash   = data_hash(reg, meta, DataHashFlags_ExcludeIds),
      .container    = (u8)meta.container,
      .flags        = (u8)meta.flags,
  };
}

static bool cache_meta_resolve(const DataReg* reg, const AssetCacheMeta* cacheMeta, DataMeta* out) {
  const DataType type = data_type_from_name_hash(g_dataReg, cacheMeta->typeNameHash);
  if (UNLIKELY(!type)) {
    return false; // Type no longer exists with the same name.
  }
  const DataMeta dataMeta = {
      .type       = type,
      .container  = (DataContainer)cacheMeta->container,
      .flags      = (DataFlags)cacheMeta->flags,
      .fixedCount = cacheMeta->fixedCount,
  };
  if (UNLIKELY(cacheMeta->formatHash != data_hash(reg, dataMeta, DataHashFlags_ExcludeIds))) {
    return false; // Format has changed and is no longer compatible.
  }
  *out = dataMeta;
  return true;
}

void asset_data_init_cache(void) {
  // clang-format off
  data_reg_struct_t(g_dataReg, AssetCacheMeta);
  data_reg_field_t(g_dataReg, AssetCacheMeta, typeNameHash, data_prim_t(u32));
  data_reg_field_t(g_dataReg, AssetCacheMeta, formatHash, data_prim_t(u32));
  data_reg_field_t(g_dataReg, AssetCacheMeta, container, data_prim_t(u8));
  data_reg_field_t(g_dataReg, AssetCacheMeta, flags, data_prim_t(u8));
  data_reg_field_t(g_dataReg, AssetCacheMeta, fixedCount, data_prim_t(u16));

  data_reg_struct_t(g_dataReg, AssetCacheDependency);
  data_reg_field_t(g_dataReg, AssetCacheDependency, id, data_prim_t(String));
  data_reg_field_t(g_dataReg, AssetCacheDependency, modTime, data_prim_t(i64));
  data_reg_field_t(g_dataReg, AssetCacheDependency, importHash, data_prim_t(u32));

  data_reg_struct_t(g_dataReg, AssetCacheEntry);
  data_reg_field_t(g_dataReg, AssetCacheEntry, id, data_prim_t(String));
  data_reg_field_t(g_dataReg, AssetCacheEntry, idHash, data_prim_t(u32));
  data_reg_field_t(g_dataReg, AssetCacheEntry, meta, t_AssetCacheMeta);
  data_reg_field_t(g_dataReg, AssetCacheEntry, modTime, data_prim_t(i64));
  data_reg_field_t(g_dataReg, AssetCacheEntry, importHash, data_prim_t(u32));
  data_reg_field_t(g_dataReg, AssetCacheEntry, dependencies, t_AssetCacheDependency, .container = DataContainer_HeapArray);

  data_reg_struct_t(g_dataReg, AssetCacheRegistry);
  data_reg_field_t(g_dataReg, AssetCacheRegistry, entries, t_AssetCacheEntry, .container = DataContainer_DynArray);
  // clang-format on

  g_assetCacheMeta = data_meta_t(t_AssetCacheRegistry);
}

AssetCache* asset_cache_create(Allocator* alloc, const String rootPath) {
  diag_assert(!string_is_empty(rootPath));

  AssetCache* c = alloc_alloc_t(alloc, AssetCache);

  *c = (AssetCache){
      .alloc    = alloc,
      .rootPath = string_dup(alloc, rootPath),
      .regMutex = thread_mutex_create(alloc),
  };

  if (UNLIKELY(!cache_ensure_dir(c))) {
    c->error = true;
    goto Ret;
  }
  if (UNLIKELY(!cache_reg_open_or_create(c))) {
    c->error = true;
    goto Ret;
  }

Ret:
  return c;
}

void asset_cache_destroy(AssetCache* c) {
  if (c->regDirty && !c->error) {
    cache_reg_save(c);
  }
  if (c->regFile) {
    file_destroy(c->regFile);
  }
  data_destroy(g_dataReg, c->alloc, g_assetCacheMeta, mem_var(c->reg));
  thread_mutex_destroy(c->regMutex);

  string_free(c->alloc, c->rootPath);
  alloc_free_t(c->alloc, c);
}

void asset_cache_flush(AssetCache* c) {
  if (UNLIKELY(c->error)) {
    return;
  }
  thread_mutex_lock(c->regMutex);
  {
    if (c->regDirty && cache_reg_save(c)) {
      c->regDirty = false;
    }
  }
  thread_mutex_unlock(c->regMutex);
}

void asset_cache_set(
    AssetCache*         c,
    const String        id,
    const DataMeta      blobMeta,
    const TimeReal      blobModTime,
    const u32           blobImportHash,
    const Mem           blob,
    const AssetRepoDep* deps,
    const usize         depCount) {
  if (UNLIKELY(c->error)) {
    return;
  }
  const StringHash     idHash    = string_hash(id);
  const AssetCacheMeta cacheMeta = cache_meta_create(g_dataReg, blobMeta);

  // Save the blob to disk.
  const String     blobPath     = cache_blob_path_scratch(c, idHash);
  const FileResult blobWriteRes = file_write_to_path_atomic(blobPath, blob);
  if (UNLIKELY(blobWriteRes != FileResult_Success)) {
    log_w(
        "Failed to write asset cache blob",
        log_param("path", fmt_path(blobPath)),
        log_param("error", fmt_text(file_result_str(blobWriteRes))));
    return;
  }

  // Initialize the dependency array.
  AssetCacheDependency* cacheDependencies = null;
  if (depCount) {
    cacheDependencies = alloc_array_t(c->alloc, AssetCacheDependency, depCount);
    for (usize i = 0; i != depCount; ++i) {
      diag_assert(!string_is_empty(deps[i].id));
      cacheDependencies[i] = (AssetCacheDependency){
          .id         = string_dup(c->alloc, deps[i].id),
          .modTime    = deps[i].modTime,
          .importHash = deps[i].importHash,
      };
    }
  }

  // Add an entry to the registry.
  thread_mutex_lock(c->regMutex);
  {
    AssetCacheEntry* entry = cache_reg_add(c, id, idHash);
    entry->meta            = cacheMeta;
    entry->modTime         = blobModTime;
    entry->importHash      = blobImportHash;
    if (entry->dependencies.count) {
      // Cleanup the old dependencies.
      heap_array_for_t(entry->dependencies, AssetCacheDependency, dep) {
        string_free(c->alloc, dep->id);
      }
      alloc_free_array_t(c->alloc, entry->dependencies.values, entry->dependencies.count);
    }
    entry->dependencies.values = cacheDependencies;
    entry->dependencies.count  = depCount;

    c->regDirty = true;
  }
  thread_mutex_unlock(c->regMutex);
}

bool asset_cache_get(AssetCache* c, const String id, AssetCacheRecord* out) {
  if (UNLIKELY(c->error)) {
    return false;
  }
  trace_begin("asset_cache_get", TraceColor_Green);

  const StringHash idHash = string_hash(id);

  // Lookup an entry in the registry.
  bool success = false;
  thread_mutex_lock(c->regMutex);
  {
    const AssetCacheEntry* entry = cache_reg_get(c, idHash);
    if (entry) {
      diag_assert_msg(string_eq(entry->id, id), "Asset id hash collision detected");

      if (!cache_meta_resolve(g_dataReg, &entry->meta, &out->meta)) {
        goto Incompatible;
      }
      if (!cache_reg_validate(c, entry)) {
        goto Incompatible;
      }
      out->modTime    = entry->modTime;
      out->importHash = entry->importHash;
      success         = true;
    }
  Incompatible:;
  }
  thread_mutex_unlock(c->regMutex);

  // Open the blob file.
  if (success) {
    const String path = cache_blob_path_scratch(c, idHash);
    FileResult   fileRes;
    if ((fileRes = file_create(c->alloc, path, FileMode_Open, FileAccess_Read, &out->blobFile))) {
      log_w(
          "Failed to open asset cache blob",
          log_param("error", fmt_text(file_result_str(fileRes))));
      success = false;
    }
  }

  trace_end();

  return success;
}

usize asset_cache_deps(
    AssetCache* c, String id, AssetRepoDep out[PARAM_ARRAY_SIZE(asset_repo_cache_deps_max)]) {
  const StringHash idHash = string_hash(id);

  usize result = 0;
  thread_mutex_lock(c->regMutex);
  {
    const AssetCacheEntry* entry = cache_reg_get(c, idHash);
    if (entry) {
      result = math_min(entry->dependencies.count, asset_repo_cache_deps_max);
      for (usize i = 0; i != result; ++i) {
        out[i] = (AssetRepoDep){
            .id         = string_dup(g_allocScratch, entry->dependencies.values[i].id),
            .modTime    = entry->dependencies.values[i].modTime,
            .importHash = entry->dependencies.values[i].importHash,
        };
      }
    }
  }
  thread_mutex_unlock(c->regMutex);
  return result;
}
