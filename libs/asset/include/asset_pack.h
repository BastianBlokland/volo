#pragma once
#include "core.h"

typedef struct sAssetPacker AssetPacker;

typedef struct {
  usize totalSize;
} AssetPackerStats;

AssetPacker*     asset_packer_create(Allocator*);
void             asset_packer_destroy(AssetPacker*);
void             asset_packer_push(AssetPacker*, String assetId);
AssetPackerStats asset_packer_write(AssetPacker*, File*);
