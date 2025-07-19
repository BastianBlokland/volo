#include "core_alloc.h"
#include "core_diag.h"
#include "core_file.h"
#include "data_read.h"
#include "data_utils.h"
#include "log_logger.h"

#include "pack_internal.h"
#include "repo_internal.h"

#define asset_pack_header_size (usize_mebibyte)

typedef struct {
  AssetRepo       api;
  File*           file;
  AssetPackHeader header;
  Allocator*      sourceAlloc; // Allocator for AssetSourcePack objects.
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
  // TODO: Implement.
  alloc_free_t(srcPack->repo->sourceAlloc, srcPack);
}

static AssetSource*
asset_source_pack_open(AssetRepo* repo, const String id, const AssetRepoLoaderHasher loaderHasher) {
  AssetRepoPack* repoPack = (AssetRepoPack*)repo;
  (void)repoPack;
  (void)id;
  (void)loaderHasher;
  (void)asset_source_pack_close;
  // TODO: Implement.
  return null;
}

static AssetRepoQueryResult asset_repo_pack_query(
    AssetRepo* repo, const String pattern, void* context, const AssetRepoQueryHandler handler) {
  AssetRepoPack* repoPack = (AssetRepoPack*)repo;
  (void)repoPack;
  (void)pattern;
  (void)context;
  (void)handler;

  // TODO: Implement.
  return AssetRepoQueryResult_ErrorPatternNotSupported;
}

static void asset_repo_pack_destroy(AssetRepo* repo) {
  AssetRepoPack* repoPack = (AssetRepoPack*)repo;

  file_destroy(repoPack->file);
  data_destroy(g_dataReg, g_allocHeap, g_assetPackMeta, mem_var(repoPack->header));

  alloc_block_destroy(repoPack->sourceAlloc);

  alloc_free_t(g_allocHeap, repoPack);
}

static bool asset_repo_pack_read_header(File* file, AssetPackHeader* out) {
  FileResult fileRes;
  String     mapping;
  if ((fileRes = file_map(file, 0, asset_pack_header_size, FileHints_None, &mapping))) {
    log_e("Failed to read pack header", log_param("error", fmt_text(file_result_str(fileRes))));
    return false;
  }
  DataReadResult readRes;
  data_read_bin(g_dataReg, mapping, g_allocHeap, g_assetPackMeta, mem_var(*out), &readRes);
  if (UNLIKELY(readRes.error)) {
    log_e("Failed to read pack header", log_param("error", fmt_text(readRes.errorMsg)));
    return false;
  }
  if ((fileRes = file_unmap(file, mapping))) {
    log_e("Failed to unmap header mapping", log_param("error", fmt_text(file_result_str(fileRes))));
  }
  return true;
}

AssetRepo* asset_repo_create_pack(const String filePath) {
  File*      file;
  FileResult fileRes;
  if ((fileRes = file_create(g_allocHeap, filePath, FileMode_Open, FileAccess_Read, &file))) {
    log_e("Failed to open pack file", log_param("error", fmt_text(file_result_str(fileRes))));
    return null;
  }
  AssetPackHeader header;
  if (!asset_repo_pack_read_header(file, &header)) {
    return null;
  }
  if (!asset_repo_pack_validate(&header)) {
    log_e("Malformed pack file");
    data_destroy(g_dataReg, g_allocHeap, g_assetPackMeta, mem_var(header));
    return null;
  }
  AssetRepoPack* repo = alloc_alloc_t(g_allocHeap, AssetRepoPack);

  *repo = (AssetRepoPack){
      .api =
          {
              .stat    = asset_source_pack_stat,
              .open    = asset_source_pack_open,
              .destroy = asset_repo_pack_destroy,
              .query   = asset_repo_pack_query,
          },
      .file   = file,
      .header = header,
      .sourceAlloc =
          alloc_block_create(g_allocHeap, sizeof(AssetSourcePack), alignof(AssetSourcePack)),
  };

  log_i(
      "Asset repository created",
      log_param("type", fmt_text_lit("pack")),
      log_param("path", fmt_path(filePath)),
      log_param("entries", fmt_int(header.entries.size)),
      log_param("regions", fmt_int(header.regions.size)));

  return (AssetRepo*)repo;
}
