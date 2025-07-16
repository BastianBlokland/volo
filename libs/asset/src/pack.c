#include "asset_pack.h"
#include "core_alloc.h"
#include "core_file.h"

struct sAssetPacker {
  Allocator* alloc;
};

AssetPacker* asset_packer_create(Allocator* alloc, const u32 assetCapacity) {
  (void)assetCapacity;

  AssetPacker* packer = alloc_alloc_t(alloc, AssetPacker);

  *packer = (AssetPacker){
      .alloc = alloc,
  };

  return packer;
}

void asset_packer_destroy(AssetPacker* packer) { alloc_free_t(packer->alloc, packer); }

bool asset_packer_push(
    AssetPacker*              packer,
    AssetManagerComp*         manager,
    const AssetImportEnvComp* importEnv,
    const String              assetId) {
  (void)packer;
  (void)manager;
  (void)importEnv;
  (void)assetId;
  return true;
}

bool asset_packer_write(
    AssetPacker*              packer,
    AssetManagerComp*         manager,
    const AssetImportEnvComp* importEnv,
    File*                     outFile,
    AssetPackerStats*         outStats) {
  (void)packer;
  (void)manager;
  (void)importEnv;

  file_write_sync(outFile, string_lit("Hello World!"));

  if (outStats) {
    *outStats = (AssetPackerStats){
        .totalSize = 0,
    };
  }
  return true;
}
