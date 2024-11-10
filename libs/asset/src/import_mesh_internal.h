#pragma once
#include "import_internal.h"

typedef struct {
  f32 vertexScale;
} AssetImportMesh;

bool asset_import_mesh(const AssetImportEnvComp*, String id, AssetImportMesh*);
