#include "asset_pack.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_file.h"
#include "core_types.h"
#include "log_logger.h"

#include "manager_internal.h"
#include "repo_internal.h"

/**
 * Pack files combine multiple individual assets into a single blob to allow for more efficient
 * loading at runtime. Pack files are immutable and thus cannot be written to by the game.
 *
 * Pack blobs consists of a header followed by regions containing files, at runtime the individual
 * regions are mapped/unmapped as needed. To support delta patching the file is split into blocks,
 * the content of individual blocks is as consistent as possible (the order of blocks might shift
 * however).
 *
 * NOTE: Using 1 MiB blocks for compat with Steam: https://partner.steamgames.com/doc/sdk/uploading
 * NOTE: The header always needs to fit into a single block.
 */

#define asset_pack_block_size (usize_mebibyte)
#define asset_pack_small_entry_threshold (32 * usize_kibibyte)
#define asset_pack_big_entry_threshold (768 * usize_kibibyte)
#define asset_pack_other_buckets 64
#define asset_pack_file_align 16

typedef struct {
  String      assetId;
  StringHash  assetIdHash;
  AssetFormat format;
  u32         region;       // sentinel_u32 if not assigned to a region yet.
  u32         offset, size; // Within the region.
} AssetPackEntry;

typedef struct {
  usize offset, size; // Byte into the file.
} AssetPackRegion;

struct sAssetPacker {
  Allocator* alloc;
  Allocator* transientAlloc; // Used for temporary allocations.
  DynArray   entries;        // AssetPackEntry[].
  DynArray   regions;        // AssetPackRegion[].
  usize      sourceSize;
};

static i8 packer_compare_entry(const void* a, const void* b) {
  return compare_stringhash(
      field_ptr(a, AssetPackEntry, assetIdHash), field_ptr(b, AssetPackEntry, assetIdHash));
}

static bool packer_write_entry(
    AssetManagerComp*         manager,
    const AssetImportEnvComp* importEnv,
    const AssetPackEntry*     entry,
    const Mem                 regionMem) {
  AssetSource* source = asset_source_open(manager, importEnv, entry->assetId);
  if (UNLIKELY(!source)) {
    log_e("Asset source deleted while packing", log_param("asset", fmt_text(entry->assetId)));
    return false;
  }
  if (UNLIKELY(source->format != entry->format || source->data.size != entry->size)) {
    log_e("Asset source invalidated while packing", log_param("asset", fmt_text(entry->assetId)));
    asset_repo_close(source);
    return false;
  }
  mem_cpy(mem_slice(regionMem, entry->offset, entry->size), source->data);
  asset_repo_close(source);
  return true;
}

static u32 packer_add_region(AssetPacker* packer, const usize offset, const usize size) {
  diag_assert(bits_aligned(offset, asset_pack_block_size));
  diag_assert(bits_aligned(size, asset_pack_block_size));

  *dynarray_push_t(&packer->regions, AssetPackRegion) = (AssetPackRegion){
      .offset = offset,
      .size   = size,
  };
  return (u32)(packer->regions.size - 1);
}

/**
 * Write a region containing all small entries.
 * Combining these in a single region means this region will likely always change during patching
 * but because the entries are so small this region is unlikely to ever be bigger then a few blocks.
 */
static bool packer_add_small_entries(
    AssetPacker*              packer,
    AssetManagerComp*         manager,
    const AssetImportEnvComp* importEnv,
    File*                     file,
    usize*                    fileOffset) {
  usize regionSize = 0;
  dynarray_for_t(&packer->entries, AssetPackEntry, entry) {
    if (sentinel_check(entry->region) && entry->size <= asset_pack_small_entry_threshold) {
      regionSize += entry->size;
    }
  }
  if (!regionSize) {
    return true; // No small entries.
  }
  regionSize = bits_align(regionSize, asset_pack_block_size);

  FileResult fileRes;
  if (UNLIKELY(fileRes = file_resize_sync(file, *fileOffset + regionSize))) {
    log_e("Failed to resize pack file", log_param("error", fmt_text(file_result_str(fileRes))));
    return false;
  }
  String regionMapping;
  if (UNLIKELY(fileRes = file_map(file, *fileOffset, regionSize, 0, &regionMapping))) {
    log_e("Failed to map pack file", log_param("error", fmt_text(file_result_str(fileRes))));
    return false;
  }

  const u32 region       = packer_add_region(packer, *fileOffset, regionSize);
  bool      success      = true;
  u32       regionOffset = 0;
  dynarray_for_t(&packer->entries, AssetPackEntry, entry) {
    if (sentinel_check(entry->region) && entry->size <= asset_pack_small_entry_threshold) {
      entry->region = region;
      entry->offset = regionOffset;
      success &= packer_write_entry(manager, importEnv, entry, regionMapping);
      regionOffset += bits_align(entry->size, asset_pack_file_align);
    }
  }

  if (UNLIKELY(fileRes = file_unmap(file, regionMapping))) {
    log_e("Failed to unmap pack file", log_param("error", fmt_text(file_result_str(fileRes))));
  }
  *fileOffset += regionSize;
  return success;
}

/**
 * Push a new region for every big file.
 * Placing big files on individual regions (each starting at a block boundary) means delta patching
 * can re-use those blocks if the files didn't change.
 */
static bool packer_add_big_entries(
    AssetPacker*              packer,
    AssetManagerComp*         manager,
    const AssetImportEnvComp* importEnv,
    File*                     file,
    usize*                    fileOffset) {
  dynarray_for_t(&packer->entries, AssetPackEntry, entry) {
    if (!sentinel_check(entry->region) || entry->size < asset_pack_big_entry_threshold) {
      continue;
    }
    const usize regionSize = bits_align(entry->size, asset_pack_block_size);

    FileResult fileRes;
    if (UNLIKELY(fileRes = file_resize_sync(file, *fileOffset + regionSize))) {
      log_e("Failed to resize pack file", log_param("error", fmt_text(file_result_str(fileRes))));
      return false;
    }
    String regionMapping;
    if (UNLIKELY(fileRes = file_map(file, *fileOffset, regionSize, 0, &regionMapping))) {
      log_e("Failed to map pack file", log_param("error", fmt_text(file_result_str(fileRes))));
      return false;
    }
    entry->region = packer_add_region(packer, *fileOffset, regionSize);

    const bool success = packer_write_entry(manager, importEnv, entry, regionMapping);
    if (UNLIKELY(fileRes = file_unmap(file, regionMapping))) {
      log_e("Failed to unmap pack file", log_param("error", fmt_text(file_result_str(fileRes))));
    }
    if (!success) {
      return false;
    }
    *fileOffset += regionSize;
  }
  return true;
}

/**
 * For other files (non-small and non-big) we divide them into buckets based on their assetId hash.
 * This means if none of the files in the bucket change then the resulting region will not change.
 *
 * There's a tradeoff in the bucket count: higher means more wasted space but less unecessary region
 * changes.
 *
 * NOTE: In the future we can consider a smarter algorithm for dividing the entries into buckets
 * that takes the entry size into account to better load-balance the buckets.
 */
static bool packer_add_other_entries(
    AssetPacker*              packer,
    AssetManagerComp*         manager,
    const AssetImportEnvComp* importEnv,
    File*                     file,
    usize*                    fileOffset) {
  struct {
    u32 size, offset;
    u32 region;
    Mem mapping;
  } buckets[asset_pack_other_buckets];
  mem_set(array_mem(buckets), 0);

  // Compute the size for each bucket.
  dynarray_for_t(&packer->entries, AssetPackEntry, entry) {
    if (sentinel_check(entry->region)) {
      buckets[entry->assetIdHash % asset_pack_other_buckets].size += entry->size;
    }
  }

  // For each filled bucket allocate a region and map it.
  FileResult fileRes;
  bool       success = true;
  for (u32 i = 0; i != asset_pack_other_buckets; ++i) {
    if (!buckets[i].size) {
      continue; // Empty bucket.
    }
    buckets[i].size   = bits_align(buckets[i].size, asset_pack_block_size);
    buckets[i].region = packer_add_region(packer, *fileOffset, buckets[i].size);
    if (UNLIKELY(fileRes = file_resize_sync(file, *fileOffset + buckets[i].size))) {
      log_e("Failed to resize pack file", log_param("error", fmt_text(file_result_str(fileRes))));
      success = false;
      continue;
    }
    if (UNLIKELY(fileRes = file_map(file, *fileOffset, buckets[i].size, 0, &buckets[i].mapping))) {
      log_e("Failed to map pack file", log_param("error", fmt_text(file_result_str(fileRes))));
      success = false;
      continue;
    }
    *fileOffset += buckets[i].size;
  }

  // Write entries to the buckets.
  if (success) {
    dynarray_for_t(&packer->entries, AssetPackEntry, entry) {
      if (sentinel_check(entry->region)) {
        const u32 bucket = entry->assetIdHash % asset_pack_other_buckets;
        diag_assert(!string_is_empty(buckets[bucket].mapping));
        entry->region = buckets[bucket].region;
        entry->offset = buckets[bucket].offset;
        success &= packer_write_entry(manager, importEnv, entry, buckets[bucket].mapping);
        buckets[bucket].offset += bits_align(entry->size, asset_pack_file_align);
      }
    }
  }

  // Unmap all regions.
  for (u32 i = 0; i != asset_pack_other_buckets; ++i) {
    if (!string_is_empty(buckets[i])) {
      if (UNLIKELY(fileRes = file_unmap(file, buckets[i].mapping))) {
        log_e("Failed to unmap pack file", log_param("error", fmt_text(file_result_str(fileRes))));
      }
    }
  }

  return success;
}

AssetPacker* asset_packer_create(Allocator* alloc, const u32 assetCapacity) {
  AssetPacker* packer = alloc_alloc_t(alloc, AssetPacker);

  *packer = (AssetPacker){
      .alloc          = alloc,
      .transientAlloc = alloc_chunked_create(alloc, alloc_bump_create, 128 * usize_kibibyte),
      .entries        = dynarray_create_t(alloc, AssetPackEntry, assetCapacity),
      .regions        = dynarray_create_t(alloc, AssetPackRegion, 64),
  };

  return packer;
}

void asset_packer_destroy(AssetPacker* packer) {
  alloc_chunked_destroy(packer->transientAlloc);
  dynarray_destroy(&packer->entries);
  dynarray_destroy(&packer->regions);
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
  if (UNLIKELY(!info.size)) {
    log_e("Failed to pack zero-sized asset", log_param("asset", fmt_text(assetId)));
    return false;
  }
  if (UNLIKELY(!(info.flags & AssetInfoFlags_Cached) && info.format != AssetFormat_Raw)) {
    /**
     * Packing a non-cached asset is supported but means the source asset will be packed and will
     * potentially need importing at runtime.
     */
    log_w("Packing non-cached asset", log_param("asset", fmt_text(assetId)));
  }

  packer->sourceSize += info.size;

  const AssetPackEntry entry = {
      .assetId     = string_dup(packer->transientAlloc, assetId),
      .assetIdHash = string_hash(assetId),
      .format      = info.format,
      .size        = (u32)info.size,
      .region      = sentinel_u32,
  };
  *dynarray_insert_sorted_t(&packer->entries, AssetPackEntry, packer_compare_entry, &entry) = entry;
  return true;
}

bool asset_packer_write(
    AssetPacker*              packer,
    AssetManagerComp*         manager,
    const AssetImportEnvComp* importEnv,
    File*                     outFile,
    AssetPackerStats*         outStats) {

  usize fileOffset = asset_pack_block_size; // Reserve a single block for the header.
  if (!packer_add_small_entries(packer, manager, importEnv, outFile, &fileOffset)) {
    return false;
  }
  if (!packer_add_big_entries(packer, manager, importEnv, outFile, &fileOffset)) {
    return false;
  }
  if (!packer_add_other_entries(packer, manager, importEnv, outFile, &fileOffset)) {
    return false;
  }
  diag_assert(bits_aligned(fileOffset, asset_pack_block_size));

  const usize headerSize = asset_pack_block_size; // TODO: Compute header size.
  if (outStats) {
    *outStats = (AssetPackerStats){
        .size    = fileOffset,
        .padding = fileOffset - packer->sourceSize - headerSize,
        .entries = (u32)packer->entries.size,
        .regions = (u32)packer->regions.size,
        .blocks  = (u32)(fileOffset / asset_pack_block_size),
    };
  }
  return true;
}
