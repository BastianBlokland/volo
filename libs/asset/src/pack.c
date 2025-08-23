#include "asset/pack.h"
#include "core/alloc.h"
#include "core/array.h"
#include "core/bits.h"
#include "core/diag.h"
#include "core/dynarray.h"
#include "core/dynstring.h"
#include "core/file.h"
#include "core/stringtable.h"
#include "core/types.h"
#include "data/write.h"
#include "log/logger.h"

#include "data.h"
#include "manager.h"
#include "pack.h"
#include "repo.h"

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
#define asset_pack_other_buckets 32
#define asset_pack_file_align 16

DataMeta g_assetPackMeta;

struct sAssetPacker {
  Allocator* alloc;
  DynArray   entries; // AssetPackEntry[].
  DynArray   regions; // AssetPackRegion[].
  u64        sourceSize;
};

static bool packer_write_entry(
    AssetManagerComp*         manager,
    const AssetImportEnvComp* importEnv,
    AssetPackEntry*           entry,
    const Mem                 regionMem) {
  AssetSource* source = asset_source_open(manager, importEnv, entry->id);
  if (UNLIKELY(!source)) {
    log_e("Asset source deleted while packing", log_param("asset", fmt_text(entry->id)));
    return false;
  }
  if (UNLIKELY(source->format != entry->format || source->data.size != entry->size)) {
    log_e("Asset source invalidated while packing", log_param("asset", fmt_text(entry->id)));
    asset_repo_close(source);
    return false;
  }
  entry->checksum = bits_crc_32(0, source->data);
  mem_cpy(mem_slice(regionMem, entry->offset, entry->size), source->data);
  asset_repo_close(source);
  return true;
}

/**
 * Write the pack header to the first block of the file.
 * NOTE: The header needs to fit in a single block, otherwise this function will crash.
 */
static bool packer_write_header(AssetPacker* packer, File* file, u64* headerSize) {
  const AssetPackHeader header = {
      .entries = packer->entries,
      .regions = packer->regions,
  };
  FileResult fileRes;
  String     blockMapping;
  if (UNLIKELY(fileRes = file_map(file, 0, asset_pack_block_size, 0, &blockMapping))) {
    log_e("Failed to map pack file", log_param("error", fmt_text(file_result_str(fileRes))));
    return false;
  }
  DynString blockBuffer = dynstring_create_over(blockMapping);
  data_write_bin(g_dataReg, &blockBuffer, g_assetPackMeta, mem_var(header));

  if (UNLIKELY(blockBuffer.size > (u64)((f64)asset_pack_block_size * 0.75))) {
    log_w(
        "Pack header size is approaching the limit",
        log_param("size", fmt_size(blockBuffer.size)),
        log_param("limit", fmt_size(asset_pack_block_size)));
  }
  *headerSize = (u64)blockBuffer.size;

  if (UNLIKELY(fileRes = file_unmap(file, blockMapping))) {
    log_e("Failed to unmap pack file", log_param("error", fmt_text(file_result_str(fileRes))));
  }
  return true;
}

static u16 packer_region_add(AssetPacker* packer, const u64 offset, const u32 size) {
  diag_assert(bits_aligned(offset, asset_pack_block_size));
  diag_assert(bits_aligned(size, asset_pack_block_size));

  if (UNLIKELY(packer->regions.size == u16_max)) {
    diag_crash_msg("Pack region count exceeds limit: {}", fmt_int(u16_max));
  }

  *dynarray_push_t(&packer->regions, AssetPackRegion) = (AssetPackRegion){
      .offset = offset,
      .size   = size,
  };
  return (u16)(packer->regions.size - 1);
}

static void packer_region_compute_checksum(AssetPacker* packer, const u16 region, const Mem mem) {
  diag_assert(region < packer->regions.size);

  AssetPackRegion* data = dynarray_at_t(&packer->regions, region, AssetPackRegion);
  data->checksum        = bits_crc_32(0, mem);
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
    u64*                      fileOffset) {
  u32 regionSize = 0;
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

  const u16 region       = packer_region_add(packer, *fileOffset, regionSize);
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

  packer_region_compute_checksum(packer, region, regionMapping);
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
    u64*                      fileOffset) {
  dynarray_for_t(&packer->entries, AssetPackEntry, entry) {
    if (!sentinel_check(entry->region) || entry->size < asset_pack_big_entry_threshold) {
      continue;
    }
    const u32 regionSize = bits_align(entry->size, asset_pack_block_size);

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
    entry->region      = packer_region_add(packer, *fileOffset, regionSize);
    const bool success = packer_write_entry(manager, importEnv, entry, regionMapping);

    packer_region_compute_checksum(packer, entry->region, regionMapping);
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
    u64*                      fileOffset) {
  struct {
    u32 size, offset;
    u16 region;
    Mem mapping;
  } buckets[asset_pack_other_buckets];
  mem_set(array_mem(buckets), 0);

  // Compute the size for each bucket.
  dynarray_for_t(&packer->entries, AssetPackEntry, entry) {
    if (sentinel_check(entry->region)) {
      buckets[entry->idHash % asset_pack_other_buckets].size += entry->size;
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
    buckets[i].region = packer_region_add(packer, *fileOffset, buckets[i].size);
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
        const u32 bucket = entry->idHash % asset_pack_other_buckets;
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
      packer_region_compute_checksum(packer, buckets[i].region, buckets[i].mapping);
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
      .alloc   = alloc,
      .entries = dynarray_create_t(alloc, AssetPackEntry, assetCapacity),
      .regions = dynarray_create_t(alloc, AssetPackRegion, 128),
  };

  return packer;
}

void asset_packer_destroy(AssetPacker* packer) {
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
  if (UNLIKELY(info.size > (u32_max - asset_pack_block_size))) {
    log_e("Asset too big to pack", log_param("asset", fmt_text(assetId)));
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

  const AssetPackEntry e = {
      .id       = stringtable_intern(g_stringtable, assetId),
      .idHash   = string_hash(assetId),
      .format   = info.format,
      .checksum = sentinel_u32, // Filled in when writing.
      .size     = (u32)info.size,
      .region   = sentinel_u16, // Assigned when writing.
  };
  *dynarray_insert_sorted_t(&packer->entries, AssetPackEntry, asset_pack_compare_entry, &e) = e;
  return true;
}

bool asset_packer_write(
    AssetPacker*              packer,
    AssetManagerComp*         manager,
    const AssetImportEnvComp* importEnv,
    File*                     outFile,
    AssetPackerStats*         outStats) {
  if (UNLIKELY(!dynarray_size(&packer->entries))) {
    log_e("Empty pack file is not supported");
    return false;
  }
  u64 fileOffset = asset_pack_block_size; // Reserve a single block for the header.
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

  u64 headerSize;
  if (!packer_write_header(packer, outFile, &headerSize)) {
    return false;
  }
  if (outStats) {
    *outStats = (AssetPackerStats){
        .size       = fileOffset,
        .padding    = fileOffset - packer->sourceSize - headerSize,
        .headerSize = headerSize,
        .entries    = (u32)packer->entries.size,
        .regions    = (u32)packer->regions.size,
        .blocks     = (u32)(fileOffset / asset_pack_block_size),
    };
  }
  return true;
}

void asset_data_init_pack(void) {
  // clang-format off
  data_reg_struct_t(g_dataReg, AssetPackEntry);
  data_reg_field_t(g_dataReg, AssetPackEntry, id, data_prim_t(String), .flags = DataFlags_Intern);
  data_reg_field_t(g_dataReg, AssetPackEntry, idHash, data_prim_t(u32));
  data_reg_field_t(g_dataReg, AssetPackEntry, format, g_assetFormatType);
  data_reg_field_t(g_dataReg, AssetPackEntry, checksum, data_prim_t(u32));
  data_reg_field_t(g_dataReg, AssetPackEntry, region, data_prim_t(u16));
  data_reg_field_t(g_dataReg, AssetPackEntry, offset, data_prim_t(u32));
  data_reg_field_t(g_dataReg, AssetPackEntry, size, data_prim_t(u32));

  data_reg_struct_t(g_dataReg, AssetPackRegion);
  data_reg_field_t(g_dataReg, AssetPackRegion, offset, data_prim_t(u64));
  data_reg_field_t(g_dataReg, AssetPackRegion, size, data_prim_t(u32));
  data_reg_field_t(g_dataReg, AssetPackRegion, checksum, data_prim_t(u32));

  data_reg_struct_t(g_dataReg, AssetPackHeader);
  data_reg_field_t(g_dataReg, AssetPackHeader, entries, t_AssetPackEntry, .container = DataContainer_DynArray);
  data_reg_field_t(g_dataReg, AssetPackHeader, regions, t_AssetPackRegion, .container = DataContainer_DynArray);
  // clang-format on

  g_assetPackMeta = data_meta_t(t_AssetPackHeader);
}

i8 asset_pack_compare_entry(const void* a, const void* b) {
  return compare_stringhash(
      field_ptr(a, AssetPackEntry, idHash), field_ptr(b, AssetPackEntry, idHash));
}
