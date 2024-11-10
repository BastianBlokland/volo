#pragma once
#include "import_internal.h"
#include "loader_texture_internal.h"

typedef enum {
  AssetImportTextureFlags_None      = 0,
  AssetImportTextureFlags_NormalMap = 1 << 0,
  AssetImportTextureFlags_Lossless  = 1 << 1,
  AssetImportTextureFlags_Linear    = 1 << 2,
  AssetImportTextureFlags_Mips      = 1 << 3,
} AssetImportTextureFlags;

typedef struct {
  AssetImportTextureFlags flags;
  u32                     channels;
  AssetTextureType        pixelType;
  u32                     width, height;
} AssetImportTexture;

bool asset_import_texture(const AssetImportEnvComp*, String id, AssetImportTexture*);
