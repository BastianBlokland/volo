#pragma once
#include "asset_manager.h"

typedef enum {
  AssetFormat_Mat,
  AssetFormat_Raw,
  AssetFormat_Spv,
  AssetFormat_Ppm,
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
AssetRepo*   asset_repo_create_mem(const AssetMemRecord* records, usize recordCount);
void         asset_repo_destroy(AssetRepo*);
AssetSource* asset_source_open(AssetRepo*, String id);
void         asset_source_close(AssetSource*);
String       asset_format_str(AssetFormat);
AssetFormat  asset_format_from_ext(String ext);
