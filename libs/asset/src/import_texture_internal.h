#pragma once
#include "loader_texture_internal.h"

// Forward declare from 'import_internal.h'.
typedef struct sAssetImportEnvComp AssetImportEnvComp;

typedef enum {
  AssetImportTextureFlags_None     = 0,
  AssetImportTextureFlags_Lossless = 1 << 0,
  AssetImportTextureFlags_Linear   = 1 << 1,
  AssetImportTextureFlags_Mips     = 1 << 2,
} AssetImportTextureFlags;

typedef enum {
  AssetImportTextureFlip_None = 0,
  AssetImportTextureFlip_Y    = 1 << 0,
} AssetImportTextureFlip;

bool asset_import_texture(
    const AssetImportEnvComp*,
    String id,
    Mem    data /* NOTE: May be modified during the import process. */,
    u32    width,
    u32    height,
    u32    channels,
    AssetTextureType,
    AssetImportTextureFlags,
    AssetImportTextureFlip,
    AssetTextureComp* out);
