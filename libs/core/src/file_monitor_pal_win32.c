#include "core_alloc.h"
#include "core_annotation.h"
#include "core_diag.h"
#include "core_file.h"
#include "core_file_monitor.h"
#include "core_path.h"
#include "core_thread.h"

/**
 * File Monitor.
 *
 * Unfortunately Win32 doesn't have a way to track modifications for a set of files.
 * The 'FindFirstChangeNotification' and 'ReadDirectoryChanges' apis both read changes in
 * directories and not in sets of files.
 *
 * Currently this implementation detects changes by polling for the 'modTime' of the files.
 * Unfortunately the cost of this scales linearly with the amount of files watched.
 *
 * In the future we could experiment with an alternative implementation that uses
 * 'ReadDirectoryChanges' to read changes in the parent directories of the files that we are
 * watching.
 */

typedef struct {
  File*      handle;
  StringHash pathHash;
  String     path;
  u64        userData;
  TimeReal   lastModTime;
} FileWatch;

struct sFileMonitor {
  Allocator*  alloc;
  ThreadMutex mutex;
  DynArray    watches;  // FileWatch[], kept sorted on the pathHash.
  DynArray    modFiles; // StringHash[] (file hashes)
  String      rootPath;
};

static i8 watch_compare_path(const void* a, const void* b) {
  return compare_stringhash(field_ptr(a, FileWatch, pathHash), field_ptr(b, FileWatch, pathHash));
}

static FileWatch* file_watch_by_path(FileMonitor* monitor, const StringHash pathHash) {
  return dynarray_search_binary(
      &monitor->watches, watch_compare_path, mem_struct(FileWatch, .pathHash = pathHash).ptr);
}

static FileMonitorResult monitor_result_from_file_result(const FileResult res) {
  switch (res) {
  case FileResult_Success:
    return FileMonitorResult_Success;
  case FileResult_NoAccess:
    return FileMonitorResult_NoAccess;
  case FileResult_PathTooLong:
    return FileMonitorResult_PathTooLong;
  case FileResult_NotFound:
    return FileMonitorResult_FileDoesNotExist;
  default:
    return FileMonitorResult_UnknownError;
  }
}

static bool monitor_file_can_be_read(FileMonitor* monitor, const String path) {
  /**
   * Check if the file can be opened for reading (meaning no-one is currently writing to it).
   * Reason for this check is to get semantics closer to the linux inotify 'CLOSE_WRITE' event where
   * we only report the event after file is finished being written to.
   */
  const String          pathAbs = path_build_scratch(monitor->rootPath, path);
  File*                 file;
  const FileAccessFlags access = FileAccess_Read;
  if (file_create(g_alloc_scratch, pathAbs, FileMode_Open, access, &file) != FileResult_Success) {
    return false;
  }
  file_destroy(file);
  return true;
}

static void monitor_scan_modified_files(FileMonitor* monitor) {
  dynarray_for_t(&monitor->watches, FileWatch, watch) {
    const TimeReal newModTime = file_stat_sync(watch->handle).modTime;
    if (newModTime > watch->lastModTime && monitor_file_can_be_read(monitor, watch->path)) {
      *dynarray_push_t(&monitor->modFiles, StringHash) = watch->pathHash;
      watch->lastModTime                               = newModTime;
    }
  }
}

/**
 * NOTE: Should only be called while having monitor->mutex locked.
 */
static FileMonitorResult
file_monitor_watch_locked(FileMonitor* monitor, const String path, const u64 userData) {
  const StringHash pathHash = string_hash(path);
  if (file_watch_by_path(monitor, pathHash)) {
    return FileMonitorResult_AlreadyWatching;
  }
  const String pathAbs = path_build_scratch(monitor->rootPath, path);
  File*        handle;
  FileResult   openRes;
  if ((openRes = file_create(monitor->alloc, pathAbs, FileMode_Open, FileAccess_None, &handle))) {
    return monitor_result_from_file_result(openRes);
  }
  const FileWatch watch = {
      .handle      = handle,
      .pathHash    = pathHash,
      .path        = string_dup(monitor->alloc, path),
      .userData    = userData,
      .lastModTime = file_stat_sync(handle).modTime,
  };
  *dynarray_insert_sorted_t(&monitor->watches, FileWatch, watch_compare_path, &watch) = watch;
  return FileMonitorResult_Success;
}

/**
 * NOTE: Should only be called while having monitor->mutex locked.
 */
static bool file_monitor_poll_locked(FileMonitor* monitor, FileMonitorEvent* out) {
  if (!monitor->modFiles.size) {
    monitor_scan_modified_files(monitor);
  }

  if (monitor->modFiles.size) {
    const StringHash p = *dynarray_at_t(&monitor->modFiles, monitor->modFiles.size - 1, StringHash);
    dynarray_pop(&monitor->modFiles, 1);

    const FileWatch* watch = file_watch_by_path(monitor, p);

    *out = (FileMonitorEvent){
        .path     = watch->path,
        .userData = watch->userData,
    };

    return true;
  }

  return false; // No files modified.
}

FileMonitor* file_monitor_create(Allocator* alloc, const String rootPath) {
  diag_assert(path_is_absolute(rootPath));

  FileMonitor* monitor = alloc_alloc_t(alloc, FileMonitor);

  *monitor = (FileMonitor){
      .alloc    = alloc,
      .mutex    = thread_mutex_create(alloc),
      .watches  = dynarray_create_t(alloc, FileWatch, 64),
      .modFiles = dynarray_create_t(alloc, StringHash, 16),
      .rootPath = string_dup(alloc, rootPath),
  };

  return monitor;
}

void file_monitor_destroy(FileMonitor* monitor) {
  dynarray_for_t(&monitor->watches, FileWatch, watch) {
    file_destroy(watch->handle);
    string_free(monitor->alloc, watch->path);
  }
  string_free(monitor->alloc, monitor->rootPath);
  dynarray_destroy(&monitor->watches);
  dynarray_destroy(&monitor->modFiles);
  thread_mutex_destroy(monitor->mutex);

  alloc_free_t(monitor->alloc, monitor);
}

FileMonitorResult file_monitor_watch(FileMonitor* monitor, const String path, const u64 userData) {
  diag_assert(!path_is_absolute(path));

  thread_mutex_lock(monitor->mutex);
  const FileMonitorResult res = file_monitor_watch_locked(monitor, path, userData);
  thread_mutex_unlock(monitor->mutex);
  return res;
}

bool file_monitor_poll(FileMonitor* monitor, FileMonitorEvent* out) {
  thread_mutex_lock(monitor->mutex);
  const FileMonitorResult res = file_monitor_poll_locked(monitor, out);
  thread_mutex_unlock(monitor->mutex);
  return res;
}
