#pragma once
#include "asset_manager.h"
#include "data_registry.h"

#include "format_internal.h"

#define asset_repo_cache_deps_max 256

typedef struct sAssetRepo   AssetRepo;
typedef struct sAssetSource AssetSource;

typedef enum {
  AssetInfoFlags_None   = 0,
  AssetInfoFlags_Cached = 1 << 0,
} AssetInfoFlags;

typedef struct sAssetInfo {
  AssetFormat    format;
  AssetInfoFlags flags;
  usize          size;
  TimeReal       modTime;
} AssetInfo;

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
  bool (*stat)(AssetRepo*, String id, AssetRepoLoaderHasher, AssetInfo* out);
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

struct sAssetSource {
  String         data;
  AssetFormat    format;
  AssetInfoFlags flags;
  TimeReal       modTime;

  void (*close)(AssetSource*);
};

String asset_repo_query_result_str(AssetRepoQueryResult);

AssetRepo* asset_repo_create_fs(String rootPath);
AssetRepo* asset_repo_create_pack(String filePath);
AssetRepo* asset_repo_create_mem(const AssetMemRecord* records, usize recordCount);
void       asset_repo_destroy(AssetRepo*);

bool         asset_repo_path(AssetRepo* repo, String id, DynString* out);
bool         asset_repo_stat(AssetRepo*, String id, AssetRepoLoaderHasher, AssetInfo* out);
AssetSource* asset_repo_open(AssetRepo*, String id, AssetRepoLoaderHasher);
void         asset_repo_close(AssetSource*);
bool         asset_repo_save(AssetRepo*, String id, String data);
bool         asset_repo_save_supported(const AssetRepo*);

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
