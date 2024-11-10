#pragma once
#include "import_internal.h"

typedef enum {
  AssetImportTextureFlags_NormalMap = 1 << 0,
  AssetImportTextureFlags_Lossless  = 1 << 1,
  AssetImportTextureFlags_Linear    = 1 << 2,
} AssetImportTextureFlags;

typedef struct {
  AssetImportTextureFlags flags;
} AssetImportTexture;

bool asset_import_texture(const AssetImportEnvComp*, String id, AssetImportTexture*);
