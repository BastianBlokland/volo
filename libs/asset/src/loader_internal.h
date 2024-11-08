#pragma once
#include "repo_internal.h"

typedef void (*AssetLoader)(EcsWorld*, String id, EcsEntityId assetEntity, AssetSource*);

AssetLoader asset_loader(AssetFormat);
u32         asset_loader_version(AssetFormat);
u32         asset_loader_hash(AssetFormat);
