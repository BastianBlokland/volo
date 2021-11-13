#pragma once
#include "repo_internal.h"

typedef void (*AssetLoader)(EcsWorld*, EcsEntityId assetEntity, AssetSource*);

AssetLoader asset_loader(AssetFormat);
