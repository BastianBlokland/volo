#include "core/alloc.h"
#include "core/bits.h"
#include "core/dynstring.h"
#include "core/file.h"
#include "core/path.h"
#include "log/logger.h"

#include "repo.h"

typedef struct {
  AssetRepo  api;
  Allocator* sourceAlloc; // Allocator for AssetSourceUserFs objects.
} AssetRepoUserFs;

typedef struct {
  AssetSource      api;
  AssetRepoUserFs* repo;
  File*            file;
} AssetSourceUserFs;

static bool asset_source_userfs_path(AssetRepo* repo, const String id, DynString* out) {
  (void)repo;
  path_build(out, id); // If its a relative path the working-directory will be prepended.
  return true;
}

static bool asset_source_userfs_stat(
    AssetRepo* repo, const String id, const AssetRepoLoaderHasher loaderHasher, AssetInfo* out) {

  (void)repo;
  (void)loaderHasher;
  const FileInfo fileInfo = file_stat_path_sync(path_build_scratch(id));
  if (fileInfo.type != FileType_Regular) {
    return false;
  }
  *out = (AssetInfo){
      .format  = asset_format_from_ext(path_extension(id)),
      .flags   = AssetInfoFlags_None,
      .size    = fileInfo.size,
      .modTime = fileInfo.modTime,
  };
  return true;
}

static void asset_source_userfs_close(AssetSource* src) {
  AssetSourceUserFs* srcUserFs = (AssetSourceUserFs*)src;
  file_destroy(srcUserFs->file);
  alloc_free_t(srcUserFs->repo->sourceAlloc, srcUserFs);
}

static AssetSource* asset_source_userfs_open(
    AssetRepo* repo, const String id, const AssetRepoLoaderHasher loaderHasher) {

  (void)loaderHasher;
  AssetRepoUserFs* repoUserFs = (AssetRepoUserFs*)repo;

  const String path = path_build_scratch(id);
  String       data;
  File*        file;
  FileResult   result;
  if ((result = file_create(g_allocHeap, path, FileMode_Open, FileAccess_Read, &file))) {
    log_w(
        "Failed to open file",
        log_param("path", fmt_path(path)),
        log_param("result", fmt_text(file_result_str(result))));
    return null;
  }
  const FileInfo fileInfo = file_stat_sync(file);
  if (fileInfo.type != FileType_Regular) {
    log_w("Invalid source file", log_param("path", fmt_path(path)));
    file_destroy(file);
    return null;
  }
  if ((result = file_map(file, 0 /* offset */, 0 /* size */, FileHints_Prefetch, &data))) {
    log_w(
        "Failed to map file",
        log_param("path", fmt_path(path)),
        log_param("result", fmt_text(file_result_str(result))));
    file_destroy(file);
    return null;
  }

  AssetSourceUserFs* src = alloc_alloc_t(repoUserFs->sourceAlloc, AssetSourceUserFs);

  *src = (AssetSourceUserFs){
      .api =
          {
              .data     = data,
              .format   = asset_format_from_ext(path_extension(id)),
              .flags    = AssetInfoFlags_None,
              .checksum = bits_crc_32(0, data),
              .modTime  = fileInfo.modTime,
              .close    = asset_source_userfs_close,
          },
      .repo = repoUserFs,
      .file = file,
  };

  return (AssetSource*)src;
}

static bool asset_repo_userfs_save(AssetRepo* repo, const String id, const String data) {
  (void)repo;
  const String     path   = path_build_scratch(id);
  const FileResult result = file_write_to_path_atomic(path, data);
  if (result) {
    log_w(
        "Failed to save file",
        log_param("path", fmt_path(path)),
        log_param("result", fmt_text(file_result_str(result))));
  }
  return result == FileResult_Success;
}

static void asset_repo_userfs_destroy(AssetRepo* repo) {
  AssetRepoUserFs* repoUserFs = (AssetRepoUserFs*)repo;

  alloc_block_destroy(repoUserFs->sourceAlloc);
  alloc_free_t(g_allocHeap, repoUserFs);
}

AssetRepo* asset_repo_create_userfs(void) {
  AssetRepoUserFs* repo = alloc_alloc_t(g_allocHeap, AssetRepoUserFs);

  *repo = (AssetRepoUserFs){
      .api =
          {
              .path    = asset_source_userfs_path,
              .stat    = asset_source_userfs_stat,
              .open    = asset_source_userfs_open,
              .save    = asset_repo_userfs_save,
              .destroy = asset_repo_userfs_destroy,
          },
      .sourceAlloc =
          alloc_block_create(g_allocHeap, sizeof(AssetSourceUserFs), alignof(AssetSourceUserFs)),
  };
  log_i("Asset repository created", log_param("type", fmt_text_lit("user-file-system")));
  return (AssetRepo*)repo;
}
