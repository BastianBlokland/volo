#include "asset_pack.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_file.h"
#include "log_logger.h"

#include "manager_internal.h"
#include "repo_internal.h"

typedef struct {
  String      assetId;
  StringHash  assetIdHash;
  AssetFormat format;
  usize       size;
} AssetPackerEntry;

struct sAssetPacker {
  Allocator* alloc;
  Allocator* transientAlloc; // Used for temporary allocations.
  DynArray   entries;        // AssetPackerEntry[].
};

static i8 packer_compare_entry(const void* a, const void* b) {
  return compare_stringhash(
      field_ptr(a, AssetPackerEntry, assetIdHash), field_ptr(b, AssetPackerEntry, assetIdHash));
}

AssetPacker* asset_packer_create(Allocator* alloc, const u32 assetCapacity) {
  (void)assetCapacity;

  AssetPacker* packer = alloc_alloc_t(alloc, AssetPacker);

  *packer = (AssetPacker){
      .alloc          = alloc,
      .transientAlloc = alloc_chunked_create(alloc, alloc_bump_create, 128 * usize_kibibyte),
      .entries        = dynarray_create_t(alloc, AssetPackerEntry, assetCapacity),
  };

  return packer;
}

void asset_packer_destroy(AssetPacker* packer) {
  alloc_chunked_destroy(packer->transientAlloc);
  dynarray_destroy(&packer->entries);
  alloc_free_t(packer->alloc, packer);
}

bool asset_packer_push(
    AssetPacker*              packer,
    AssetManagerComp*         manager,
    const AssetImportEnvComp* importEnv,
    const String              assetId) {
  diag_assert(!string_is_empty(assetId));

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

  const AssetPackerEntry e = {
      .assetId     = string_dup(packer->transientAlloc, assetId),
      .assetIdHash = string_hash(assetId),
      .format      = info.format,
      .size        = info.size,
  };
  *dynarray_insert_sorted_t(&packer->entries, AssetPackerEntry, packer_compare_entry, &e) = e;
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
