#include "core_alloc.h"
#include "core_file.h"
#include "log_logger.h"

#include "repo_internal.h"

typedef struct {
  AssetRepo  api;
  File*      file;
  Allocator* sourceAlloc; // Allocator for AssetSourcePack objects.
} AssetRepoPack;

typedef struct {
  AssetSource    api;
  AssetRepoPack* repo;
  u16            region;
} AssetSourcePack;

static bool asset_source_pack_stat(
    AssetRepo* repo, const String id, const AssetRepoLoaderHasher loaderHasher, AssetInfo* out) {
  AssetRepoPack* repoPack = (AssetRepoPack*)repo;
  (void)repoPack;
  (void)id;
  (void)loaderHasher;
  (void)out;
  // TODO: Implement.
  return false;
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
  alloc_block_destroy(repoPack->sourceAlloc);

  alloc_free_t(g_allocHeap, repoPack);
}

AssetRepo* asset_repo_create_pack(const String filePath) {
  File*      file;
  FileResult fileRes;
  if ((fileRes = file_create(g_allocHeap, filePath, FileMode_Open, FileAccess_Read, &file))) {
    log_e("Failed to open pack file", log_param("error", fmt_text(file_result_str(fileRes))));
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
      .file = file,
      .sourceAlloc =
          alloc_block_create(g_allocHeap, sizeof(AssetSourcePack), alignof(AssetSourcePack)),
  };

  log_i(
      "Asset repository created",
      log_param("type", fmt_text_lit("pack")),
      log_param("path", fmt_path(filePath)));

  return (AssetRepo*)repo;
}
