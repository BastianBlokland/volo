#pragma once
#include "asset_manager.h"
#include "data_registry.h"

#include "format_internal.h"

// Forward declare from 'core_time.h'.
typedef i64 TimeReal;

typedef struct sAssetRepo   AssetRepo;
typedef struct sAssetSource AssetSource;

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
  AssetSource* (*open)(AssetRepo*, String id);
  bool (*save)(AssetRepo*, String id, String data);
  void (*destroy)(AssetRepo*);
  void (*changesWatch)(AssetRepo*, String id, u64 userData);
  bool (*changesPoll)(AssetRepo*, u64* outUserData);
  AssetRepoQueryResult (*query)(AssetRepo*, String pattern, void* ctx, AssetRepoQueryHandler);
  void (*cache)(AssetRepo*, String id, DataMeta blobMeta, Mem blob);
};

struct sAssetSource {
  String      data;
  AssetFormat format;
  TimeReal    modTime;

  void (*close)(AssetSource*);
};

String asset_repo_query_result_str(AssetRepoQueryResult);

AssetRepo* asset_repo_create_fs(String rootPath);
AssetRepo* asset_repo_create_mem(const AssetMemRecord* records, usize recordCount);
void       asset_repo_destroy(AssetRepo*);

bool         asset_repo_path(AssetRepo* repo, String id, DynString* out);
AssetSource* asset_repo_source_open(AssetRepo*, String id);
bool         asset_repo_save(AssetRepo*, String id, String data);
void         asset_repo_source_close(AssetSource*);

void                 asset_repo_changes_watch(AssetRepo*, String id, u64 userData);
bool                 asset_repo_changes_poll(AssetRepo*, u64* outUserData);
AssetRepoQueryResult asset_repo_query(AssetRepo*, String pattern, void* ctx, AssetRepoQueryHandler);

void asset_repo_cache(AssetRepo*, String id, DataMeta blobMeta, Mem blob);
