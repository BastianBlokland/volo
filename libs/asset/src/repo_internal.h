#pragma once
#include "asset_manager.h"
#include "data_registry.h"

#include "format_internal.h"

// Forward declare from 'core_time.h'.
typedef i64 TimeReal;

#define asset_repo_cache_deps_max 256

typedef struct sAssetRepo   AssetRepo;
typedef struct sAssetSource AssetSource;

typedef struct {
  String   id;
  TimeReal modTime;
  u32      loaderHash;
} AssetRepoDep;

/**
 * Utility to compute a hash of the loader (NOT a hash of the asset itself) for the given asset-id.
 * When the loader hash changes any cached versions of this asset are invalidated.
 */
typedef struct {
  const void* ctx;
  u32 (*computeHash)(const void* ctx, String assetId);
} AssetRepoLoaderHasher;

typedef void (*AssetRepoQueryHandler)(void* ctx, String assetId);

typedef enum {
  AssetRepoQueryResult_Success,
  AssetRepoQueryResult_ErrorNotSupported,
  AssetRepoQueryResult_ErrorPatternNotSupported,
  AssetRepoQueryResult_ErrorWhileQuerying,

  AssetRepoQueryResult_Count,
} AssetRepoQueryResult;

/**
 * Asset repository.
 * NOTE: Api is thread-safe.
 */
struct sAssetRepo {
  bool (*path)(AssetRepo*, String id, DynString* out);
  AssetSource* (*open)(AssetRepo*, String id, AssetRepoLoaderHasher);
  bool (*save)(AssetRepo*, String id, String data);
  void (*destroy)(AssetRepo*);
  void (*changesWatch)(AssetRepo*, String id, u64 userData);
  bool (*changesPoll)(AssetRepo*, u64* outUserData);
  AssetRepoQueryResult (*query)(AssetRepo*, String pattern, void* ctx, AssetRepoQueryHandler);
  void (*cache)(
      AssetRepo*,
      String              id,
      DataMeta            blobMeta,
      TimeReal            blobModTime,
      u32                 blobLoaderHash,
      Mem                 blob,
      const AssetRepoDep* deps,
      usize               depCount);
  usize (*cacheDeps)(
      AssetRepo*, String id, AssetRepoDep out[PARAM_ARRAY_SIZE(asset_repo_cache_deps_max)]);
};

typedef enum {
  AssetSourceFlags_None   = 0,
  AssetSourceFlags_Cached = 1 << 0,
} AssetSourceFlags;

struct sAssetSource {
  String           data;
  AssetFormat      format;
  AssetSourceFlags flags;
  TimeReal         modTime;

  void (*close)(AssetSource*);
};

String asset_repo_query_result_str(AssetRepoQueryResult);

AssetRepo* asset_repo_create_fs(String rootPath);
AssetRepo* asset_repo_create_mem(const AssetMemRecord* records, usize recordCount);
void       asset_repo_destroy(AssetRepo*);

bool         asset_repo_path(AssetRepo* repo, String id, DynString* out);
AssetSource* asset_repo_source_open(AssetRepo*, String id, AssetRepoLoaderHasher);
bool         asset_repo_save(AssetRepo*, String id, String data);
void         asset_repo_source_close(AssetSource*);

void                 asset_repo_changes_watch(AssetRepo*, String id, u64 userData);
bool                 asset_repo_changes_poll(AssetRepo*, u64* outUserData);
AssetRepoQueryResult asset_repo_query(AssetRepo*, String pattern, void* ctx, AssetRepoQueryHandler);

void asset_repo_cache(
    AssetRepo*,
    String              id,
    DataMeta            blobMeta,
    TimeReal            blobModTime,
    u32                 blobLoaderHash,
    Mem                 blob,
    const AssetRepoDep* deps,
    usize               depCount);

usize asset_repo_cache_deps(
    AssetRepo*, String id, AssetRepoDep out[PARAM_ARRAY_SIZE(asset_repo_cache_deps_max)]);
