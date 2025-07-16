#include "asset_pack.h"
#include "core_alloc.h"
#include "core_file.h"
#include "log_logger.h"

#include "manager_internal.h"
#include "repo_internal.h"

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

  AssetInfo info;
  if (UNLIKELY(!asset_source_stat(manager, importEnv, assetId, &info))) {
    log_e("Failed to pack missing asset", log_param("asset", fmt_text(assetId)));
    return false;
  }
  if (UNLIKELY(!(info.flags & AssetInfoFlags_Cached) && info.format != AssetFormat_Raw)) {
    /**
     * Packing a non-cached asset is supported but means the source asset will be packed and will
     * potentially need importing at runtime.
     */
    log_w("Packing non-cached asset", log_param("asset", fmt_text(assetId)));
  }

  (void)packer;
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
