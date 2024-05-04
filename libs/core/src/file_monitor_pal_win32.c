#include "core_alloc.h"
#include "core_annotation.h"
#include "core_diag.h"
#include "core_file_monitor.h"
#include "core_path.h"
#include "core_thread.h"
#include "core_time.h"
#include "core_winutils.h"

#include "file_internal.h"

#include <Windows.h>

#define monitor_event_size (sizeof(FILE_NOTIFY_INFORMATION) + MAX_PATH)
#define monitor_path_chunk_size (16 * usize_kibibyte)

/**
 * Minimal interval between reporting changes on the same file.
 * Reason why we need this is that on Window's there's no such thing as linux inotify 'CLOSE_WRITE'
 * event so a single file-write can produce many events.
 */
#define monitor_min_interval time_milliseconds(10)

// Internal flags.
enum {
  FileMonitorFlags_ReadPending = 1 << (FileMonitorFlags_Count + 0),
};

typedef struct {
  String     path;
  u64        fileId;
  u64        userData;
  TimeSteady lastChangeTime;
} FileWatch;

struct sFileMonitor {
  Allocator*       alloc;
  Allocator*       allocPath; // (chunked) bump allocator for paths.
  ThreadMutex      mutex;
  FileMonitorFlags flags;
  String           rootPath;
  HANDLE           rootHandle;
  OVERLAPPED       rootOverlapped; // Overlapped IO handle for reading changes on the root dir.
  DynArray         watches;        // FileWatch[], kept sorted on the pathHash.

  Mem bufferRemaining;

  ALIGNAS(alignof(FILE_NOTIFY_INFORMATION))
  u8 buffer[monitor_event_size * 10]; // Big enough for atleast 10 events.
};

static i8 monitor_watch_compare(const void* a, const void* b) {
  return compare_u64(field_ptr(a, FileWatch, fileId), field_ptr(b, FileWatch, fileId));
}

static FileWatch* monitor_watch_by_file(FileMonitor* monitor, const u64 fileId) {
  return dynarray_search_binary(
      &monitor->watches, monitor_watch_compare, mem_struct(FileWatch, .fileId = fileId).ptr);
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

static u64 monitor_file_id_from_handle(const HANDLE handle) {
  BY_HANDLE_FILE_INFORMATION info;
  const BOOL                 success = GetFileInformationByHandle(handle, &info);
  if (UNLIKELY(!success)) {
    const DWORD err = GetLastError();
    diag_crash_msg(
        "GetFileInformationByHandle() failed: {}, {}",
        fmt_int((u64)err),
        fmt_text(winutils_error_msg_scratch(err)));
  }
  return ((u64)info.nFileIndexHigh << 32) | (u64)info.nFileIndexLow;
}

static FileMonitorResult
monitor_query_file(FileMonitor* monitor, const String path, u64* outId, usize* outSize) {
  const String          pathAbs = path_build_scratch(monitor->rootPath, path);
  const FileAccessFlags access  = FileAccess_None;
  File*                 file;
  FileResult            res;
  if ((res = file_create(g_alloc_scratch, pathAbs, FileMode_Open, access, &file))) {
    return monitor_result_from_file_result(res);
  }
  *outId   = monitor_file_id_from_handle(file->handle);
  *outSize = file_stat_sync(file).size;
  file_destroy(file);
  return FileMonitorResult_Success;
}

static HANDLE monitor_open_root(const String rootPath) {
  // Convert the path to a null-terminated wide-char string.
  const Mem rootPathWideStr = winutils_to_widestr_scratch(rootPath);

  // Open a handle to the root directory.
  const DWORD flags = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS |
                      FILE_FLAG_POSIX_SEMANTICS | FILE_FLAG_OVERLAPPED;
  return CreateFile(
      (const wchar_t*)rootPathWideStr.ptr,
      FILE_LIST_DIRECTORY,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      null,
      OPEN_EXISTING,
      flags,
      null);
}

static HANDLE monitor_event_create(void) {
  const HANDLE result = CreateEventEx(null, null, CREATE_EVENT_MANUAL_RESET, EVENT_ALL_ACCESS);
  if (UNLIKELY(!result)) {
    const DWORD err = GetLastError();
    diag_crash_msg(
        "CreateEventEx() failed: {}, {}",
        fmt_int((u64)err),
        fmt_text(winutils_error_msg_scratch(err)));
  }
  return result;
}

/**
 * NOTE: Should only be called while having monitor->mutex locked.
 */
static FileMonitorResult monitor_watch_locked(
    FileMonitor* monitor, const String path, const u64 fileId, const u64 userData) {

  if (monitor_watch_by_file(monitor, fileId)) {
    return FileMonitorResult_AlreadyWatching;
  }
  const FileWatch watch = {
      .path     = string_dup(monitor->allocPath, path),
      .fileId   = fileId,
      .userData = userData,
  };
  *dynarray_insert_sorted_t(&monitor->watches, FileWatch, monitor_watch_compare, &watch) = watch;
  return FileMonitorResult_Success;
}

/**
 * NOTE: Should only be called while having monitor->mutex locked.
 */
static void monitor_read_begin_locked(FileMonitor* monitor) {
  diag_assert(monitor->rootHandle != INVALID_HANDLE_VALUE);
  diag_assert(!(monitor->flags & FileMonitorFlags_ReadPending));

  const bool  watchSubtree = true;
  const DWORD notifyFilter = FILE_NOTIFY_CHANGE_LAST_WRITE;
  const bool  success      = ReadDirectoryChangesW(
      monitor->rootHandle,
      monitor->buffer,
      sizeof(monitor->buffer),
      watchSubtree,
      notifyFilter,
      null,
      &monitor->rootOverlapped,
      null);

  if (UNLIKELY(!success)) {
    const DWORD err = GetLastError();
    diag_crash_msg(
        "ReadDirectoryChanges() failed: {}, {}",
        fmt_int((u64)err),
        fmt_text(winutils_error_msg_scratch(err)));
  }

  monitor->flags |= FileMonitorFlags_ReadPending;
}

/**
 * NOTE: Should only be called while having monitor->mutex locked.
 */
static bool monitor_read_observe_locked(FileMonitor* monitor) {
  diag_assert(!monitor->bufferRemaining.size);

  const bool wait = (monitor->flags & FileMonitorFlags_Blocking) != 0;
  DWORD      bytesRead;
  if (!GetOverlappedResult(monitor->rootHandle, &monitor->rootOverlapped, &bytesRead, wait)) {
    const DWORD err = GetLastError();
    if (LIKELY(err == ERROR_IO_INCOMPLETE)) {
      return false; // No data available.
    }
    diag_crash_msg(
        "GetOverlappedResult() failed: {}, {}",
        fmt_int((u64)err),
        fmt_text(winutils_error_msg_scratch(err)));
  }

  monitor->bufferRemaining = mem_create(monitor->buffer, (usize)bytesRead);
  monitor->flags &= ~FileMonitorFlags_ReadPending;
  return true;
}

/**
 * NOTE: Should only be called while having monitor->mutex locked.
 */
static bool monitor_poll_locked(FileMonitor* monitor, FileMonitorEvent* out) {
Begin:
  /**
   * If our buffer is empty then read new events from the kernel.
   */
  if (!monitor->bufferRemaining.size && !monitor_read_observe_locked(monitor)) {
    return false; // No events available.
  }

  const TimeSteady timeNow = time_steady_clock();

  /**
   * Return the first valid event from the buffer.
   */
  while (monitor->bufferRemaining.size) {
    const FILE_NOTIFY_INFORMATION* event = monitor->bufferRemaining.ptr;
    if (event->NextEntryOffset) {
      monitor->bufferRemaining = mem_consume(monitor->bufferRemaining, event->NextEntryOffset);
    } else {
      monitor->bufferRemaining = mem_empty;
    }
    const usize  pathNumWideChars = event->FileNameLength / sizeof(WCHAR);
    const String path = winutils_from_widestr_scratch(event->FileName, pathNumWideChars);

    u64               fileId;
    usize             fileSize;
    FileMonitorResult res;
    if ((res = monitor_query_file(monitor, path, &fileId, &fileSize))) {
      continue; // Skip event; Unable to open file (could have been deleted since).
    }
    if (!fileSize) {
      continue; // Skip event; Empty file, most likely a truncate that will be followed by a write.
    }
    FileWatch* watch = monitor_watch_by_file(monitor, fileId);
    if (!watch) {
      continue; // Skip event; Not a file we are watching.
    }
    if (time_steady_duration(watch->lastChangeTime, timeNow) < monitor_min_interval) {
      continue; // Skip event; Already reported an event for this file in the interval window.
    }
    watch->lastChangeTime = timeNow;

    *out = (FileMonitorEvent){
        .path     = watch->path,
        .userData = watch->userData,
    };
    if (!monitor->bufferRemaining.size) {
      monitor_read_begin_locked(monitor); // Buffer fully consumed; start a new async read.
    }
    return true;
  }

  /**
   * Buffer contained no events for files we are watching.
   * Begin a new async read and restart this routine.
   */
  monitor_read_begin_locked(monitor);
  goto Begin;
}

FileMonitor* file_monitor_create(Allocator* alloc, const String rootPath, FileMonitorFlags flags) {
  const String rootPathAbs = path_build_scratch(rootPath);

  FileMonitor* monitor = alloc_alloc_t(alloc, FileMonitor);

  *monitor = (FileMonitor){
      .alloc      = alloc,
      .allocPath  = alloc_chunked_create(g_alloc_page, alloc_bump_create, monitor_path_chunk_size),
      .mutex      = thread_mutex_create(alloc),
      .flags      = flags,
      .watches    = dynarray_create_t(alloc, FileWatch, 64),
      .rootHandle = monitor_open_root(rootPathAbs),
      .rootOverlapped.hEvent = monitor_event_create(),
  };
  monitor->rootPath = string_dup(monitor->allocPath, rootPathAbs);

  if (monitor->rootHandle != INVALID_HANDLE_VALUE) {
    monitor_read_begin_locked(monitor);
  }

  return monitor;
}

void file_monitor_destroy(FileMonitor* monitor) {
  if (monitor->rootHandle != INVALID_HANDLE_VALUE) {
    CloseHandle(monitor->rootHandle);
  }
  CloseHandle(monitor->rootOverlapped.hEvent);
  alloc_chunked_destroy(monitor->allocPath);
  dynarray_destroy(&monitor->watches);
  thread_mutex_destroy(monitor->mutex);

  alloc_free_t(monitor->alloc, monitor);
}

FileMonitorResult file_monitor_watch(FileMonitor* monitor, const String path, const u64 userData) {
  diag_assert(!path_is_absolute(path));

  if (UNLIKELY(monitor->rootHandle == INVALID_HANDLE_VALUE)) {
    return FileMonitorResult_UnableToOpenRoot;
  }

  u64               fileId;
  usize             fileSize;
  FileMonitorResult res;
  if ((res = monitor_query_file(monitor, path, &fileId, &fileSize)) != FileMonitorResult_Success) {
    return res;
  }

  thread_mutex_lock(monitor->mutex);
  res = monitor_watch_locked(monitor, path, fileId, userData);
  thread_mutex_unlock(monitor->mutex);
  return res;
}

bool file_monitor_poll(FileMonitor* monitor, FileMonitorEvent* out) {
  thread_mutex_lock(monitor->mutex);
  const FileMonitorResult res = monitor_poll_locked(monitor, out);
  thread_mutex_unlock(monitor->mutex);
  return res;
}
