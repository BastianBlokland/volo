#include "core_alloc.h"
#include "core_file.h"
#include "core_file_iterator.h"
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
  alloc_free_t(g_allocHeap, srcFs);
}

static bool asset_source_path(AssetRepo* repo, const String id, DynString* out) {
  AssetRepoFs* repoFs = (AssetRepoFs*)repo;

  path_build(out, repoFs->rootPath, id);
  return true;
}

static AssetSource* asset_source_fs_open(AssetRepo* repo, const String id) {
  AssetRepoFs* repoFs = (AssetRepoFs*)repo;

  const AssetFormat fmt  = asset_format_from_ext(path_extension(id));
  const String      path = path_build_scratch(repoFs->rootPath, id);
  String            data;
  File*             file;
  FileResult        result;
  if ((result = file_create(g_allocHeap, path, FileMode_Open, FileAccess_Read, &file))) {
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

  AssetSourceFs* src = alloc_alloc_t(g_allocHeap, AssetSourceFs);

  *src = (AssetSourceFs){
      .api  = {.data = data, .format = fmt, .close = asset_source_fs_close},
      .file = file,
  };

  return (AssetSource*)src;
}

static bool asset_repo_fs_save(AssetRepo* repo, const String id, const String data) {
  AssetRepoFs* repoFs = (AssetRepoFs*)repo;

  const String     path   = path_build_scratch(repoFs->rootPath, id);
  const FileResult result = file_write_to_path_sync(path, data);
  if (result) {
    log_w(
        "AssetRepository: Failed to save file",
        log_param("path", fmt_path(path)),
        log_param("result", fmt_text(file_result_str(result))));
  }
  return result == FileResult_Success;
}

static void asset_repo_fs_changes_watch(AssetRepo* repo, const String id, const u64 userData) {
  AssetRepoFs* repoFs = (AssetRepoFs*)repo;

  const FileMonitorResult res = file_monitor_watch(repoFs->monitor, id, userData);
  if (UNLIKELY(res != FileMonitorResult_Success && res != FileMonitorResult_AlreadyWatching)) {
    log_w(
        "AssetRepository: Failed to watch file for changes",
        log_param("id", fmt_path(id)),
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

typedef enum {
  AssetRepoFsQuery_Recursive = 1 << 0,
} AssetRepoFsQueryFlags;

static AssetRepoQueryResult asset_repo_fs_query_iteration(
    AssetRepoFs*                repoFs,
    const String                directory,
    const String                pattern,
    const AssetRepoFsQueryFlags flags,
    void*                       context,
    const AssetRepoQueryHandler handler) {

  if (UNLIKELY(directory.size) > 256) {
    // Sanity check the maximum directory length (relative to the repo root-path).
    log_w("AssetRepository: Directory path length exceeds maximum");
    return AssetRepoQueryResult_ErrorWhileQuerying;
  }

  Allocator* alloc     = alloc_bump_create_stack(768);
  DynString  dirBuffer = dynstring_create(alloc, 512);

  // Open a file iterator for the absolute path starting from the repo root-path.
  path_append(&dirBuffer, repoFs->rootPath);
  path_append(&dirBuffer, directory);
  FileIterator* itr = file_iterator_create(alloc, dynstring_view(&dirBuffer));

  FileIteratorEntry  entry;
  FileIteratorResult itrResult;
  while ((itrResult = file_iterator_next(itr, &entry)) == FileIteratorResult_Found) {
    // Construct a file path relative to the repo root-path.
    dynstring_clear(&dirBuffer);
    path_append(&dirBuffer, directory);
    path_append(&dirBuffer, entry.name);
    const String path = dynstring_view(&dirBuffer);

    switch (entry.type) {
    case FileType_Regular:
      if (string_match_glob(path, pattern, StringMatchFlags_None)) {
        handler(context, path);
      }
      break;
    case FileType_Directory:
      if (flags & AssetRepoFsQuery_Recursive) {
        // TODO: Handle errors for sub-directory iteration failure.
        asset_repo_fs_query_iteration(repoFs, path, pattern, flags, context, handler);
      }
      break;
    case FileType_None:
    case FileType_Unknown:
      break;
    }
  }
  file_iterator_destroy(itr);

  if (UNLIKELY(itrResult != FileIteratorResult_End)) {
    log_w(
        "AssetRepository: Error while performing file query",
        log_param("result", fmt_text(file_iterator_result_str(itrResult))));
    return AssetRepoQueryResult_ErrorWhileQuerying;
  }
  return AssetRepoQueryResult_Success;
}

static AssetRepoQueryResult asset_repo_fs_query(
    AssetRepo* repo, const String pattern, void* context, const AssetRepoQueryHandler handler) {
  AssetRepoFs* repoFs = (AssetRepoFs*)repo;

  // Find a root directory for the query.
  const String directory = path_parent(pattern);

  static const String g_globChars = string_static("*?");
  if (UNLIKELY(!sentinel_check(string_find_first_any(directory, g_globChars)))) {
    /**
     * Filtering in the directory part part is not supported at the moment.
     * Supporting this would require recursing from the first non-filtered directory.
     */
    log_w("AssetRepository: Unsupported file query pattern");
    return AssetRepoQueryResult_ErrorPatternNotSupported;
  }

  AssetRepoFsQueryFlags flags = 0;

  /**
   * Recursive queries are defined by a file-name starting with a wildcard.
   *
   * For example a query of `dir/ *.txt` will match both 'dir/hello.txt' and 'dir/sub/hello.txt',
   * '*.txt' will match any 'txt' files regardless how deeply its nested. This means there's no
   * way to search for direct children starting with a wildcard at the moment, in the future we
   * can consider supporting more exotic syntax like 'dir/ ** / *.txt' for recursive queries.
   */
  const String fileFilter = path_filename(pattern);
  if (string_starts_with(fileFilter, string_lit("*"))) {
    flags |= AssetRepoFsQuery_Recursive;
  }

  return asset_repo_fs_query_iteration(repoFs, directory, pattern, flags, context, handler);
}

static void asset_repo_fs_destroy(AssetRepo* repo) {
  AssetRepoFs* repoFs = (AssetRepoFs*)repo;

  string_free(g_allocHeap, repoFs->rootPath);
  file_monitor_destroy(repoFs->monitor);
  alloc_free_t(g_allocHeap, repoFs);
}

AssetRepo* asset_repo_create_fs(String rootPath) {
  AssetRepoFs* repo = alloc_alloc_t(g_allocHeap, AssetRepoFs);

  *repo = (AssetRepoFs){
      .api =
          {
              .path         = asset_source_path,
              .open         = asset_source_fs_open,
              .save         = asset_repo_fs_save,
              .destroy      = asset_repo_fs_destroy,
              .changesWatch = asset_repo_fs_changes_watch,
              .changesPoll  = asset_repo_fs_changes_poll,
              .query        = asset_repo_fs_query,
          },
      .rootPath = string_dup(g_allocHeap, rootPath),
      .monitor  = file_monitor_create(g_allocHeap, rootPath, FileMonitorFlags_None),
  };

  return (AssetRepo*)repo;
}
