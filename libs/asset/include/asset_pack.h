#pragma once
#include "core.h"

typedef struct sAssetPacker AssetPacker;

typedef struct {
  usize totalSize;
} AssetPackerStats;

AssetPacker* asset_packer_create(Allocator*, u32 assetCapacity);
void         asset_packer_destroy(AssetPacker*);

bool asset_packer_push(AssetPacker*, String assetId);
bool asset_packer_write(AssetPacker*, File* outFile, AssetPackerStats* outStats);
