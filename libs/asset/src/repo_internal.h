#pragma once
#include "asset_manager.h"

#include "format_internal.h"

typedef struct sAssetRepo   AssetRepo;
typedef struct sAssetSource AssetSource;

struct sAssetRepo {
  AssetSource* (*open)(AssetRepo*, String id);
  void (*destroy)(AssetRepo*);
  void (*changesWatch)(AssetRepo*, String id, u64 userData);
  bool (*changesPoll)(AssetRepo*, u64* outUserData);
};

struct sAssetSource {
  String      data;
  AssetFormat format;
  void (*close)(AssetSource*);
};

AssetRepo* asset_repo_create_fs(String rootPath);
AssetRepo* asset_repo_create_mem(const AssetMemRecord* records, usize recordCount);
void       asset_repo_destroy(AssetRepo*);

AssetSource* asset_repo_source_open(AssetRepo*, String id);
void         asset_repo_source_close(AssetSource*);

void asset_repo_changes_watch(AssetRepo*, String id, u64 userData);
bool asset_repo_changes_poll(AssetRepo*, u64* outUserData);
