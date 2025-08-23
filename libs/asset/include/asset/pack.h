#pragma once
#include "asset/forward.h"

typedef struct sAssetPacker AssetPacker;

typedef struct {
  u64 size, padding;
  u64 headerSize;
  u32 entries;
  u32 regions;
  u32 blocks;
} AssetPackerStats;

AssetPacker* asset_packer_create(Allocator*, u32 assetCapacity);
void         asset_packer_destroy(AssetPacker*);

bool asset_packer_push(AssetPacker*, AssetManagerComp*, const AssetImportEnvComp*, String assetId);

bool asset_packer_write(
    AssetPacker*,
    AssetManagerComp*,
    const AssetImportEnvComp*,
    File*             outFile,
    AssetPackerStats* outStats);
