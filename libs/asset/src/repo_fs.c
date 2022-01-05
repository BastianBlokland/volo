#include "core_alloc.h"
#include "core_file.h"
#include "core_file_monitor.h"
#include "core_path.h"
#include "log_logger.h"

#include "repo_internal.h"

typedef struct {
  AssetRepo    api;
  String       rootPath;
  FileMonitor* monitor;
} AssetRepoFs;

typedef struct {
  AssetSource api;
  File*       file;
} AssetSourceFs;

static void asset_source_fs_close(AssetSource* src) {
  AssetSourceFs* srcFs = (AssetSourceFs*)src;
  file_destroy(srcFs->file);
  alloc_free_t(g_alloc_heap, srcFs);
}

static AssetSource* asset_source_fs_open(AssetRepo* repo, const String id) {
  AssetRepoFs* repoFs = (AssetRepoFs*)repo;

  const AssetFormat fmt  = asset_format_from_ext(path_extension(id));
  const String      path = path_build_scratch(repoFs->rootPath, id);
  String            data;
  File*             file;
  FileResult        result;
  if ((result = file_create(g_alloc_heap, path, FileMode_Open, FileAccess_Read, &file))) {
    log_w(
        "AssetRepository: Failed to open file",
        log_param("path", fmt_path(path)),
        log_param("result", fmt_text(file_result_str(result))));
    return null;
  }
  if ((result = file_map(file, &data))) {
    log_w(
        "AssetRepository: Failed to map file",
        log_param("path", fmt_path(path)),
        log_param("result", fmt_text(file_result_str(result))));
    file_destroy(file);
    return null;
  }

  AssetSourceFs* src = alloc_alloc_t(g_alloc_heap, AssetSourceFs);

  *src = (AssetSourceFs){
      .api  = {.data = data, .format = fmt, .close = asset_source_fs_close},
      .file = file,
  };

  return (AssetSource*)src;
}

static void asset_repo_fs_changes_watch(AssetRepo* repo, const String id, const u64 userData) {
  AssetRepoFs* repoFs = (AssetRepoFs*)repo;

  const String            path = path_build_scratch(repoFs->rootPath, id);
  const FileMonitorResult res  = file_monitor_watch(repoFs->monitor, path, userData);
  if (UNLIKELY(res != FileMonitorResult_Success && res != FileMonitorResult_AlreadyWatching)) {
    log_w(
        "AssetRepository: Failed to watch file for changes",
        log_param("path", fmt_path(path)),
        log_param("result", fmt_text(file_monitor_result_str(res))));
  }
}

static bool asset_repo_fs_changes_poll(AssetRepo* repo, u64* outUserData) {
  AssetRepoFs* repoFs = (AssetRepoFs*)repo;

  FileMonitorEvent evt;
  if (file_monitor_poll(repoFs->monitor, &evt)) {
    *outUserData = evt.userData;
    return true;
  }
  return false;
}

static void asset_repo_fs_destroy(AssetRepo* repo) {
  AssetRepoFs* repoFs = (AssetRepoFs*)repo;

  string_free(g_alloc_heap, repoFs->rootPath);
  file_monitor_destroy(repoFs->monitor);
  alloc_free_t(g_alloc_heap, repoFs);
}

AssetRepo* asset_repo_create_fs(String rootPath) {
  AssetRepoFs* repo = alloc_alloc_t(g_alloc_heap, AssetRepoFs);

  *repo = (AssetRepoFs){
      .api =
          {
              .open         = asset_source_fs_open,
              .destroy      = asset_repo_fs_destroy,
              .changesWatch = asset_repo_fs_changes_watch,
              .changesPoll  = asset_repo_fs_changes_poll,
          },
      .rootPath = string_dup(g_alloc_heap, rootPath),
      .monitor  = file_monitor_create(g_alloc_heap),
  };

  return (AssetRepo*)repo;
}
