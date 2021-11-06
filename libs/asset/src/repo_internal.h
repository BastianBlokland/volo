#pragma once
#include "asset_manager.h"

typedef enum {
  AssetFormat_Raw,
  AssetFormat_Tga,

  AssetFormat_Count,
} AssetFormat;

typedef struct sAssetRepo   AssetRepo;
typedef struct sAssetSource AssetSource;

struct sAssetRepo {
  AssetSource* (*open)(AssetRepo*, String id);
  void (*destroy)(AssetRepo*);
};

struct sAssetSource {
  String      data;
  AssetFormat format;
  void (*close)(AssetSource*);
};

AssetRepo*   asset_repo_create_fs(String rootPath);
AssetRepo*   asset_repo_create_mem(AssetMemRecord* records, usize recordCount);
void         asset_repo_destroy(AssetRepo*);
AssetSource* asset_source_open(AssetRepo*, String id);
void         asset_source_close(AssetSource*);
AssetFormat  asset_format_from_ext(String ext);
