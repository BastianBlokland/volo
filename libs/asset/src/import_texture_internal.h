#pragma once
#include "import_internal.h"

typedef struct {
  u32 dummy;
} AssetImportTexture;

bool asset_import_texture(const AssetImportEnvComp*, String id, AssetImportTexture*);
