#pragma once
#include "forward.h"
#include "repo.h"

typedef void (*AssetLoader)(
    EcsWorld*, const AssetImportEnvComp*, String id, EcsEntityId assetEntity, AssetSource*);

AssetLoader asset_loader(AssetFormat);
u32         asset_loader_version(AssetFormat);
u32         asset_loader_hash(const AssetImportEnvComp*, String assetId);
