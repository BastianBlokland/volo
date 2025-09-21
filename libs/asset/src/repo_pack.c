#include "core/alloc.h"
#include "core/bits.h"
#include "core/diag.h"
#include "core/dynarray.h"
#include "core/file.h"
#include "core/thread.h"
#include "data/read.h"
#include "data/utils.h"
#include "log/logger.h"

#include "pack.h"
#include "repo.h"

#define VOLO_ASSET_PACK_LOGGING 0
#define VOLO_ASSET_PACK_VALIDATE 0
#define VOLO_ASSET_PACK_PREMAP_SMALL_REGION 1

#define asset_pack_header_size (usize_mebibyte)

typedef struct {
  String mapping;
  i32    refCount;
  u32    mapCounter;
} AssetRegionState;

typedef struct {
  AssetRepo         api;
  File*             file;
  ThreadMutex       fileMutex;
  AssetRegionState* regions;
  AssetPackHeader   header;
  Allocator*        sourceAlloc; // Allocator for AssetSourcePack objects.
} AssetRepoPack;

typedef struct {
  AssetSource    api;
  AssetRepoPack* repo;
  u16            region;
} AssetSourcePack;

static bool asset_repo_pack_validate(const AssetPackHeader* header) {
  if (!header->entries.size) {
    return false;
  }
  if (!header->regions.size) {
    return false;
  }
  return true;
}

static const AssetPackEntry* asset_repo_pack_find(AssetRepoPack* pack, const String id) {
  const AssetPackEntry* entry = dynarray_search_binary(
      &pack->header.entries,
      asset_pack_compare_entry,
      &(AssetPackEntry){.idHash = string_hash(id)});
  if (!entry) {
    return null;
  }
  diag_assert_msg(
      string_eq(entry->id, id),
      "Hash collision detected: {} <-> {}",
      fmt_text(id),
      fmt_text(entry->id));
  return entry;
}

static String asset_repo_pack_acquire(AssetRepoPack* repo, const u16 region) {
  if (UNLIKELY(region >= repo->header.regions.size)) {
    diag_crash_msg("Corrupt pack file");
  }
  AssetRegionState* state        = repo->regions + region;
  const i32         prevRefCount = thread_atomic_add_i32(&state->refCount, 1);

  if (!prevRefCount || string_is_empty(state->mapping)) {
    thread_mutex_lock(repo->fileMutex);
    if (string_is_empty(state->mapping)) {
      const AssetPackRegion* info = dynarray_at_t(&repo->header.regions, region, AssetPackRegion);
      if (!info->size) {
        diag_crash_msg("Corrupt pack file");
      }
      if (file_map(repo->file, info->offset, info->size, FileHints_Prefetch, &state->mapping)) {
        diag_crash_msg("Failed to map pack region");
      }
      ++state->mapCounter;
#if VOLO_ASSET_PACK_LOGGING
      log_d(
          "Asset pack region mapped",
          log_param("region", fmt_int(region)),
          log_param("size", fmt_size(state->mapping.size)),
          log_param("counter", fmt_int(state->mapCounter)));
#endif
#if VOLO_ASSET_PACK_VALIDATE
      if (UNLIKELY(bits_crc_32(0, state->mapping) != info->checksum)) {
        diag_crash_msg("Pack region checksum failed");
      }
#endif
    }
    thread_mutex_unlock(repo->fileMutex);
  }
  return state->mapping;
}

static void asset_repo_pack_release(AssetRepoPack* repo, const u16 region) {
  AssetRegionState* state        = repo->regions + region;
  const i32         prevRefCount = thread_atomic_sub_i32(&state->refCount, 1);
  diag_assert_msg(prevRefCount, "Pack region double release");

  if (prevRefCount == 1) {
    thread_mutex_lock(repo->fileMutex);
    if (!thread_atomic_load_i32(&state->refCount) && !string_is_empty(state->mapping)) {
      const String toUnmap = state->mapping;
      state->mapping       = string_empty;
      if (file_unmap(repo->file, toUnmap)) {
        diag_crash_msg("Failed to unmap pack region");
      }
#if VOLO_ASSET_PACK_LOGGING
      log_d("Asset pack region unmapped", log_param("region", fmt_int(region)));
#endif
    }
    thread_mutex_unlock(repo->fileMutex);
  }
}

static bool asset_source_pack_stat(
    AssetRepo* repo, const String id, const AssetRepoLoaderHasher loaderHasher, AssetInfo* out) {
  (void)loaderHasher;

  AssetRepoPack*        repoPack = (AssetRepoPack*)repo;
  const AssetPackEntry* entry    = asset_repo_pack_find(repoPack, id);
  if (!entry) {
    return false;
  }
  *out = (AssetInfo){
      .format  = entry->format,
      .flags   = AssetInfoFlags_None,
      .size    = entry->size,
      .modTime = 0, // Mod-time not tracked in pack files.
  };
  return true;
}

static void asset_source_pack_close(AssetSource* src) {
  AssetSourcePack* srcPack = (AssetSourcePack*)src;
  asset_repo_pack_release(srcPack->repo, srcPack->region);
  alloc_free_t(srcPack->repo->sourceAlloc, srcPack);
}

static AssetSource*
asset_source_pack_open(AssetRepo* repo, const String id, const AssetRepoLoaderHasher loaderHasher) {
  (void)loaderHasher;

  AssetRepoPack*        repoPack = (AssetRepoPack*)repo;
  const AssetPackEntry* entry    = asset_repo_pack_find(repoPack, id);
  if (UNLIKELY(!entry)) {
    log_w("File missing from pack file", log_param("id", fmt_text(id)));
    return null;
  }
  const String regionMem = asset_repo_pack_acquire(repoPack, entry->region);
  if (UNLIKELY((entry->offset + entry->size) > regionMem.size)) {
    diag_crash_msg("Corrupt pack file");
  }

  AssetSourcePack* src = alloc_alloc_t(repoPack->sourceAlloc, AssetSourcePack);

  *src = (AssetSourcePack){
      .api =
          {
              .data     = mem_slice(regionMem, entry->offset, entry->size),
              .format   = entry->format,
              .flags    = AssetInfoFlags_None,
              .checksum = entry->checksum,
              .modTime  = 0, // Mod-time not tracked in pack files.
              .close    = asset_source_pack_close,
          },
      .repo   = repoPack,
      .region = entry->region,
  };

  return (AssetSource*)src;
}

static AssetRepoQueryResult asset_repo_pack_query(
    AssetRepo* repo, const String pattern, void* ctx, const AssetRepoQueryHandler handler) {
  AssetRepoPack* repoPack = (AssetRepoPack*)repo;

  dynarray_for_t(&repoPack->header.entries, AssetPackEntry, entry) {
    if (string_match_glob(entry->id, pattern, StringMatchFlags_None)) {
      handler(ctx, entry->id);
    }
  }

  return AssetRepoQueryResult_Success;
}

static void asset_repo_pack_destroy(AssetRepo* repo) {
  AssetRepoPack* repoPack = (AssetRepoPack*)repo;

  file_destroy(repoPack->file);
  thread_mutex_destroy(repoPack->fileMutex);
  alloc_free_array_t(g_allocHeap, repoPack->regions, repoPack->header.regions.size);
  data_destroy(g_dataReg, g_allocHeap, g_assetPackMeta, mem_var(repoPack->header));

  alloc_block_destroy(repoPack->sourceAlloc);

  alloc_free_t(g_allocHeap, repoPack);
}

static bool asset_repo_pack_read_header(File* file, const String filePath, AssetPackHeader* out) {
  FileResult fileRes;
  String     mapping;
  if ((fileRes = file_map(file, 0, asset_pack_header_size, FileHints_None, &mapping))) {
    log_e(
        "Failed to read pack header",
        log_param("path", fmt_path(filePath)),
        log_param("error", fmt_text(file_result_str(fileRes))));
    return false;
  }
  const DataReadFlags readFlags = DataReadFlags_None;
  DataReadResult      readRes;
  data_read_bin(
      g_dataReg, mapping, g_allocHeap, g_assetPackMeta, readFlags, mem_var(*out), &readRes);
  if (UNLIKELY(readRes.error)) {
    log_e(
        "Failed to read pack header",
        log_param("path", fmt_path(filePath)),
        log_param("error", fmt_text(readRes.errorMsg)));
    return false;
  }
  if ((fileRes = file_unmap(file, mapping))) {
    log_e(
        "Failed to unmap header mapping",
        log_param("path", fmt_path(filePath)),
        log_param("error", fmt_text(file_result_str(fileRes))));
  }
  return true;
}

AssetRepo* asset_repo_create_pack(const String filePath) {
  File*      file;
  FileResult fileRes;
  if ((fileRes = file_create(g_allocHeap, filePath, FileMode_Open, FileAccess_Read, &file))) {
    log_e(
        "Failed to open pack file",
        log_param("path", fmt_path(filePath)),
        log_param("error", fmt_text(file_result_str(fileRes))));
    return null;
  }
  AssetPackHeader header;
  if (!asset_repo_pack_read_header(file, filePath, &header)) {
    file_destroy(file);
    return null;
  }
  if (!asset_repo_pack_validate(&header)) {
    log_e("Malformed pack file", log_param("path", fmt_path(filePath)));
    data_destroy(g_dataReg, g_allocHeap, g_assetPackMeta, mem_var(header));
    file_destroy(file);
    return null;
  }
  AssetRepoPack* repo = alloc_alloc_t(g_allocHeap, AssetRepoPack);

  const u32         regionCount = (u32)header.regions.size;
  AssetRegionState* regions     = alloc_array_t(g_allocHeap, AssetRegionState, regionCount);
  mem_set(mem_create(regions, sizeof(AssetRegionState) * regionCount), 0);

  *repo = (AssetRepoPack){
      .api =
          {
              .stat    = asset_source_pack_stat,
              .open    = asset_source_pack_open,
              .destroy = asset_repo_pack_destroy,
              .query   = asset_repo_pack_query,
          },
      .file      = file,
      .fileMutex = thread_mutex_create(g_allocHeap),
      .regions   = regions,
      .header    = header,
      .sourceAlloc =
          alloc_block_create(g_allocHeap, sizeof(AssetSourcePack), alignof(AssetSourcePack)),
  };

  log_i(
      "Asset repository created",
      log_param("type", fmt_text_lit("pack")),
      log_param("path", fmt_path(filePath)),
      log_param("entries", fmt_int(header.entries.size)),
      log_param("regions", fmt_int(header.regions.size)));

#if VOLO_ASSET_PACK_PREMAP_SMALL_REGION
  asset_repo_pack_acquire(repo, 0 /* small assets region */);
#endif

  return (AssetRepo*)repo;
}
