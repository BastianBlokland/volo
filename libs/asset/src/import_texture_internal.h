#pragma once
#include "import_internal.h"
#include "loader_texture_internal.h"

typedef enum {
  AssetImportTextureFlags_None     = 0,
  AssetImportTextureFlags_Lossless = 1 << 0,
  AssetImportTextureFlags_Linear   = 1 << 1,
  AssetImportTextureFlags_Mips     = 1 << 2,
} AssetImportTextureFlags;

typedef enum {
  AssetImportTextureTrans_None  = 0,
  AssetImportTextureTrans_FlipY = 1 << 0,
} AssetImportTextureTrans;

typedef struct {
  AssetImportTextureFlags flags;
  AssetImportTextureTrans trans;
  u32                     width, height;
  u32                     mips; // 0 indicates maximum number of mips.

  u32              orgChannels;
  AssetTextureType orgPixelType;
  u32              orgWidth, orgHeight, orgLayers;
} AssetImportTexture;

bool asset_import_texture(const AssetImportEnvComp*, String id, AssetImportTexture*);
