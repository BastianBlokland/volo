#pragma once
#include "repo_internal.h"

// Forward declare from 'import_internal.h'.
typedef struct sAssetImportEnvComp AssetImportEnvComp;

typedef void (*AssetLoader)(
    EcsWorld*, const AssetImportEnvComp*, String id, EcsEntityId assetEntity, AssetSource*);

AssetLoader asset_loader(AssetFormat);
u32         asset_loader_version(AssetFormat);
u32         asset_loader_hash(const AssetImportEnvComp*, String assetId);
