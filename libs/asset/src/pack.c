#include "asset_pack.h"
#include "core_alloc.h"
#include "core_file.h"

struct sAssetPacker {
  Allocator* alloc;
};

AssetPacker* asset_packer_create(Allocator* alloc) {
  AssetPacker* packer = alloc_alloc_t(alloc, AssetPacker);

  *packer = (AssetPacker){
      .alloc = alloc,
  };

  return packer;
}

void asset_packer_destroy(AssetPacker* packer) { alloc_free_t(packer->alloc, packer); }

void asset_packer_push(AssetPacker* packer, const String assetId) {
  (void)packer;
  (void)assetId;
}

AssetPackerStats asset_packer_write(AssetPacker* packer, File* file) {
  (void)packer;
  file_write_sync(file, string_lit("Hello World!"));
  return (AssetPackerStats){
      .totalSize = 0,
  };
}
