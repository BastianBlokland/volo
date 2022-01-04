#include "core_alloc.h"
#include "core_annotation.h"
#include "core_bits.h"
#include "core_file.h"
#include "core_file_monitor.h"

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
 *
 * Another annoyance is that we have no good alternative to the linux inotify 'CLOSE_WRITE' event,
 * meaning we report multiple events while the file is being written, with no way to know when the
 * writer has finnished writing the file.
 */

typedef struct {
  File*    handle;
  u32      pathHash;
  String   path;
  u64      userData;
  TimeReal lastModTime;
} FileWatch;

struct sFileMonitor {
  Allocator* alloc;
  DynArray   watches;  // FileWatch[], kept sorted on the pathHash.
  DynArray   modFiles; // u32[] (file hashes)
};

static i8 watch_compare_path(const void* a, const void* b) {
  return compare_u32(field_ptr(a, FileWatch, pathHash), field_ptr(b, FileWatch, pathHash));
}

static FileWatch* file_watch_by_path(FileMonitor* monitor, const u32 pathHash) {
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

static void monitor_scan_modified_files(FileMonitor* monitor) {
  dynarray_for_t(&monitor->watches, FileWatch, watch) {
    const TimeReal newModTime = file_stat_sync(watch->handle).modTime;
    if (newModTime > watch->lastModTime) {
      *dynarray_push_t(&monitor->modFiles, u32) = watch->pathHash;
      watch->lastModTime                        = newModTime;
    }
  }
}

FileMonitor* file_monitor_create(Allocator* alloc) {
  FileMonitor* monitor = alloc_alloc_t(alloc, FileMonitor);
  *monitor             = (FileMonitor){
      .alloc    = alloc,
      .watches  = dynarray_create_t(alloc, FileWatch, 64),
      .modFiles = dynarray_create_t(alloc, u32, 16),
  };
  return monitor;
}

void file_monitor_destroy(FileMonitor* monitor) {
  dynarray_for_t(&monitor->watches, FileWatch, watch) {
    file_destroy(watch->handle);
    string_free(monitor->alloc, watch->path);
  }
  dynarray_destroy(&monitor->watches);
  dynarray_destroy(&monitor->modFiles);

  alloc_free_t(monitor->alloc, monitor);
}

FileMonitorResult file_monitor_watch(FileMonitor* monitor, const String path, const u64 userData) {
  const u32  pathHash      = bits_hash_32(path);
  FileWatch* existingWatch = file_watch_by_path(monitor, pathHash);
  if (existingWatch) {
    return FileMonitorResult_AlreadyWatching;
  }
  File*      handle;
  FileResult openRes;
  if ((openRes = file_create(monitor->alloc, path, FileMode_Open, FileAccess_None, &handle))) {
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

bool file_monitor_poll(FileMonitor* monitor, FileMonitorEvent* out) {
  if (!monitor->modFiles.size) {
    monitor_scan_modified_files(monitor);
  }

  if (monitor->modFiles.size) {
    const u32 pathHash = *dynarray_at_t(&monitor->modFiles, monitor->modFiles.size - 1, u32);
    dynarray_pop(&monitor->modFiles, 1);

    const FileWatch* watch = file_watch_by_path(monitor, pathHash);
    *out                   = (FileMonitorEvent){
        .path     = watch->path,
        .userData = watch->userData,
    };
    return true;
  }

  return false; // No files modified.
}
